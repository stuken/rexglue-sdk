/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <rex/audio/audio_media_player.h>

#include <algorithm>
#include <cstring>

#include <rex/audio/asf_demuxer.h>
#include <rex/audio/audio_driver.h>
#include <rex/audio/audio_system.h>
#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/memory/utils.h>
#include <rex/runtime.h>
#include <rex/string.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xtypes.h>

REXCVAR_DEFINE_BOOL(enable_xmp, true, "Audio",
                    "Enables XMP (custom soundtrack) playback.");

extern "C" {
#if REX_COMPILER_MSVC
#pragma warning(push)
#pragma warning(disable : 4101 4244 5033)
#endif
#include "libavcodec/avcodec.h"
#if REX_COMPILER_MSVC
#pragma warning(pop)
#endif
}  // extern "C"

namespace rex::audio {

namespace {
constexpr int kOutputSampleRate = 48000;
constexpr size_t kOutputChannels = 6;
constexpr size_t kSamplesPerChannel = 256;
constexpr int kQueuedFrames = 8;

// Skips a leading ID3v2 tag (common on real-world MP3 files), which the raw
// MPEG audio parser doesn't know about. Returns the number of bytes to skip.
size_t SkipId3v2Tag(const uint8_t* data, size_t size) {
  if (size < 10 || data[0] != 'I' || data[1] != 'D' || data[2] != '3') {
    return 0;
  }
  const uint32_t tag_size = (uint32_t(data[6] & 0x7F) << 21) | (uint32_t(data[7] & 0x7F) << 14) |
                            (uint32_t(data[8] & 0x7F) << 7) | uint32_t(data[9] & 0x7F);
  const bool has_footer = (data[5] & 0x10) != 0;
  const size_t total = size_t(10) + tag_size + (has_footer ? 10 : 0);
  return std::min(total, size);
}
}  // namespace

AudioMediaPlayer::AudioMediaPlayer(rex::system::KernelState* kernel_state)
    : kernel_state_(kernel_state), memory_(kernel_state->memory()) {
  if (!REXCVAR_GET(enable_xmp)) {
    // worker_thread_ stays null; Play() below checks for that and no-ops,
    // so every other method's existing early-out guards keep working
    // unchanged (Stop() is only a no-op when idle-with-nothing-pending,
    // which is exactly the state XMP starts in and stays in when disabled).
    return;
  }
  worker_running_.store(true, std::memory_order_relaxed);
  worker_thread_ = rex::thread::Thread::Create({}, [this]() { WorkerThreadMain(); });
  if (!worker_thread_) {
    REXAPU_ERROR("AudioMediaPlayer: failed to create the XMP worker thread - XMP disabled");
    worker_running_.store(false, std::memory_order_relaxed);
    return;
  }
  worker_thread_->set_name("XMP Audio Media Player");
}

AudioMediaPlayer::~AudioMediaPlayer() {
  Stop();
  worker_running_.store(false, std::memory_order_relaxed);
  resume_fence_.Signal();
  if (worker_thread_) {
    rex::thread::Wait(worker_thread_.get(), false);
  }
  TeardownDriver();
}

void AudioMediaPlayer::Play(Song* song) {
  if (!worker_thread_) {
    REXAPU_DEBUG("AudioMediaPlayer: Play() called for '{}' but XMP is disabled (enable_xmp=false)",
                rex::string::to_utf8(song->name));
    return;
  }
  REXAPU_DEBUG("AudioMediaPlayer: Play() called for song '{}'", rex::string::to_utf8(song->name));
  Stop();
  {
    auto lock = global_critical_region_.Acquire();
    pending_song_ = song;
  }
  song_should_stop_.store(false, std::memory_order_relaxed);
  state_.store(State::kPlaying, std::memory_order_relaxed);
  resume_fence_.Signal();
}

void AudioMediaPlayer::Stop() {
  if (state_.load(std::memory_order_relaxed) == State::kIdle && !pending_song_) {
    return;
  }
  song_should_stop_.store(true, std::memory_order_relaxed);
  state_.store(State::kIdle, std::memory_order_relaxed);
  resume_fence_.Signal();
  processing_end_fence_.Wait();
}

void AudioMediaPlayer::Pause() {
  State expected = State::kPlaying;
  state_.compare_exchange_strong(expected, State::kPaused, std::memory_order_relaxed);
}

void AudioMediaPlayer::Continue() {
  State expected = State::kPaused;
  state_.compare_exchange_strong(expected, State::kPlaying, std::memory_order_relaxed);
}

void AudioMediaPlayer::WorkerThreadMain() {
  while (worker_running_.load(std::memory_order_relaxed)) {
    resume_fence_.Wait();
    if (!worker_running_.load(std::memory_order_relaxed)) {
      break;
    }

    Song* song = nullptr;
    {
      auto lock = global_critical_region_.Acquire();
      song = pending_song_;
      pending_song_ = nullptr;
    }

    if (song) {
      PlaySong(song);
    }

    processing_end_fence_.Signal();
  }
}

bool AudioMediaPlayer::EnsureDriver() {
  if (driver_) {
    return true;
  }

  audio_system_ = static_cast<rex::audio::AudioSystem*>(kernel_state_->emulator()->audio_system());
  if (!audio_system_) {
    return false;
  }

  driver_semaphore_ = rex::thread::Semaphore::Create(kQueuedFrames, kQueuedFrames);
  if (!driver_semaphore_) {
    return false;
  }

  AudioDriver* driver = nullptr;
  X_STATUS status = audio_system_->CreateStandaloneDriver(driver_semaphore_.get(), &driver);
  if (XFAILED(status) || !driver) {
    driver_semaphore_.reset();
    return false;
  }
  driver_ = driver;

  frame_guest_ptr_ =
      kernel_state_->memory()->SystemHeapAlloc(uint32_t(kOutputChannels * kSamplesPerChannel *
                                                        sizeof(float)));
  if (!frame_guest_ptr_) {
    TeardownDriver();
    return false;
  }

  return true;
}

void AudioMediaPlayer::TeardownDriver() {
  if (driver_) {
    audio_system_->DestroyStandaloneDriver(driver_);
    driver_ = nullptr;
  }
  driver_semaphore_.reset();
  if (frame_guest_ptr_) {
    kernel_state_->memory()->SystemHeapFree(frame_guest_ptr_);
    frame_guest_ptr_ = 0;
  }
}

void AudioMediaPlayer::WaitWhilePaused() {
  if (state_.load(std::memory_order_relaxed) != State::kPaused) {
    return;
  }
  // driver_ is guaranteed valid here: this only runs from within
  // PlayMp3Data/PlayWmaData, which PlaySong only calls after EnsureDriver()
  // has already succeeded, and nothing tears driver_ down again until this
  // song's processing has returned to WorkerThreadMain.
  driver_->Pause();
  while (state_.load(std::memory_order_relaxed) == State::kPaused &&
        !song_should_stop_.load(std::memory_order_relaxed)) {
    rex::thread::Sleep(std::chrono::milliseconds(20));
  }
  driver_->Resume();
}

void AudioMediaPlayer::DrainQueue() {
  // driver_semaphore_ starts at kQueuedFrames and is created with that same
  // maximum count (EnsureDriver), so it can never hold more than
  // kQueuedFrames permits at once. SubmitPendingFrame acquires one permit
  // per frame it hands to driver_->SubmitFrame(), and SDLAudioDriver
  // releases one back once SDLCallback has actually dequeued and output
  // that frame (see SDLCallback's semaphore_->Release() call) - so
  // "permits available" tracks exactly how many submitted frames the
  // driver hasn't finished outputting yet. Acquiring all kQueuedFrames of
  // them ourselves is therefore only possible once none remain
  // outstanding, i.e. the driver has genuinely finished outputting
  // everything submitted for the song that just finished decoding - not
  // just that decoding itself finished. Released back immediately after so
  // the next song's SubmitPendingFrame calls see a full queue again.
  int acquired = 0;
  for (; acquired < kQueuedFrames; ++acquired) {
    if (song_should_stop_.load(std::memory_order_relaxed)) {
      break;  // An explicit Stop()/Play() wants an immediate cut, not this.
    }
    // Bounded rather than blocking forever, same reasoning as
    // SubmitPendingFrame's own wait: a stuck/dead driver shouldn't wedge
    // every future song behind this drain.
    if (rex::thread::Wait(driver_semaphore_.get(), false, std::chrono::milliseconds(500)) !=
        rex::thread::WaitResult::kSuccess) {
      REXAPU_WARN("AudioMediaPlayer: DrainQueue timed out waiting for the driver to catch up");
      break;
    }
  }
  if (acquired > 0) {
    driver_semaphore_->Release(acquired, nullptr);
  }
}

std::vector<uint8_t> AudioMediaPlayer::LoadSongToMemory(rex::system::KernelState* kernel_state,
                                                        const std::u16string& file_path) {
  rex::filesystem::File* vfs_file = nullptr;
  rex::filesystem::FileAction file_action;
  X_STATUS result = kernel_state->file_system()->OpenFile(
      nullptr, rex::string::to_utf8(file_path), rex::filesystem::FileDisposition::kOpen,
      rex::filesystem::FileAccess::kGenericRead, false, true, &vfs_file, &file_action);
  if (XFAILED(result) || !vfs_file) {
    return {};
  }

  std::vector<uint8_t> buffer(vfs_file->entry()->size());
  size_t bytes_read = 0;
  result = vfs_file->ReadSync(buffer, 0, &bytes_read);
  vfs_file->Destroy();
  if (XFAILED(result)) {
    return {};
  }
  buffer.resize(bytes_read);
  return buffer;
}

namespace {
// Extracts interleaved-stereo float samples from a decoded AVFrame (mono is
// duplicated to both channels) and hands them to the caller-supplied sink.
template <typename Sink>
void ForEachDecodedSample(const AVFrame* frame, Sink&& sink) {
  const int channels = std::max(frame->channels, 1);
  const int count = frame->nb_samples;
  if (count <= 0) {
    return;
  }

  if (frame->format == AV_SAMPLE_FMT_FLTP) {
    const float* plane0 = reinterpret_cast<const float*>(frame->data[0]);
    const float* plane1 = channels > 1 ? reinterpret_cast<const float*>(frame->data[1]) : plane0;
    for (int i = 0; i < count; ++i) {
      sink(plane0[i], plane1[i]);
    }
  } else if (frame->format == AV_SAMPLE_FMT_FLT) {
    const float* interleaved = reinterpret_cast<const float*>(frame->data[0]);
    for (int i = 0; i < count; ++i) {
      const float l = interleaved[i * channels + 0];
      sink(l, channels > 1 ? interleaved[i * channels + 1] : l);
    }
  } else {
    REXAPU_WARN("AudioMediaPlayer: unexpected decoded sample format {}",
               static_cast<int>(frame->format));
  }
}
}  // namespace

void AudioMediaPlayer::AppendDecoded(const float* left, const float* right, size_t count,
                                     int in_rate) {
  resample_in_left_.insert(resample_in_left_.end(), left, left + count);
  resample_in_right_.insert(resample_in_right_.end(), right, right + count);

  const double step = static_cast<double>(in_rate) / static_cast<double>(kOutputSampleRate);
  while (resample_in_left_.size() >= 2 &&
        static_cast<size_t>(resample_pos_) + 1 < resample_in_left_.size()) {
    if (song_should_stop_.load(std::memory_order_relaxed)) {
      break;
    }

    const size_t i0 = static_cast<size_t>(resample_pos_);
    const double frac = resample_pos_ - static_cast<double>(i0);
    const float l = static_cast<float>(resample_in_left_[i0] * (1.0 - frac) +
                                       resample_in_left_[i0 + 1] * frac);
    const float r = static_cast<float>(resample_in_right_[i0] * (1.0 - frac) +
                                       resample_in_right_[i0 + 1] * frac);
    pending_left_.push_back(l);
    pending_right_.push_back(r);
    resample_pos_ += step;

    if (pending_left_.size() >= kSamplesPerChannel) {
      SubmitPendingFrame();
    }
  }

  const size_t consumed =
      std::min(static_cast<size_t>(resample_pos_), resample_in_left_.size());
  if (consumed > 0) {
    resample_in_left_.erase(resample_in_left_.begin(), resample_in_left_.begin() + consumed);
    resample_in_right_.erase(resample_in_right_.begin(), resample_in_right_.begin() + consumed);
    resample_pos_ -= static_cast<double>(consumed);
  }
}

void AudioMediaPlayer::SubmitPendingFrame() {
  // A timeout just means the output device is behind; keep decoding rather
  // than stalling the worker thread forever on a stuck/dead driver.
  rex::thread::Wait(driver_semaphore_.get(), false, std::chrono::milliseconds(500));

  // Pushed to the driver rather than baked into the samples below, so the
  // driver's own dequeue/mix stage (SDLAudioDriver::SDLCallback) applies it
  // to every frame still sitting in its queue, not just this one - see
  // SetVolume's comment for why this push (driver_ access) has to happen
  // here, on the worker thread, rather than directly inside SetVolume().
  driver_->SetVolume(volume_.load(std::memory_order_relaxed));

  uint8_t* host_ptr = memory_->TranslateVirtual(frame_guest_ptr_);
  std::memset(host_ptr, 0, kOutputChannels * kSamplesPerChannel * sizeof(float));
  float* channel0 = reinterpret_cast<float*>(host_ptr) + 0 * kSamplesPerChannel;
  float* channel1 = reinterpret_cast<float*>(host_ptr) + 1 * kSamplesPerChannel;
  for (size_t i = 0; i < kSamplesPerChannel; ++i) {
    rex::memory::store_and_swap<float>(&channel0[i], pending_left_[i]);
    rex::memory::store_and_swap<float>(&channel1[i], pending_right_[i]);
  }
  // Channels 2-5 (fc, lf, bl, br) are left silent (already zeroed above).

  driver_->SubmitFrame(frame_guest_ptr_);

  pending_left_.erase(pending_left_.begin(), pending_left_.begin() + kSamplesPerChannel);
  pending_right_.erase(pending_right_.begin(), pending_right_.begin() + kSamplesPerChannel);
}

void AudioMediaPlayer::PlaySong(Song* song) {
  // Loops (rather than recursing) so repeat-mode playlists don't grow the
  // worker thread's stack across an unbounded number of auto-advanced
  // songs. Each iteration plays one song; song_ended_callback_ (if a song
  // ran out of input on its own, not via an external stop request) decides
  // what plays next, same as an internally-triggered "Next()" - see its
  // comment in xmp_app.h/.cpp for the threading argument that makes this
  // safe without a lock.
  while (song) {
    REXAPU_DEBUG("AudioMediaPlayer: worker picked up song '{}' (path='{}', format={}, volume={:g})",
                rex::string::to_utf8(song->name), rex::string::to_utf8(song->file_path),
                static_cast<uint32_t>(song->format), volume_.load(std::memory_order_relaxed));

    // A title that has never set a real volume - or explicitly asks for
    // exactly 0.0 - is asking for "whatever the system default is": on real
    // hardware/xenia this reads a persisted XCONFIG_USER_MUSIC_VOLUME setting
    // (see xe::apu::AudioMediaPlayer::Play()'s `if (volume_ == 0.0f) { volume_
    // = xconfig()->ReadSetting(...); }`). RexGlue has no persisted user-
    // settings store yet (that's the GPD/profile rewrite tracked separately in
    // XAM_PORT_AUDIT.md), so full volume is used as the closest available
    // default rather than staying silent - confirmed via PGR3 logs setting
    // volume to 0.0 once at boot and never raising it again, which would
    // otherwise permanently silence every song for the rest of the session.
    if (volume_.load(std::memory_order_relaxed) == 0.0f) {
      volume_.store(1.0f, std::memory_order_relaxed);
      REXAPU_INFO("AudioMediaPlayer: volume was 0 (no real value set yet) - defaulting to 1.0");
    }

    std::vector<uint8_t> file_data = LoadSongToMemory(kernel_state_, song->file_path);
    if (file_data.empty()) {
      REXAPU_WARN("AudioMediaPlayer: failed to load song file '{}' for playback",
                 rex::string::to_utf8(song->file_path));
      state_.store(State::kIdle, std::memory_order_relaxed);
      return;
    }
    REXAPU_DEBUG("AudioMediaPlayer: loaded {} bytes for '{}'", file_data.size(),
                rex::string::to_utf8(song->file_path));

    if (!EnsureDriver()) {
      REXAPU_ERROR("AudioMediaPlayer: failed to create an audio driver for XMP playback");
      state_.store(State::kIdle, std::memory_order_relaxed);
      return;
    }

    resample_in_left_.clear();
    resample_in_right_.clear();
    resample_pos_ = 0.0;
    pending_left_.clear();
    pending_right_.clear();

    switch (song->format) {
      case Song::Format::kMp3:
        PlayMp3Data(file_data);
        break;
      case Song::Format::kWma:
        PlayWmaData(file_data);
        break;
      default:
        REXAPU_WARN("AudioMediaPlayer: unknown song format {} for '{}' - skipping playback",
                   static_cast<uint32_t>(song->format), rex::string::to_utf8(song->name));
        state_.store(State::kIdle, std::memory_order_relaxed);
        return;
    }

    if (song_should_stop_.load(std::memory_order_relaxed)) {
      // Ended via an explicit Stop()/Play()/... request, not naturally -
      // state_ is already whatever that caller set it to (Stop() sets
      // kIdle itself; Play() sets kPlaying for the song it's switching to).
      // Don't auto-advance: the request already decided what's next.
      return;
    }

    // Ending naturally: wait for the driver to actually finish outputting
    // this song's audio (not just for decoding to finish) before letting
    // song_ended_callback_ advance XmpApp's active_song_index_ and
    // broadcast the "now playing" change - see DrainQueue()'s comment.
    DrainQueue();
    if (song_should_stop_.load(std::memory_order_relaxed)) {
      return;
    }

    song = song_ended_callback_ ? song_ended_callback_() : nullptr;
  }

  // Ran out of input and there's nothing next (no callback, or it returned
  // nullptr) - genuinely idle now, not just between two auto-advanced songs.
  state_.store(State::kIdle, std::memory_order_relaxed);
}

void AudioMediaPlayer::PlayMp3Data(const std::vector<uint8_t>& file_data) {
  const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_MP3);
  if (!codec) {
    REXAPU_ERROR("AudioMediaPlayer: MP3 decoder not available in this build");
    return;
  }
  AVCodecContext* av_context = avcodec_alloc_context3(codec);
  if (!av_context) {
    return;
  }
  av_context->channels = 0;
  av_context->sample_rate = 0;
  if (avcodec_open2(av_context, codec, nullptr) < 0) {
    REXAPU_ERROR("AudioMediaPlayer: failed to open MP3 decoder");
    avcodec_free_context(&av_context);
    return;
  }

  AVCodecParserContext* parser = av_parser_init(AV_CODEC_ID_MP3);
  if (!parser) {
    REXAPU_ERROR("AudioMediaPlayer: failed to init MPEG audio parser");
    avcodec_free_context(&av_context);
    return;
  }

  AVPacket* packet = av_packet_alloc();
  AVFrame* frame = av_frame_alloc();

  const uint8_t* data = file_data.data();
  size_t data_size = file_data.size();
  const size_t id3_skip = SkipId3v2Tag(data, data_size);
  data += id3_skip;
  data_size -= id3_skip;

  size_t decoded_frame_count = 0;
  auto decode_frame_to_pending = [&](const AVFrame* decoded) {
    std::vector<float> left, right;
    left.reserve(decoded->nb_samples);
    right.reserve(decoded->nb_samples);
    ForEachDecodedSample(decoded, [&](float l, float r) {
      left.push_back(l);
      right.push_back(r);
    });
    if (left.empty()) {
      return;
    }
    if (decoded_frame_count == 0) {
      REXAPU_DEBUG("AudioMediaPlayer: MP3 decoding audio ({} Hz, {} channel(s), volume={:g})",
                  decoded->sample_rate, decoded->channels, volume_.load(std::memory_order_relaxed));
    }
    ++decoded_frame_count;
    // Volume isn't touched here at all - it's pushed straight to the driver
    // each submit cycle instead (see SubmitPendingFrame).
    const int sample_rate = decoded->sample_rate > 0 ? decoded->sample_rate : kOutputSampleRate;
    AppendDecoded(left.data(), right.data(), left.size(), sample_rate);
  };

  while (data_size > 0 && !song_should_stop_.load(std::memory_order_relaxed)) {
    WaitWhilePaused();
    if (song_should_stop_.load(std::memory_order_relaxed)) {
      break;
    }

    uint8_t* out_data = nullptr;
    int out_size = 0;
    const int consumed =
        av_parser_parse2(parser, av_context, &out_data, &out_size, data,
                         static_cast<int>(data_size), AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
    if (consumed < 0) {
      break;
    }
    data += consumed;
    data_size -= static_cast<size_t>(consumed);

    if (out_size <= 0) {
      continue;
    }

    av_packet_unref(packet);
    packet->data = out_data;
    packet->size = out_size;
    if (avcodec_send_packet(av_context, packet) < 0) {
      continue;
    }

    while (!song_should_stop_.load(std::memory_order_relaxed)) {
      const int ret = avcodec_receive_frame(av_context, frame);
      if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        break;
      }
      if (ret < 0) {
        break;
      }
      decode_frame_to_pending(frame);
    }
  }

  if (!song_should_stop_.load(std::memory_order_relaxed)) {
    avcodec_send_packet(av_context, nullptr);
    while (avcodec_receive_frame(av_context, frame) >= 0) {
      decode_frame_to_pending(frame);
    }
  }

  REXAPU_DEBUG(
      "AudioMediaPlayer: MP3 playback ended ({} frames decoded, stopped={})",
      decoded_frame_count, song_should_stop_.load(std::memory_order_relaxed));

  av_frame_free(&frame);
  av_packet_free(&packet);
  av_parser_close(parser);
  avcodec_free_context(&av_context);
}

void AudioMediaPlayer::PlayWmaData(const std::vector<uint8_t>& file_data) {
  AsfDemuxer demuxer;
  if (!demuxer.Open(file_data.data(), file_data.size())) {
    REXAPU_WARN(
        "AudioMediaPlayer: couldn't find a playable WMA audio stream in this "
        "file (not a valid ASF file, no audio stream, or an unsupported "
        "codec - only WMAv2/WMAPro are vendored)");
    return;
  }
  const AsfDemuxer::AudioInfo& info = demuxer.audio_info();
  REXAPU_DEBUG(
      "AudioMediaPlayer: found WMA stream (format_tag={:#06x}, {} Hz, {} channel(s), "
      "block_align={}, extradata={} bytes)",
      info.format_tag, info.sample_rate, info.channels, info.block_align,
      info.extra_data.size());

  AVCodecID codec_id;
  switch (info.format_tag) {
    case 0x0161:
      codec_id = AV_CODEC_ID_WMAV2;
      break;
    case 0x0162:
      codec_id = AV_CODEC_ID_WMAPRO;
      break;
    default:
      // AsfDemuxer::Open() only succeeds for the two tags above, so this is
      // unreachable in practice - kept as a defensive fallback.
      REXAPU_WARN("AudioMediaPlayer: unsupported WMA format tag {:#06x}", info.format_tag);
      return;
  }

  const AVCodec* codec = avcodec_find_decoder(codec_id);
  if (!codec) {
    REXAPU_ERROR("AudioMediaPlayer: WMA decoder not available in this build");
    return;
  }
  AVCodecContext* av_context = avcodec_alloc_context3(codec);
  if (!av_context) {
    return;
  }
  // Unlike MP3, the WMA decoders can't derive these from the bitstream - they
  // come straight from the ASF Stream Properties Object's WAVEFORMATEX-shaped
  // type-specific data (see asf_demuxer.cpp).
  av_context->sample_rate = static_cast<int>(info.sample_rate);
  av_context->channels = info.channels;
  av_context->bit_rate = static_cast<int64_t>(info.avg_bytes_per_sec) * 8;
  av_context->block_align = info.block_align;
  if (!info.extra_data.empty()) {
    av_context->extradata =
        static_cast<uint8_t*>(av_mallocz(info.extra_data.size() + AV_INPUT_BUFFER_PADDING_SIZE));
    if (av_context->extradata) {
      std::memcpy(av_context->extradata, info.extra_data.data(), info.extra_data.size());
      av_context->extradata_size = static_cast<int>(info.extra_data.size());
    }
  }

  if (avcodec_open2(av_context, codec, nullptr) < 0) {
    REXAPU_ERROR("AudioMediaPlayer: failed to open WMA decoder");
    avcodec_free_context(&av_context);
    return;
  }

  AVPacket* packet = av_packet_alloc();
  AVFrame* frame = av_frame_alloc();
  std::vector<uint8_t> compressed_frame;

  size_t decoded_frame_count = 0;
  auto decode_frame_to_pending = [&](const AVFrame* decoded) {
    std::vector<float> left, right;
    left.reserve(decoded->nb_samples);
    right.reserve(decoded->nb_samples);
    ForEachDecodedSample(decoded, [&](float l, float r) {
      left.push_back(l);
      right.push_back(r);
    });
    if (left.empty()) {
      return;
    }
    if (decoded_frame_count == 0) {
      REXAPU_DEBUG("AudioMediaPlayer: WMA decoding audio (volume={:g})",
                  volume_.load(std::memory_order_relaxed));
    }
    ++decoded_frame_count;
    // Volume isn't touched here at all - it's pushed straight to the driver
    // each submit cycle instead (see SubmitPendingFrame).
    const int sample_rate = decoded->sample_rate > 0 ? decoded->sample_rate : kOutputSampleRate;
    AppendDecoded(left.data(), right.data(), left.size(), sample_rate);
  };

  while (!song_should_stop_.load(std::memory_order_relaxed)) {
    WaitWhilePaused();
    if (song_should_stop_.load(std::memory_order_relaxed)) {
      break;
    }

    if (!demuxer.ReadNextFrame(&compressed_frame)) {
      break;  // end of the audio stream, or an unrecoverable parse error
    }
    if (compressed_frame.empty()) {
      continue;
    }

    av_packet_unref(packet);
    packet->data = compressed_frame.data();
    packet->size = static_cast<int>(compressed_frame.size());
    if (avcodec_send_packet(av_context, packet) < 0) {
      continue;
    }

    while (!song_should_stop_.load(std::memory_order_relaxed)) {
      const int ret = avcodec_receive_frame(av_context, frame);
      if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        break;
      }
      if (ret < 0) {
        break;
      }
      decode_frame_to_pending(frame);
    }
  }

  if (!song_should_stop_.load(std::memory_order_relaxed)) {
    avcodec_send_packet(av_context, nullptr);
    while (avcodec_receive_frame(av_context, frame) >= 0) {
      decode_frame_to_pending(frame);
    }
  }

  REXAPU_DEBUG(
      "AudioMediaPlayer: WMA playback ended ({} frames decoded, stopped={})",
      decoded_frame_count, song_should_stop_.load(std::memory_order_relaxed));

  av_frame_free(&frame);
  av_packet_free(&packet);
  avcodec_free_context(&av_context);
}

}  // namespace rex::audio

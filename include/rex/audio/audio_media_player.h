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

#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include <rex/kernel/xam/apps/xmp_app.h>
#include <rex/thread.h>
#include <rex/thread/mutex.h>

namespace rex::system {
class KernelState;
}  // namespace rex::system

namespace rex::memory {
class Memory;
}  // namespace rex::memory

namespace rex::audio {

class AudioDriver;
class AudioSystem;

// Real playback backend for XmpApp (the XMP "custom soundtrack" player).
//
// Xenia's equivalent (xe::apu::AudioMediaPlayer) drives a host-float
// AudioDriver interface fed by an independently-created driver instance.
// RexGlue's rex::audio::AudioDriver instead exposes
// SubmitFrame(uint32_t frame_ptr), where frame_ptr is a *guest* address
// holding a fixed-format frame: 6 channels (fl, fr, fc, lf, bl, br) of 256
// samples each, stored as big-endian floats in planar (non-interleaved)
// layout - see conversion::sequential_6_BE_to_interleaved_* - plus
// SetVolume/Pause/Resume, applied by the driver itself at its own
// dequeue/mix stage rather than by this class pre-scaling or dropping
// submitted samples (see SetVolume and WaitWhilePaused below for why driver_
// access stays confined to the worker thread either way). There is also no
// public "give me an untracked driver" factory (AudioSystem::CreateDriver is
// protected, reachable only via the 8-slot guest-callback client table used
// by XAudioRegisterRenderDriverClient). This class owns a small persistent
// guest heap buffer it writes each outgoing frame into, and uses
// AudioSystem::CreateStandaloneDriver (added alongside this class) to get a
// driver instance that isn't part of that guest-callback table, since XMP
// playback is driven by this class's own worker thread rather than by a
// guest-supplied callback.
//
// Only MP3 playback is implemented. Both the MP3 and WMA (WMAv2/WMAPro) raw
// codecs are already vendored (see thirdparty/ffmpeg-overlay/codec_list.c),
// but libavformat is not built into this project - only libavutil and
// libavcodec are (see thirdparty/CMakeLists.txt's FFmpeg section) - and MP3
// is the one common XMP format that doesn't need a container demuxer: MPEG
// audio frames are self-delimiting, and the already-vendored MPEG audio
// parser (CONFIG_MPEGAUDIO_PARSER) finds frame boundaries directly in the
// raw file bytes. WMA is ASF-container-based and would need a real ASF
// demuxer (not just the codec) to extract frames; that's real, well-scoped
// follow-up work, not attempted here - see XAM_PORT_AUDIT.md item 6.7.
class AudioMediaPlayer {
 public:
  using Song = rex::kernel::xam::apps::XmpApp::Song;
  using State = rex::kernel::xam::apps::XmpApp::State;

  explicit AudioMediaPlayer(rex::system::KernelState* kernel_state);
  ~AudioMediaPlayer();

  // Starts decoding/playing `song` on the worker thread, synchronously
  // stopping whatever was previously playing first. `song` must stay valid
  // until a subsequent Play()/Stop() call returns (Stop() blocks until the
  // worker thread is no longer touching it).
  void Play(Song* song);

  // Stops playback and blocks until the worker thread has stopped touching
  // the previously active Song, so it's safe to free it immediately after
  // this call returns.
  void Stop();

  void Pause();
  void Continue();

  bool is_idle() const { return state_.load(std::memory_order_relaxed) == State::kIdle; }
  bool is_playing() const { return state_.load(std::memory_order_relaxed) == State::kPlaying; }
  bool is_paused() const { return state_.load(std::memory_order_relaxed) == State::kPaused; }

  // Stores the requested volume; does *not* touch driver_ directly, since
  // this can be called from any thread (whatever dispatches XMPSetVolume)
  // while driver_ is only ever created/destroyed on the worker thread
  // (EnsureDriver/TeardownDriver) - reading it here without synchronization
  // could race a concurrent TeardownDriver(). Instead the worker thread reads
  // this atomic and pushes it to driver_->SetVolume() once per submitted
  // output frame (~5.3ms, see SubmitPendingFrame), where the driver applies
  // it at actual dequeue/mix time - not baked into sample data - so it also
  // reaches frames already queued in the driver, not just newly-submitted
  // ones. Matches xenia's own clamp (std::min(volume, 1.0f)) - only an upper
  // bound, since a title is never expected to send anything above unity gain.
  void SetVolume(float volume) {
    volume_.store(std::min(volume, 1.0f), std::memory_order_relaxed);
  }
  float GetVolume() const { return volume_.load(std::memory_order_relaxed); }

 private:
  void WorkerThreadMain();
  void PlaySong(Song* song);
  // One decode/feed loop per supported compressed format - see
  // audio_media_player.cpp and asf_demuxer.h for why WMA needs its own
  // container-level demuxing step where MP3 doesn't.
  void PlayMp3Data(const std::vector<uint8_t>& file_data);
  void PlayWmaData(const std::vector<uint8_t>& file_data);
  static std::vector<uint8_t> LoadSongToMemory(rex::system::KernelState* kernel_state,
                                                const std::u16string& file_path);

  // Blocks (only) while state_ is kPaused, pausing/resuming driver_ - a real
  // OS-level device pause (SDLAudioDriver::Pause -> SDL_PauseAudioDevice)
  // rather than just letting already-queued frames drain to silence - around
  // the wait. Called from PlayMp3Data/PlayWmaData's decode loops, which run
  // on the worker thread, so this keeps driver_ access single-threaded the
  // same way SetVolume's comment describes; AudioMediaPlayer::Pause()/
  // Continue() (called from whatever thread dispatches XMP pause messages)
  // only flip state_ and let this notice it instead of touching driver_
  // themselves.
  void WaitWhilePaused();

  bool EnsureDriver();
  void TeardownDriver();

  // Appends newly-decoded interleaved-stereo samples at `in_rate` Hz to the
  // resampling pipeline, linearly resampling to the driver's fixed 48 kHz
  // and submitting complete 256-sample driver frames as they fill.
  void AppendDecoded(const float* left, const float* right, size_t count, int in_rate);
  void SubmitPendingFrame();

  rex::system::KernelState* kernel_state_ = nullptr;
  rex::memory::Memory* memory_ = nullptr;

  std::atomic<State> state_ = State::kIdle;
  std::atomic<bool> song_should_stop_ = false;

  std::atomic<bool> worker_running_ = false;
  std::unique_ptr<rex::thread::Thread> worker_thread_;
  rex::thread::Fence resume_fence_;
  rex::thread::Fence processing_end_fence_;

  rex::thread::global_critical_region global_critical_region_;
  Song* pending_song_ = nullptr;

  std::atomic<float> volume_ = 1.0f;

  rex::audio::AudioSystem* audio_system_ = nullptr;
  std::unique_ptr<rex::thread::Semaphore> driver_semaphore_;
  rex::audio::AudioDriver* driver_ = nullptr;
  uint32_t frame_guest_ptr_ = 0;

  // Resampling pipeline state, reset at the start of each song.
  std::vector<float> resample_in_left_;
  std::vector<float> resample_in_right_;
  double resample_pos_ = 0.0;
  std::vector<float> pending_left_;
  std::vector<float> pending_right_;
};

}  // namespace rex::audio

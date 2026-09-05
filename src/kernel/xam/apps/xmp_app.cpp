/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <algorithm>

#include <rex/audio/audio_media_player.h>
#include <rex/kernel/xam/apps/xmp_app.h>
#include <rex/logging.h>
#include <rex/system/xthread.h>
#include <rex/thread.h>

namespace rex {
namespace kernel {
namespace xam {
using namespace rex::system;
using namespace rex::system::xam;
namespace apps {
using namespace rex::system;

XmpApp::XmpApp(KernelState* kernel_state)
    : App(kernel_state, 0xFA),
      state_(State::kIdle),
      playback_client_(PlaybackClient::kTitle),
      playback_mode_(PlaybackMode::kInOrder),
      repeat_mode_(RepeatMode::kPlaylist),
      unknown_flags_(0),
      volume_(1.0f),
      active_playlist_(nullptr),
      active_song_index_(0),
      next_playlist_handle_(1),
      next_song_handle_(1),
      media_player_(std::make_unique<rex::audio::AudioMediaPlayer>(kernel_state)) {
  media_player_->SetSongEndedCallback([this] { return OnSongEndedNaturally(); });
}

XmpApp::~XmpApp() = default;

X_HRESULT XmpApp::XMPGetStatus(uint32_t state_ptr) {
  if (!XThread::GetCurrentThread()->main_thread()) {
    // Some stupid games will hammer this on a thread - induce a delay
    // here to keep from starving real threads.
    rex::thread::Sleep(std::chrono::milliseconds(1));
  }

  REXKRNL_TRACE("XMPGetStatus({:08X})", state_ptr);
  State state;
  {
    std::lock_guard<std::mutex> lock(playback_state_mutex_);
    state = state_;
  }
  memory::store_and_swap<uint32_t>(memory_->TranslateVirtual(state_ptr),
                                   static_cast<uint32_t>(state));
  return X_E_SUCCESS;
}

X_HRESULT XmpApp::XMPCreateTitlePlaylist(uint32_t songs_ptr, uint32_t song_count,
                                         uint32_t playlist_name_ptr,
                                         const std::u16string& playlist_name, uint32_t flags,
                                         uint32_t out_song_handles, uint32_t out_playlist_handle) {
  REXKRNL_DEBUG(
      "XMPCreateTitlePlaylist({:08X}, {:08X}, {:08X}({}), {:08X}, {:08X}, "
      "{:08X})",
      songs_ptr, song_count, playlist_name_ptr, rex::string::to_utf8(playlist_name), flags,
      out_song_handles, out_playlist_handle);
  auto playlist = std::make_unique<Playlist>();
  playlist->handle = ++next_playlist_handle_;
  playlist->name = playlist_name;
  playlist->flags = flags;
  if (songs_ptr) {
    for (uint32_t i = 0; i < song_count; ++i) {
      auto song = std::make_unique<Song>();
      song->handle = ++next_song_handle_;
      uint8_t* song_base = memory_->TranslateVirtual(songs_ptr + (i * 36));
      song->file_path = memory::load_and_swap<std::u16string>(
          memory_->TranslateVirtual(memory::load_and_swap<uint32_t>(song_base + 0)));
      song->name = memory::load_and_swap<std::u16string>(
          memory_->TranslateVirtual(memory::load_and_swap<uint32_t>(song_base + 4)));
      song->artist = memory::load_and_swap<std::u16string>(
          memory_->TranslateVirtual(memory::load_and_swap<uint32_t>(song_base + 8)));
      song->album = memory::load_and_swap<std::u16string>(
          memory_->TranslateVirtual(memory::load_and_swap<uint32_t>(song_base + 12)));
      song->album_artist = memory::load_and_swap<std::u16string>(
          memory_->TranslateVirtual(memory::load_and_swap<uint32_t>(song_base + 16)));
      song->genre = memory::load_and_swap<std::u16string>(
          memory_->TranslateVirtual(memory::load_and_swap<uint32_t>(song_base + 20)));
      song->track_number = memory::load_and_swap<uint32_t>(song_base + 24);
      song->duration_ms = memory::load_and_swap<uint32_t>(song_base + 28);
      song->format = static_cast<Song::Format>(memory::load_and_swap<uint32_t>(song_base + 32));
      if (out_song_handles) {
        memory::store_and_swap<uint32_t>(memory_->TranslateVirtual(out_song_handles + (i * 4)),
                                         song->handle);
      }
      playlist->songs.emplace_back(std::move(song));
    }
  }
  if (out_playlist_handle) {
    memory::store_and_swap<uint32_t>(memory_->TranslateVirtual(out_playlist_handle),
                                     playlist->handle);
  }

  {
    auto global_lock = global_critical_region_.Acquire();
    playlists_.insert({playlist->handle, playlist.get()});
    playlist.release();
  }
  kernel_state_->BroadcastNotification(kMsgTitlePlaylistContentChanged, 0);
  return X_E_SUCCESS;
}

X_HRESULT XmpApp::XMPDeleteTitlePlaylist(uint32_t playlist_handle) {
  REXKRNL_DEBUG("XMPDeleteTitlePlaylist({:08X})", playlist_handle);
  auto global_lock = global_critical_region_.Acquire();
  auto it = playlists_.find(playlist_handle);
  if (it == playlists_.end()) {
    REXKRNL_ERROR("Playlist {:08X} not found", playlist_handle);
    return X_E_NOTFOUND;
  }
  auto playlist = it->second;
  bool is_active;
  {
    std::lock_guard<std::mutex> lock(playback_state_mutex_);
    is_active = (playlist == active_playlist_);
  }
  if (is_active) {
    XMPStop(0);
  }
  playlists_.erase(it);
  delete playlist;
  return X_E_SUCCESS;
}

X_HRESULT XmpApp::XMPPlayTitlePlaylist(uint32_t playlist_handle, uint32_t song_handle) {
  REXKRNL_DEBUG("XMPPlayTitlePlaylist({:08X}, {:08X})", playlist_handle, song_handle);
  Playlist* playlist = nullptr;
  {
    auto global_lock = global_critical_region_.Acquire();
    auto it = playlists_.find(playlist_handle);
    if (it == playlists_.end()) {
      REXKRNL_ERROR("Playlist {:08X} not found", playlist_handle);
      return X_E_NOTFOUND;
    }
    playlist = it->second;
  }

  if (playback_client_ == PlaybackClient::kSystem) {
    REXKRNL_WARN("XMPPlayTitlePlaylist: System playback is enabled!");
    return X_E_SUCCESS;
  }

  if (playlist->songs.empty()) {
    return X_E_FAIL;
  }

  // Parks the worker thread so it can't be mid-way through PlaySong()/
  // OnSongEndedNaturally() while the mutations below run - see
  // OnSongEndedNaturally()'s comment for why playback_state_mutex_ is still
  // needed on top of this.
  media_player_->Stop();

  Song* song_to_play;
  {
    std::lock_guard<std::mutex> lock(playback_state_mutex_);
    active_playlist_ = playlist;
    active_song_index_ = 0;
    if (song_handle) {
      auto it = std::find_if(playlist->songs.begin(), playlist->songs.end(),
                             [song_handle](const std::unique_ptr<Song>& song) {
                               return song->handle == song_handle;
                             });
      if (it != playlist->songs.end()) {
        active_song_index_ = static_cast<int>(std::distance(playlist->songs.begin(), it));
      }
    }
    state_ = State::kPlaying;
    song_to_play = playlist->songs[active_song_index_].get();
  }
  media_player_->Play(song_to_play);
  OnStateChanged(State::kPlaying);
  kernel_state_->BroadcastNotification(kMsgPlaybackBehaviorChanged, 1);
  return X_E_SUCCESS;
}

X_HRESULT XmpApp::XMPContinue() {
  REXKRNL_DEBUG("XMPContinue()");
  State state;
  bool was_paused;
  {
    std::lock_guard<std::mutex> lock(playback_state_mutex_);
    was_paused = (state_ == State::kPaused);
    if (was_paused) {
      state_ = State::kPlaying;
    }
    state = state_;
  }
  if (was_paused) {
    media_player_->Continue();
  }
  OnStateChanged(state);
  return X_E_SUCCESS;
}

X_HRESULT XmpApp::XMPStop(uint32_t unk) {
  assert_zero(unk);
  REXKRNL_DEBUG("XMPStop({:08X})", unk);
  media_player_->Stop();
  {
    std::lock_guard<std::mutex> lock(playback_state_mutex_);
    active_playlist_ = nullptr;  // ?
    active_song_index_ = 0;
    state_ = State::kIdle;
  }
  OnStateChanged(State::kIdle);
  return X_E_SUCCESS;
}

X_HRESULT XmpApp::XMPPause() {
  REXKRNL_DEBUG("XMPPause()");
  State state;
  bool was_playing;
  {
    std::lock_guard<std::mutex> lock(playback_state_mutex_);
    was_playing = (state_ == State::kPlaying);
    if (was_playing) {
      state_ = State::kPaused;
    }
    state = state_;
  }
  if (was_playing) {
    media_player_->Pause();
  }
  OnStateChanged(state);
  return X_E_SUCCESS;
}

X_HRESULT XmpApp::XMPNext() {
  REXKRNL_DEBUG("XMPNext()");
  {
    std::lock_guard<std::mutex> lock(playback_state_mutex_);
    if (!active_playlist_) {
      return X_E_NOTFOUND;
    }
  }
  // Parks the worker thread - see OnSongEndedNaturally()'s comment for why
  // playback_state_mutex_ is still needed around the mutation below even so.
  media_player_->Stop();
  Song* song_to_play;
  {
    std::lock_guard<std::mutex> lock(playback_state_mutex_);
    // Re-checked: a concurrent XMPStop()/XMPDeleteTitlePlaylist() on another
    // guest thread may have cleared this while we were blocked in Stop()
    // above.
    if (!active_playlist_) {
      return X_E_NOTFOUND;
    }
    state_ = State::kPlaying;
    active_song_index_ = (active_song_index_ + 1) % active_playlist_->songs.size();
    song_to_play = active_playlist_->songs[active_song_index_].get();
  }
  media_player_->Play(song_to_play);
  OnStateChanged(State::kPlaying);
  return X_E_SUCCESS;
}

X_HRESULT XmpApp::XMPPrevious() {
  REXKRNL_DEBUG("XMPPrevious()");
  {
    std::lock_guard<std::mutex> lock(playback_state_mutex_);
    if (!active_playlist_) {
      return X_E_NOTFOUND;
    }
  }
  media_player_->Stop();
  Song* song_to_play;
  {
    std::lock_guard<std::mutex> lock(playback_state_mutex_);
    if (!active_playlist_) {
      return X_E_NOTFOUND;
    }
    state_ = State::kPlaying;
    if (!active_song_index_) {
      active_song_index_ = static_cast<int>(active_playlist_->songs.size()) - 1;
    } else {
      --active_song_index_;
    }
    song_to_play = active_playlist_->songs[active_song_index_].get();
  }
  media_player_->Play(song_to_play);
  OnStateChanged(State::kPlaying);
  return X_E_SUCCESS;
}

void XmpApp::OnStateChanged(State state) {
  kernel_state_->BroadcastNotification(kMsgStateChanged, static_cast<uint32_t>(state));
}

XmpApp::Song* XmpApp::OnSongEndedNaturally() {
  Song* next_song = nullptr;
  State state;
  bool has_next = false;
  {
    std::lock_guard<std::mutex> lock(playback_state_mutex_);
    // A concurrent XMPStop()/XMPDeleteTitlePlaylist() may have already
    // cleared this out from under us by the time the worker thread got
    // here - nothing to advance.
    if (!active_playlist_ || active_playlist_->songs.empty()) {
      return nullptr;
    }

    const size_t next_index = static_cast<size_t>(active_song_index_) + 1;
    if (next_index < active_playlist_->songs.size()) {
      active_song_index_ = static_cast<int>(next_index);
      has_next = true;
    } else if (repeat_mode_ == RepeatMode::kPlaylist) {
      active_song_index_ = 0;
      has_next = true;
    } else {
      state_ = State::kIdle;
    }
    if (has_next) {
      next_song = active_playlist_->songs[active_song_index_].get();
    }
    state = state_;
  }

  OnStateChanged(state);
  return next_song;
}

X_HRESULT XmpApp::DispatchMessageSync(uint32_t message, uint32_t buffer_ptr,
                                      uint32_t buffer_length) {
  // NOTE: buffer_length may be zero or valid.
  auto buffer = memory_->TranslateVirtual(buffer_ptr);
  switch (message) {
    case 0x00070002: {
      assert_true(!buffer_length || buffer_length == 12);
      uint32_t xmp_client = memory::load_and_swap<uint32_t>(buffer + 0);
      uint32_t storage_ptr = memory::load_and_swap<uint32_t>(buffer + 4);
      uint32_t song_handle = memory::load_and_swap<uint32_t>(buffer + 8);  // 0?
      uint32_t playlist_handle =
          memory::load_and_swap<uint32_t>(memory_->TranslateVirtual(storage_ptr));
      assert_true(xmp_client == 0x00000002);
      return XMPPlayTitlePlaylist(playlist_handle, song_handle);
    }
    case 0x00070003: {
      assert_true(!buffer_length || buffer_length == 4);
      uint32_t xmp_client = memory::load_and_swap<uint32_t>(buffer + 0);
      assert_true(xmp_client == 0x00000002);
      return XMPContinue();
    }
    case 0x00070004: {
      assert_true(!buffer_length || buffer_length == 8);
      uint32_t xmp_client = memory::load_and_swap<uint32_t>(buffer + 0);
      uint32_t unk = memory::load_and_swap<uint32_t>(buffer + 4);
      assert_true(xmp_client == 0x00000002);
      return XMPStop(unk);
    }
    case 0x00070005: {
      assert_true(!buffer_length || buffer_length == 4);
      uint32_t xmp_client = memory::load_and_swap<uint32_t>(buffer + 0);
      assert_true(xmp_client == 0x00000002);
      return XMPPause();
    }
    case 0x00070006: {
      assert_true(!buffer_length || buffer_length == 4);
      uint32_t xmp_client = memory::load_and_swap<uint32_t>(buffer + 0);
      assert_true(xmp_client == 0x00000002);
      return XMPNext();
    }
    case 0x00070007: {
      assert_true(!buffer_length || buffer_length == 4);
      uint32_t xmp_client = memory::load_and_swap<uint32_t>(buffer + 0);
      assert_true(xmp_client == 0x00000002);
      return XMPPrevious();
    }
    case 0x00070008: {
      assert_true(!buffer_length || buffer_length == 16);
      struct {
        rex::be<uint32_t> xmp_client;
        rex::be<uint32_t> playback_mode;
        rex::be<uint32_t> repeat_mode;
        rex::be<uint32_t> flags;
      }* args = memory_->TranslateVirtual<decltype(args)>(buffer_ptr);
      static_assert_size(decltype(*args), 16);

      assert_true(args->xmp_client == 0x00000002 || args->xmp_client == 0x00000000);
      REXKRNL_DEBUG("XMPSetPlaybackBehavior({:08X}, {:08X}, {:08X})", uint32_t(args->playback_mode),
                    uint32_t(args->repeat_mode), uint32_t(args->flags));
      playback_mode_ = static_cast<PlaybackMode>(uint32_t(args->playback_mode));
      {
        // repeat_mode_ is read unsynchronized off the worker thread inside
        // OnSongEndedNaturally() - see playback_state_mutex_'s comment.
        std::lock_guard<std::mutex> lock(playback_state_mutex_);
        repeat_mode_ = static_cast<RepeatMode>(uint32_t(args->repeat_mode));
      }
      unknown_flags_ = args->flags;
      kernel_state_->BroadcastNotification(kMsgPlaybackBehaviorChanged, 0);
      return X_E_SUCCESS;
    }
    case 0x00070009: {
      assert_true(!buffer_length || buffer_length == 8);
      uint32_t xmp_client = memory::load_and_swap<uint32_t>(buffer + 0);
      uint32_t state_ptr = memory::load_and_swap<uint32_t>(buffer + 4);  // out ptr to 4b - expect 0
      assert_true(xmp_client == 0x00000002);
      return XMPGetStatus(state_ptr);
    }
    case 0x0007000B: {
      assert_true(!buffer_length || buffer_length == 8);
      struct {
        rex::be<uint32_t> xmp_client;
        rex::be<uint32_t> volume_ptr;
      }* args = memory_->TranslateVirtual<decltype(args)>(buffer_ptr);
      static_assert_size(decltype(*args), 8);

      assert_true(args->xmp_client == 0x00000002);
      REXKRNL_DEBUG("XMPGetVolume({:08X})", uint32_t(args->volume_ptr));
      // Read back from media_player_ rather than the local volume_ - it may
      // have substituted a real default for an explicit 0.0 (see
      // AudioMediaPlayer::PlaySong), and this keeps a single source of truth
      // instead of a game seeing a stale 0 while audio actually plays.
      volume_ = media_player_->GetVolume();
      memory::store_and_swap<float>(memory_->TranslateVirtual(args->volume_ptr), volume_);
      return X_E_SUCCESS;
    }
    case 0x0007000C: {
      assert_true(!buffer_length || buffer_length == 8);
      struct {
        rex::be<uint32_t> xmp_client;
        rex::be<float> value;
      }* args = memory_->TranslateVirtual<decltype(args)>(buffer_ptr);
      static_assert_size(decltype(*args), 8);

      assert_true(args->xmp_client == 0x00000002);
      REXKRNL_DEBUG("XMPSetVolume({:g})", float(args->value));
      volume_ = args->value;
      media_player_->SetVolume(volume_);
      return X_E_SUCCESS;
    }
    case 0x0007000D: {
      assert_true(!buffer_length || buffer_length == 36);
      struct {
        rex::be<uint32_t> xmp_client;
        rex::be<uint32_t> storage_ptr;
        rex::be<uint32_t> storage_size;
        rex::be<uint32_t> songs_ptr;
        rex::be<uint32_t> song_count;
        rex::be<uint32_t> playlist_name_ptr;
        rex::be<uint32_t> flags;
        rex::be<uint32_t> song_handles_ptr;
        rex::be<uint32_t> playlist_handle_ptr;
      }* args = memory_->TranslateVirtual<decltype(args)>(buffer_ptr);
      static_assert_size(decltype(*args), 36);

      memory::store_and_swap<uint32_t>(memory_->TranslateVirtual(args->playlist_handle_ptr),
                                       args->storage_ptr);
      assert_true(args->xmp_client == 0x00000002 || args->xmp_client == 0x00000000);
      std::u16string playlist_name;
      if (!args->playlist_name_ptr) {
        playlist_name = u"";
      } else {
        playlist_name = memory::load_and_swap<std::u16string>(
            memory_->TranslateVirtual(args->playlist_name_ptr));
      }
      // dummy_alloc_ptr is the result of a XamAlloc of storage_size.
      assert_true(uint32_t(args->storage_size) == 4 + uint32_t(args->song_count) * 128);
      return XMPCreateTitlePlaylist(args->songs_ptr, args->song_count, args->playlist_name_ptr,
                                    playlist_name, args->flags, args->song_handles_ptr,
                                    args->storage_ptr);
    }
    case 0x0007000E: {
      assert_true(!buffer_length || buffer_length == 12);
      struct {
        rex::be<uint32_t> xmp_client;
        rex::be<uint32_t> unk_ptr;  // 0
        rex::be<uint32_t> info_ptr;
      }* args = memory_->TranslateVirtual<decltype(args)>(buffer_ptr);
      static_assert_size(decltype(*args), 12);

      // Matches xenia's XMP_SONGINFO layout exactly (handle, 0x23C padding,
      // then five 40-char16 string fields, then three trailing uint32s).
      constexpr uint32_t kMaxSongMetadataStringLength = 40;
      struct SongInfo {
        rex::be<X_HANDLE> handle;
        uint8_t unknown[0x23C];
        rex::be<char16_t> title[kMaxSongMetadataStringLength];
        rex::be<char16_t> artist[kMaxSongMetadataStringLength];
        rex::be<char16_t> album[kMaxSongMetadataStringLength];
        rex::be<char16_t> album_artist[kMaxSongMetadataStringLength];
        rex::be<char16_t> genre[kMaxSongMetadataStringLength];
        rex::be<uint32_t> track_number;
        rex::be<uint32_t> duration;
        rex::be<uint32_t> song_format;
        rex::be<uint32_t> unknown_1;
      };
      static_assert_size(SongInfo, 0x3E0);

      auto info = memory_->TranslateVirtual<SongInfo*>(args->info_ptr);
      assert_true(args->xmp_client == 0x00000002);
      assert_zero(args->unk_ptr);
      REXKRNL_DEBUG("XMPGetCurrentSong({:08X}, {:08X})", uint32_t(args->unk_ptr),
                    uint32_t(args->info_ptr));
      // active_playlist_ alone isn't enough: OnSongEndedNaturally() leaves
      // it set (only marks state_ kIdle) when the playlist finishes without
      // repeating, so without this check a finished playlist would keep
      // reporting its last song as "current" forever. state_ == kPaused
      // still reports correctly, matching xenia (Pause() doesn't clear
      // active_song_ there either).
      Song* song;
      {
        std::lock_guard<std::mutex> lock(playback_state_mutex_);
        if (!active_playlist_ || state_ == State::kIdle) {
          return X_E_FAIL;
        }
        song = active_playlist_->songs[active_song_index_].get();
      }
      std::memset(info, 0, sizeof(SongInfo));
      info->handle = song->handle;
      memory::store_and_swap<std::u16string>(info->title, song->name);
      memory::store_and_swap<std::u16string>(info->artist, song->artist);
      memory::store_and_swap<std::u16string>(info->album, song->album);
      memory::store_and_swap<std::u16string>(info->album_artist, song->album_artist);
      memory::store_and_swap<std::u16string>(info->genre, song->genre);
      info->track_number = song->track_number;
      info->duration = song->duration_ms;
      info->song_format = static_cast<uint32_t>(song->format);
      return X_E_SUCCESS;
    }
    case 0x00070013: {
      assert_true(!buffer_length || buffer_length == 8);
      struct {
        rex::be<uint32_t> xmp_client;
        rex::be<uint32_t> storage_ptr;
      }* args = memory_->TranslateVirtual<decltype(args)>(buffer_ptr);
      static_assert_size(decltype(*args), 8);

      uint32_t playlist_handle =
          memory::load_and_swap<uint32_t>(memory_->TranslateVirtual(args->storage_ptr));
      assert_true(args->xmp_client == 0x00000002 || args->xmp_client == 0x00000000);
      return XMPDeleteTitlePlaylist(playlist_handle);
    }
    case 0x0007001A: {
      // XMPSetPlaybackController
      assert_true(!buffer_length || buffer_length == 12);
      struct {
        rex::be<uint32_t> xmp_client;
        rex::be<uint32_t> controller;
        rex::be<uint32_t> controller_locked;
      }* args = memory_->TranslateVirtual<decltype(args)>(buffer_ptr);
      static_assert_size(decltype(*args), 12);

      assert_true((args->xmp_client == 0x00000002 && args->controller == 0x00000000) ||
                  (args->xmp_client == 0x00000000 && args->controller == 0x00000001));
      REXKRNL_DEBUG("XMPSetPlaybackController({:08X}, {:08X})", uint32_t(args->controller),
                    uint32_t(args->controller_locked));

      // xmp_client==Game(2) && controller==Game(0) means the title is
      // claiming playback control (what a game does before playing its own
      // title playlist); xmp_client==Dash(0) && controller==User(1) means
      // the system dashboard is - those are the only two combinations the
      // assert above allows. This directly gates XMPPlayTitlePlaylist below
      // (it silently no-ops while playback_client_ == kSystem), so reading
      // the wrong field here (as this used to, by misreading the third field
      // - actually a "locked" bool, not a client id - as if it were one)
      // silently blocked all title-driven playback whenever a title claimed
      // control this way, which XMP custom-soundtrack titles routinely do.
      playback_client_ =
          args->xmp_client == 0x00000002 ? PlaybackClient::kTitle : PlaybackClient::kSystem;
      kernel_state_->BroadcastNotification(kMsgPlaybackControllerChanged,
                                           playback_client_ == PlaybackClient::kTitle);
      return X_E_SUCCESS;
    }
    case 0x0007001B: {
      // XMPGetPlaybackController
      assert_true(!buffer_length || buffer_length == 12);
      struct {
        rex::be<uint32_t> xmp_client;
        rex::be<uint32_t> controller_ptr;
        rex::be<uint32_t> locked_ptr;
      }* args = memory_->TranslateVirtual<decltype(args)>(buffer_ptr);
      static_assert_size(decltype(*args), 12);

      assert_true(args->xmp_client == 0x00000002);
      REXKRNL_DEBUG("XMPGetPlaybackController({:08X}, {:08X}, {:08X})", uint32_t(args->xmp_client),
                    uint32_t(args->controller_ptr), uint32_t(args->locked_ptr));
      // Report the real controller (Game=0/Dash=2) matching playback_client_
      // instead of always claiming Game; "locked" isn't tracked separately
      // from playback_client_ here, so it's always reported unlocked.
      const uint32_t controller_value = playback_client_ == PlaybackClient::kTitle ? 0 : 2;
      memory::store_and_swap<uint32_t>(memory_->TranslateVirtual(args->controller_ptr),
                                       controller_value);
      memory::store_and_swap<uint32_t>(memory_->TranslateVirtual(args->locked_ptr), 0);

      if (!XThread::GetCurrentThread()->main_thread()) {
        // Atrain spawns a thread 82437FD0 to call this in a tight loop forever.
        rex::thread::Sleep(std::chrono::milliseconds(10));
      }

      return X_E_SUCCESS;
    }
    case 0x00070025: {
      // XMPCreateUserPlaylistEnumerator
      // For whatever reason buffer_length is 0 in this case. Xenia doesn't
      // implement a real enumerator here either - just acknowledges the call.
      struct {
        rex::be<uint32_t> xmp_client;
        rex::be<uint32_t> flags;
        rex::be<uint32_t> object_ptr;
      }* args = memory_->TranslateVirtual<decltype(args)>(buffer_ptr);

      REXKRNL_DEBUG("XMPCreateUserPlaylistEnumerator({:08X}, {:08X}, {:08X})",
                    uint32_t(args->xmp_client), uint32_t(args->flags), uint32_t(args->object_ptr));
      return X_E_SUCCESS;
    }
    case 0x00070029: {
      // XMPGetPlaybackBehavior
      assert_true(!buffer_length || buffer_length == 16);
      struct {
        rex::be<uint32_t> xmp_client;
        rex::be<uint32_t> playback_mode_ptr;
        rex::be<uint32_t> repeat_mode_ptr;
        rex::be<uint32_t> unk3_ptr;
      }* args = memory_->TranslateVirtual<decltype(args)>(buffer_ptr);
      static_assert_size(decltype(*args), 16);

      assert_true(args->xmp_client == 0x00000002 || args->xmp_client == 0x00000000);
      REXKRNL_DEBUG("XMPGetPlaybackBehavior({:08X}, {:08X}, {:08X})",
                    uint32_t(args->playback_mode_ptr), uint32_t(args->repeat_mode_ptr),
                    uint32_t(args->unk3_ptr));
      if (args->playback_mode_ptr) {
        memory::store_and_swap<uint32_t>(memory_->TranslateVirtual(args->playback_mode_ptr),
                                         static_cast<uint32_t>(playback_mode_));
      }
      if (args->repeat_mode_ptr) {
        memory::store_and_swap<uint32_t>(memory_->TranslateVirtual(args->repeat_mode_ptr),
                                         static_cast<uint32_t>(repeat_mode_));
      }
      if (args->unk3_ptr) {
        memory::store_and_swap<uint32_t>(memory_->TranslateVirtual(args->unk3_ptr), unknown_flags_);
      }
      return X_E_SUCCESS;
    }
    case 0x0007002B: {
      // XMPGetMediaSources - called on the NXE and Kinect dashboard after
      // clicking on the picture, video, and music library.
      constexpr uint32_t kMaxSourcesForMediaPlayer = 10;
      assert_true(!buffer_length || buffer_length == 20);
      struct {
        rex::be<uint32_t> xmp_client;
        rex::be<uint32_t> get_connected_sources_only;
        rex::be<uint32_t> media_resources_ptr;
        rex::be<uint32_t> max_source;
        rex::be<uint32_t> sources_returned_ptr;
      }* args = memory_->TranslateVirtual<decltype(args)>(buffer_ptr);
      static_assert_size(decltype(*args), 20);

      REXKRNL_DEBUG(
          "XMPGetMediaSources({:08X}, {:08X}, {:08X}, {:08X}, {:08X}), unimplemented",
          uint32_t(args->xmp_client), uint32_t(args->get_connected_sources_only),
          uint32_t(args->media_resources_ptr), uint32_t(args->max_source),
          uint32_t(args->sources_returned_ptr));

      if (!args->sources_returned_ptr) {
        return X_E_INVALIDARG;
      }
      if (!args->media_resources_ptr) {
        memory::store_and_swap<uint32_t>(memory_->TranslateVirtual(args->sources_returned_ptr),
                                         kMaxSourcesForMediaPlayer);
        return X_E_SUCCESS;
      }
      if (uint32_t(args->max_source) < kMaxSourcesForMediaPlayer) {
        return 0x80070008;
      }
      for (uint32_t i = 0; i < kMaxSourcesForMediaPlayer; ++i) {
        // Some 0xB4-byte struct, but no idea what it is (matches xenia).
        auto entry = memory_->TranslateVirtual(args->media_resources_ptr + (i * 0xB4));
        std::memset(entry, 0, 0x28);
        memory::store_and_swap<uint32_t>(entry, i);
      }
      // We're returning 0, which means there is no source of media available.
      memory::store_and_swap<uint32_t>(memory_->TranslateVirtual(args->sources_returned_ptr), 0);
      return X_E_SUCCESS;
    }
    case 0x0007002E: {
      assert_true(!buffer_length || buffer_length == 12);
      // Query of size for XamAlloc - the result of the alloc is passed to
      // 0x0007000D.
      struct {
        rex::be<uint32_t> xmp_client;
        rex::be<uint32_t> song_count;
        rex::be<uint32_t> size_ptr;
      }* args = memory_->TranslateVirtual<decltype(args)>(buffer_ptr);
      static_assert_size(decltype(*args), 12);

      assert_true(args->xmp_client == 0x00000002 || args->xmp_client == 0x00000000);
      // We don't use the storage, so just fudge the number.
      memory::store_and_swap<uint32_t>(memory_->TranslateVirtual(args->size_ptr),
                                       4 + uint32_t(args->song_count) * 128);
      return X_E_SUCCESS;
    }
    case 0x0007002F: {
      // XMPDashInit - called on the start up of all dashboard versions before Kinect.
      assert_true(!buffer_length || buffer_length == 24);
      struct {
        rex::be<uint32_t> xmp_client;
        rex::be<uint32_t> buffer_ptr;
        rex::be<uint32_t> buffer_length;
        rex::be<uint32_t> unk1;
        rex::be<uint32_t> unk2;
        rex::be<uint32_t> storage_ptr;
      }* args = memory_->TranslateVirtual<decltype(args)>(buffer_ptr);
      static_assert_size(decltype(*args), 24);

      REXKRNL_DEBUG(
          "XMPDashInit({:08X}, {:08X}, {:08X}, {:08X}, {:08X}, {:08X}), unimplemented",
          uint32_t(args->xmp_client), uint32_t(args->buffer_ptr), uint32_t(args->buffer_length),
          uint32_t(args->unk1), uint32_t(args->unk2), uint32_t(args->storage_ptr));
      kernel_state_->BroadcastNotification(kMsgDashInitChanged, 1);
      return X_E_SUCCESS;
    }
    case 0x00070031: {
      // XMPGetNumSongsInTitlePlaylist
      assert_true(!buffer_length || buffer_length == 12);
      struct {
        rex::be<uint32_t> xmp_client;
        rex::be<uint32_t> playlist_ptr;
        rex::be<uint32_t> song_count_ptr;
      }* args = memory_->TranslateVirtual<decltype(args)>(buffer_ptr);
      static_assert_size(decltype(*args), 12);

      REXKRNL_DEBUG("XMPGetNumSongsInTitlePlaylist({:08X}, {:08X}, {:08X}), unimplemented",
                    uint32_t(args->xmp_client), uint32_t(args->playlist_ptr),
                    uint32_t(args->song_count_ptr));
      if (!args->playlist_ptr || !args->song_count_ptr) {
        return X_E_INVALIDARG;
      }
      memory::store_and_swap<uint32_t>(memory_->TranslateVirtual(args->song_count_ptr), 0);
      return X_E_SUCCESS;
    }
    case 0x0007003D: {
      // XMPCaptureOutput - hooks a callback into the playback pipeline for
      // e.g. music visualization; no-op until real XMP playback exists.
      REXKRNL_DEBUG("XMPCaptureOutput(...) - stub, no playback backend to capture from");
      return X_E_SUCCESS;
    }
    case 0x00070044: {
      // XMPSetMediaSourceWorkspace - called on the start up of all dashboard
      // versions before Kinect. Xenia's own implementation is a no-op too.
      assert_true(!buffer_length || buffer_length == 16);
      struct {
        rex::be<uint32_t> xmp_client;
        rex::be<uint32_t> workspace_type;
        rex::be<uint32_t> storage_ptr;
        rex::be<uint32_t> storage_length;
      }* args = memory_->TranslateVirtual<decltype(args)>(buffer_ptr);
      static_assert_size(decltype(*args), 16);

      REXKRNL_DEBUG(
          "XMPSetMediaSourceWorkspace({:08X}, {:08X}, {:08X}, {:08X}), unimplemented",
          uint32_t(args->xmp_client), uint32_t(args->workspace_type), uint32_t(args->storage_ptr),
          uint32_t(args->storage_length));
      return X_E_SUCCESS;
    }
    case 0x00070053: {
      // XMPGetDashInitState - called on the blades dashboard after clicking on
      // the picture or video library.
      struct {
        rex::be<uint32_t> xmp_client;
        rex::be<uint32_t> dash_init_state_ptr;
      }* args = memory_->TranslateVirtual<decltype(args)>(buffer_ptr);

      REXKRNL_DEBUG("XMPGetDashInitState({:08X}, {:08X})", uint32_t(args->xmp_client),
                    uint32_t(args->dash_init_state_ptr));
      // Matches xenia: this state is never actually set anywhere, so it's
      // always 0 upstream too.
      memory::store_and_swap<uint32_t>(memory_->TranslateVirtual(args->dash_init_state_ptr), 0);
      return X_E_SUCCESS;
    }
  }
  REXKRNL_ERROR(
      "Unimplemented XMP message app={:08X}, msg={:08X}, arg1={:08X}, "
      "arg2={:08X}",
      app_id(), message, buffer_ptr, buffer_length);
  return X_E_FAIL;
}

}  // namespace apps
}  // namespace xam
}  // namespace kernel
}  // namespace rex

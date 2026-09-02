#pragma once
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

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <rex/system/kernel_state.h>
#include <rex/system/xam/app_manager.h>
#include <rex/thread/mutex.h>

namespace rex::audio {
class AudioMediaPlayer;
}  // namespace rex::audio

namespace rex {
namespace kernel {
namespace xam {
namespace apps {

// Only source of docs for a lot of these functions:
// https://github.com/oukiar/freestyledash/blob/master/Freestyle/Scenes/Media/Music/ScnMusic.cpp

class XmpApp : public system::xam::App {
 public:
  enum class State : uint32_t {
    kIdle = 0,
    kPlaying = 1,
    kPaused = 2,
  };
  enum class PlaybackClient : uint32_t {
    kSystem = 0,
    kTitle = 1,
  };
  enum class PlaybackMode : uint32_t {
    kInOrder = 0,
    kShuffle = 1,
  };
  enum class RepeatMode : uint32_t {
    kPlaylist = 0,
    kNoRepeat = 1,
  };
  struct Song {
    enum class Format : uint32_t {
      kWma = 0,
      kMp3 = 1,
    };

    uint32_t handle;
    std::u16string file_path;
    std::u16string name;
    std::u16string artist;
    std::u16string album;
    std::u16string album_artist;
    std::u16string genre;
    uint32_t track_number;
    uint32_t duration_ms;
    Format format;
  };
  struct Playlist {
    uint32_t handle;
    std::u16string name;
    uint32_t flags;
    std::vector<std::unique_ptr<Song>> songs;
  };

  explicit XmpApp(system::KernelState* kernel_state);
  ~XmpApp() override;

  X_HRESULT XMPGetStatus(uint32_t status_ptr);

  X_HRESULT XMPCreateTitlePlaylist(uint32_t songs_ptr, uint32_t song_count,
                                   uint32_t playlist_name_ptr, const std::u16string& playlist_name,
                                   uint32_t flags, uint32_t out_song_handles,
                                   uint32_t out_playlist_handle);
  X_HRESULT XMPDeleteTitlePlaylist(uint32_t playlist_handle);
  X_HRESULT XMPPlayTitlePlaylist(uint32_t playlist_handle, uint32_t song_handle);
  X_HRESULT XMPContinue();
  X_HRESULT XMPStop(uint32_t unk);
  X_HRESULT XMPPause();
  X_HRESULT XMPNext();
  X_HRESULT XMPPrevious();

  X_HRESULT DispatchMessageSync(uint32_t message, uint32_t buffer_ptr,
                                uint32_t buffer_length) override;

 private:
  static const uint32_t kMsgStateChanged = 0x0A000001;
  static const uint32_t kMsgPlaybackBehaviorChanged = 0x0A000002;
  static const uint32_t kMsgPlaybackControllerChanged = 0x0A000003;
  static const uint32_t kMsgTitlePlaylistContentChanged = 0x8A000005;
  static const uint32_t kMsgDashInitChanged = 0x8A000009;

  void OnStateChanged();

  // Called from AudioMediaPlayer's worker thread (via the callback wired up
  // in the constructor) when a song finishes decoding entirely on its own -
  // not via an explicit Stop()/Play()/Next()/Previous() request. Advances
  // active_song_index_ to the next song in active_playlist_, wraps to the
  // start when repeat_mode_ is kPlaylist, or marks state_ kIdle and returns
  // nullptr when there's nothing left to play. Matches xenia's own
  // AudioMediaPlayer::Play() worker-side auto-advance/repeat logic, which
  // (like this) doesn't consult playback_mode_ (shuffle) either - xenia
  // declares XMP_CLIENT/PlaybackMode::kShuffle but never actually branches
  // on it in Next()/Previous()/the natural-completion path, so always
  // advancing sequentially is faithful, not a gap relative to xenia.
  //
  // Safe to touch active_playlist_/active_song_index_/state_ without a lock:
  // this only ever runs while the worker thread has *not* been asked to
  // stop, and every dispatch-thread handler that mutates those same fields
  // calls media_player_->Stop() first - which blocks until the worker has
  // fully left PlaySong() (and therefore this function) - before touching
  // them. The one exception is XMPPause()/XMPContinue(), which don't park
  // the worker before writing state_; a natural end-of-playlist racing
  // exactly against a pause/resume request can lose one of the two writes,
  // which is a narrow, low-consequence race (a stale kPaused/kPlaying value,
  // never a dangling pointer) accepted rather than adding a second
  // synchronization mechanism for it.
  Song* OnSongEndedNaturally();

  State state_;
  PlaybackClient playback_client_;
  PlaybackMode playback_mode_;
  RepeatMode repeat_mode_;
  uint32_t unknown_flags_;
  float volume_;
  Playlist* active_playlist_;
  int active_song_index_;

  rex::thread::global_critical_region global_critical_region_;
  std::unordered_map<uint32_t, Playlist*> playlists_;
  uint32_t next_playlist_handle_;
  uint32_t next_song_handle_;

  // Real playback backend (decode + mix + output) for whichever song
  // active_playlist_/active_song_index_ currently point at. See
  // audio_media_player.h for why this is a separate class rather than
  // inline in XmpApp.
  std::unique_ptr<rex::audio::AudioMediaPlayer> media_player_;
};

}  // namespace apps
}  // namespace xam
}  // namespace kernel
}  // namespace rex

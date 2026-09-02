/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#pragma once

#include <atomic>

#include <rex/kernel.h>
#include <rex/memory.h>
#include <rex/system/interfaces/audio.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/xthread.h>
#include <rex/thread.h>
#include <rex/thread/mutex.h>

namespace rex::stream {
class ByteStream;
}  // namespace rex::stream

namespace rex::audio {

constexpr memory::fourcc_t kAudioSaveSignature = memory::make_fourcc("XAUD");

class AudioDriver;
class XmaDecoder;

class AudioSystem : public system::IAudioSystem {
 public:
  virtual ~AudioSystem();

  memory::Memory* memory() const { return memory_; }
  runtime::FunctionDispatcher* function_dispatcher() const { return function_dispatcher_; }
  XmaDecoder* xma_decoder() const { return xma_decoder_.get(); }

  virtual X_STATUS Setup(system::KernelState* kernel_state);
  virtual void Shutdown();

  X_STATUS RegisterClient(uint32_t callback, uint32_t callback_arg, size_t* out_index);
  void UnregisterClient(size_t index);
  void SubmitFrame(size_t index, uint32_t samples_ptr);

  // Creates an independent driver instance that isn't part of the 8-slot
  // guest-callback client table used by RegisterClient/SubmitFrame(index,...)
  // (which XAudioRegisterRenderDriverClient's guest-driven pump loop owns).
  // For a host-driven playback source with its own worker thread - e.g. the
  // XMP audio backend - that pushes frames on its own schedule rather than
  // being pumped via a guest callback.
  X_STATUS CreateStandaloneDriver(rex::thread::Semaphore* semaphore, AudioDriver** out_driver);
  void DestroyStandaloneDriver(AudioDriver* driver);

  bool Save(stream::ByteStream* stream);
  bool Restore(stream::ByteStream* stream);

  bool is_paused() const { return paused_; }
  void Pause();
  void Resume();

 protected:
  explicit AudioSystem(runtime::FunctionDispatcher* function_dispatcher);

  virtual void Initialize();

  void WorkerThreadMain();

  virtual X_STATUS CreateDriver(size_t index, rex::thread::Semaphore* semaphore,
                                AudioDriver** out_driver) = 0;
  virtual void DestroyDriver(AudioDriver* driver) = 0;

  static constexpr size_t kMaximumQueuedFrames = 64;
  // Matches the real Xenos audio subsystem's ~5.333ms XMA output interval
  // (see xboxkrnl_audio_xma.cpp) - WorkerThreadMain paces each client's
  // pump to this interval (scaled by guest_time_scalar) rather than pumping
  // as fast as its semaphore allows. Ported from xenia's "[APU] Pace audio
  // subsystem" (6e5b8324f).
  static constexpr uint32_t kAudioPumpInterval = 5333u;
  // How far ahead of a client's deadline to wake and re-check, in
  // microseconds - absorbs scheduling jitter in the final wait.
  static constexpr uint32_t kAudioIntervalSlack = 400u;

  memory::Memory* memory_ = nullptr;
  runtime::FunctionDispatcher* function_dispatcher_ = nullptr;
  std::unique_ptr<XmaDecoder> xma_decoder_;

  std::atomic<bool> worker_running_ = {false};
  system::object_ref<system::XHostThread> worker_thread_;

  rex::thread::global_critical_region global_critical_region_;
  static const size_t kMaximumClientCount = 8;
  struct {
    AudioDriver* driver;
    // Microsecond timestamp (steady_clock epoch) this client is next due to
    // be pumped; see kAudioPumpInterval.
    uint64_t next_pump_us;
    uint32_t callback;
    uint32_t callback_arg;
    uint32_t wrapped_callback_arg;
    bool in_use;
  } clients_[kMaximumClientCount];

  int FindFreeClient();

  std::unique_ptr<rex::thread::Semaphore> client_semaphores_[kMaximumClientCount];
  // Signaled whenever the set of registered clients changes (so the worker
  // re-scans for a new pacing deadline immediately) or on shutdown/pause.
  // Always there in case we have no clients yet.
  std::unique_ptr<rex::thread::Event> pending_work_event_;

  bool paused_ = false;
  rex::thread::Fence pause_fence_;
  std::unique_ptr<rex::thread::Event> resume_event_;
};

}  // namespace rex::audio

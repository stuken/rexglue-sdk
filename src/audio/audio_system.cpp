/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <limits>

#include <rex/assert.h>
#include <rex/audio/audio_driver.h>
#include <rex/audio/audio_system.h>
#include <rex/audio/flags.h>
#include <rex/audio/xma/decoder.h>
#include <rex/chrono/clock.h>
#include <rex/dbg.h>
#include <rex/logging.h>
#include <rex/math.h>
#include <rex/memory/ring_buffer.h>
#include <rex/stream.h>
#include <rex/string/buffer.h>
#include <rex/system/thread_state.h>
#include <rex/thread.h>
#include <rex/cvar.h>

// As with normal Microsoft, there are like twelve different ways to access
// the audio APIs. Early games use XMA*() methods almost exclusively to touch
// decoders. Later games use XAudio*() and direct memory writes to the XMA
// structures (as opposed to the XMA* calls), meaning that we have to support
// both.
//
// For ease of implementation, most audio related processing is handled in
// AudioSystem, and the functions here call off to it.
// The XMA*() functions just manipulate the audio system in the guest context
// and let the normal AudioSystem handling take it, to prevent duplicate
// implementations. They can be found in xboxkrnl_audio_xma.cc

namespace rex::audio {

namespace {
constexpr std::chrono::milliseconds kWorkerShutdownTimeout{500};
}  // namespace

AudioSystem::AudioSystem(runtime::FunctionDispatcher* function_dispatcher)
    : memory_(function_dispatcher->memory()),
      function_dispatcher_(function_dispatcher),
      worker_running_(false) {
  std::memset(clients_, 0, sizeof(clients_));

  for (size_t i = 0; i < kMaximumClientCount; ++i) {
    client_semaphores_[i] = rex::thread::Semaphore::Create(0, kMaximumQueuedFrames);
    assert_not_null(client_semaphores_[i]);
  }
  pending_work_event_ = rex::thread::Event::CreateAutoResetEvent(false);
  assert_not_null(pending_work_event_);

  xma_decoder_ = std::make_unique<rex::audio::XmaDecoder>(function_dispatcher_);

  resume_event_ = rex::thread::Event::CreateAutoResetEvent(false);
  assert_not_null(resume_event_);
}

AudioSystem::~AudioSystem() {
  if (xma_decoder_) {
    xma_decoder_->Shutdown();
  }
}

X_STATUS AudioSystem::Setup(system::KernelState* kernel_state) {
  X_STATUS result = xma_decoder_->Setup(kernel_state);
  if (result) {
    return result;
  }

  worker_running_ = true;
  worker_thread_ = system::object_ref<system::XHostThread>(
      new system::XHostThread(kernel_state, 128 * 1024, 0, [this]() {
        WorkerThreadMain();
        return 0;
      }));

  worker_thread_->set_name("Audio Worker");
  worker_thread_->Create();
  // Bump the underlying host thread's OS priority (not XThread::SetPriority,
  // which writes the *guest* X_KTHREAD - this worker has no guest context)
  // for better pacing accuracy.
  if (rex::thread::Thread* host_thread = worker_thread_->thread()) {
    host_thread->set_priority(rex::thread::ThreadPriority::kAboveNormal);
  }

  return X_STATUS_SUCCESS;
}

void AudioSystem::WorkerThreadMain() {
  // Initialize driver and ringbuffer.
  Initialize();

  // The host mixer releases a client's semaphore on its own coarse cadence,
  // but the Xenos audio subsystem operates at kAudioPumpInterval (matching
  // real hardware's XMA output timing), scaled inversely by
  // guest_time_scalar for fast-forward/slow-motion. Rather than pumping a
  // client as fast as its semaphore allows (bursty, and faster than the
  // guest expects), this paces pumps to each client's next_pump_us deadline
  // and uses the semaphore only as back-pressure: a callback only fires if
  // a host output slot is actually free, otherwise this pump is skipped
  // (the host queue is already well buffered, so nothing is lost). Ported
  // from xenia's "[APU] Pace audio subsystem" (6e5b8324f).
  uint32_t diag_pump_count = 0;
  while (worker_running_) {
    const uint64_t now = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());

    size_t client_index = kMaximumClientCount;
    uint64_t earliest_pump_us = std::numeric_limits<uint64_t>::max();
    uint32_t client_callback = 0;
    uint32_t client_callback_arg = 0;
    {
      auto global_lock = global_critical_region_.Acquire();

      for (size_t i = 0; i < kMaximumClientCount; ++i) {
        if (!clients_[i].in_use || clients_[i].next_pump_us >= earliest_pump_us) {
          continue;
        }
        earliest_pump_us = clients_[i].next_pump_us;
        client_index = i;
      }

      if (client_index != kMaximumClientCount) {
        client_callback = clients_[client_index].callback;
        client_callback_arg = clients_[client_index].wrapped_callback_arg;

        const double scalar = rex::chrono::Clock::guest_time_scalar();
        const uint64_t min_us = scalar > 0.0
                                    ? static_cast<uint64_t>(kAudioPumpInterval / scalar)
                                    : kAudioPumpInterval;
        clients_[client_index].next_pump_us =
            (earliest_pump_us > now ? earliest_pump_us : now) + min_us;
      }
    }

    // No clients registered yet: park until one registers, or we're told to
    // stop/pause.
    if (client_index == kMaximumClientCount) {
      rex::thread::Wait(pending_work_event_.get(), true);
      if (paused_) {
        pause_fence_.Signal();
        rex::thread::Wait(resume_event_.get(), false);
      }
      continue;
    }

    // Pace to kAudioIntervalSlack ahead of the deadline.
    const uint64_t wake_target_us =
        earliest_pump_us > kAudioIntervalSlack ? earliest_pump_us - kAudioIntervalSlack : 0;
    if (wake_target_us > now) {
      const std::chrono::milliseconds timeout((wake_target_us - now) / 1000);
      auto result = rex::thread::Wait(pending_work_event_.get(), true, timeout);
      if (result == rex::thread::WaitResult::kSuccess) {
        // A client was (un)registered, or shutdown/pause was signaled,
        // before this deadline - re-scan immediately rather than pumping
        // stale state.
        if (paused_) {
          pause_fence_.Signal();
          rex::thread::Wait(resume_event_.get(), false);
        }
        continue;
      }

      const uint64_t now_precise = static_cast<uint64_t>(
          std::chrono::duration_cast<std::chrono::microseconds>(
              std::chrono::steady_clock::now().time_since_epoch())
              .count());
      if (wake_target_us > now_precise) {
        rex::thread::Sleep(std::chrono::microseconds(wake_target_us - now_precise));
      }
    }

    // Submit only if the host has a free output slot; otherwise this pump
    // is skipped.
    if (client_callback &&
        rex::thread::Wait(client_semaphores_[client_index].get(), false,
                          std::chrono::milliseconds(0)) == rex::thread::WaitResult::kSuccess) {
      if (diag_pump_count < 10) {
        REXAPU_DEBUG("AudioWorker: dispatching callback {:08X} with arg {:08X} for client {}",
                     client_callback, client_callback_arg, client_index);
      }
      SCOPE_profile_cpu_i("apu", "rex::audio::AudioSystem->client_callback");
      uint64_t args[] = {client_callback_arg};
      function_dispatcher_->Execute(worker_thread_->thread_state(), client_callback, args,
                                    rex::countof(args));
      if (diag_pump_count < 10) {
        REXAPU_DEBUG("AudioWorker: callback returned for client {}", client_index);
        diag_pump_count++;
      }
    }
  }
  worker_running_ = false;

  // TODO(benvanik): call module API to kill?
}

int AudioSystem::FindFreeClient() {
  for (size_t i = 0; i < kMaximumClientCount; i++) {
    auto& client = clients_[i];
    if (!client.in_use) {
      return i;
    }
  }

  return -1;
}

void AudioSystem::Initialize() {}

void AudioSystem::Shutdown() {
  if (!worker_running_) {
    return;
  }

  // Shut down XMA decoder first - its worker can stall in FFmpeg
  if (xma_decoder_) {
    xma_decoder_->Shutdown();
  }

  worker_running_ = false;
  pending_work_event_->Set();
  if (worker_thread_) {
    // The worker may be stuck inside a guest callback that is itself blocked on
    // guest objects (e.g. KeWaitForMultipleObjects), so terminating is the last
    // resort. Give it a chance to unwind first: TerminateThread abandons any
    // lock the thread holds, including the CRT heap lock.
    rex::thread::Thread* host_thread = worker_thread_->thread();
    bool exited = host_thread && rex::thread::Wait(host_thread, false, kWorkerShutdownTimeout) ==
                                     rex::thread::WaitResult::kSuccess;
    if (!exited) {
      REXAPU_WARN("Audio worker did not exit within {} ms; terminating",
                  kWorkerShutdownTimeout.count());
      worker_thread_->Terminate(0);
    }
    worker_thread_.reset();
  }

  // Destroy all active client drivers (closes SDL audio devices, stopping
  // callback threads) before the semaphores they reference are destroyed.
  for (size_t i = 0; i < kMaximumClientCount; i++) {
    if (clients_[i].in_use) {
      DestroyDriver(clients_[i].driver);
      if (clients_[i].wrapped_callback_arg) {
        memory()->SystemHeapFree(clients_[i].wrapped_callback_arg);
      }
      clients_[i] = {};
    }
  }
}

X_STATUS AudioSystem::RegisterClient(uint32_t callback, uint32_t callback_arg, size_t* out_index) {
  REXAPU_DEBUG("AudioSystem::RegisterClient: callback={:08X} callback_arg={:08X}", callback,
               callback_arg);
  auto global_lock = global_critical_region_.Acquire();

  auto index = FindFreeClient();
  assert_true(index >= 0);
  REXAPU_DEBUG("AudioSystem::RegisterClient: using client index={} queued_frames={}", index,
               kMaximumQueuedFrames);

  auto client_semaphore = client_semaphores_[index].get();
  auto ret = client_semaphore->Release(kMaximumQueuedFrames, nullptr);
  assert_true(ret);

  AudioDriver* driver;
  auto result = CreateDriver(index, client_semaphore, &driver);
  if (XFAILED(result)) {
    return result;
  }
  assert_not_null(driver);

  uint32_t ptr = memory()->SystemHeapAlloc(0x4);
  memory::store_and_swap<uint32_t>(memory()->TranslateVirtual(ptr), callback_arg);

  clients_[index] = {};
  clients_[index].driver = driver;
  clients_[index].callback = callback;
  clients_[index].callback_arg = callback_arg;
  clients_[index].wrapped_callback_arg = ptr;
  clients_[index].in_use = true;

  // Wake the worker so it re-scans and starts pacing this client immediately.
  pending_work_event_->Set();

  if (out_index) {
    *out_index = index;
  }

  return X_STATUS_SUCCESS;
}

void AudioSystem::SubmitFrame(size_t index, uint32_t samples_ptr) {
  SCOPE_profile_cpu_f("apu");

  static uint32_t submit_count = 0;
  if (submit_count < 10) {
    REXAPU_DEBUG("AudioSystem::SubmitFrame called: index={} samples_ptr={:08X}", index,
                 samples_ptr);
    submit_count++;
  }

  auto global_lock = global_critical_region_.Acquire();
  assert_true(index < kMaximumClientCount);
  assert_true(clients_[index].driver != NULL);
  (clients_[index].driver)->SubmitFrame(samples_ptr);
}

X_STATUS AudioSystem::CreateStandaloneDriver(rex::thread::Semaphore* semaphore,
                                             AudioDriver** out_driver) {
  // `index` is only meaningful to RegisterClient's own clients_[] slot
  // bookkeeping; SDLAudioSystem::CreateDriver (the only real implementation)
  // ignores it, so kMaximumClientCount is a safe sentinel meaning "no slot".
  return CreateDriver(kMaximumClientCount, semaphore, out_driver);
}

void AudioSystem::DestroyStandaloneDriver(AudioDriver* driver) {
  DestroyDriver(driver);
}

void AudioSystem::UnregisterClient(size_t index) {
  SCOPE_profile_cpu_f("apu");

  auto global_lock = global_critical_region_.Acquire();
  assert_true(index < kMaximumClientCount);
  DestroyDriver(clients_[index].driver);
  memory()->SystemHeapFree(clients_[index].wrapped_callback_arg);
  clients_[index] = {};

  // Drain the semaphore of its count.
  auto client_semaphore = client_semaphores_[index].get();
  rex::thread::WaitResult wait_result;
  do {
    wait_result = rex::thread::Wait(client_semaphore, false, std::chrono::milliseconds(0));
  } while (wait_result == rex::thread::WaitResult::kSuccess);
  assert_true(wait_result == rex::thread::WaitResult::kTimeout);
}

bool AudioSystem::Save(stream::ByteStream* stream) {
  stream->Write(kAudioSaveSignature);

  // Count the number of used clients first.
  // Any gaps should be handled gracefully.
  uint32_t used_clients = 0;
  for (size_t i = 0; i < kMaximumClientCount; i++) {
    if (clients_[i].in_use) {
      used_clients++;
    }
  }

  stream->Write(used_clients);
  for (uint32_t i = 0; i < kMaximumClientCount; i++) {
    auto& client = clients_[i];
    if (!client.in_use) {
      continue;
    }

    stream->Write(i);
    stream->Write(client.callback);
    stream->Write(client.callback_arg);
    stream->Write(client.wrapped_callback_arg);
  }

  return true;
}

bool AudioSystem::Restore(stream::ByteStream* stream) {
  if (stream->Read<uint32_t>() != kAudioSaveSignature) {
    REXAPU_ERROR("AudioSystem::Restore - Invalid magic value!");
    return false;
  }

  uint32_t num_clients = stream->Read<uint32_t>();
  for (uint32_t i = 0; i < num_clients; i++) {
    auto id = stream->Read<uint32_t>();
    assert_true(id < kMaximumClientCount);

    auto& client = clients_[id];

    // Reset the semaphore and recreate the driver ourselves.
    if (client.driver) {
      UnregisterClient(id);
    }

    client.callback = stream->Read<uint32_t>();
    client.callback_arg = stream->Read<uint32_t>();
    client.wrapped_callback_arg = stream->Read<uint32_t>();

    client.next_pump_us = 0;
    client.in_use = true;

    auto client_semaphore = client_semaphores_[id].get();
    auto ret = client_semaphore->Release(kMaximumQueuedFrames, nullptr);
    assert_true(ret);

    AudioDriver* driver = nullptr;
    auto status = CreateDriver(id, client_semaphore, &driver);
    if (XFAILED(status)) {
      REXAPU_ERROR(
          "AudioSystem::Restore - Call to CreateDriver failed with status "
          "{:08X}",
          status);
      return false;
    }

    assert_not_null(driver);
    client.driver = driver;
  }

  return true;
}

void AudioSystem::Pause() {
  if (paused_) {
    return;
  }
  paused_ = true;

  // Kind of a hack, but it works.
  pending_work_event_->Set();
  pause_fence_.Wait();

  xma_decoder_->Pause();
}

void AudioSystem::Resume() {
  if (!paused_) {
    return;
  }
  paused_ = false;

  resume_event_->Set();

  xma_decoder_->Resume();
}

}  // namespace rex::audio

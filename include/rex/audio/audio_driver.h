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

#include <rex/kernel.h>
#include <rex/memory.h>

namespace rex::audio {

class AudioDriver {
 public:
  explicit AudioDriver(memory::Memory* memory);
  virtual ~AudioDriver();

  virtual void SubmitFrame(uint32_t samples_ptr) = 0;

  // Per-driver playback controls, applied at the driver's own final-mix/
  // dequeue stage rather than by the caller pre-scaling or dropping submitted
  // samples - so a change reaches frames already sitting in the driver's
  // queue, not just ones submitted after the call. Mirrors xenia's
  // xe::apu::AudioDriver interface.
  virtual void SetVolume(float volume) = 0;
  virtual void Pause() = 0;
  virtual void Resume() = 0;

 protected:
  inline uint8_t* TranslatePhysical(uint32_t guest_address) const {
    return memory_->TranslatePhysical(guest_address);
  }

  memory::Memory* memory_ = nullptr;
};

}  // namespace rex::audio

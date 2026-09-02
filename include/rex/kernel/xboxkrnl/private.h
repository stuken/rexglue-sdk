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

#pragma once

#include <cstdint>

#include <rex/system/xtypes.h>

namespace rex::kernel::xboxkrnl {

X_STATUS xeExGetXConfigSetting(uint16_t category, uint16_t setting, void* buffer,
                               uint16_t buffer_size, uint16_t* required_size);

}  // namespace rex::kernel::xboxkrnl

/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <rex/kernel/xam/apps/messenger_app.h>
#include <rex/logging.h>

namespace rex {
namespace kernel {
namespace xam {
using namespace rex::system;
using namespace rex::system::xam;
namespace apps {
using namespace rex::system;

MessengerApp::MessengerApp(KernelState* kernel_state) : App(kernel_state, 0xF7) {}

X_HRESULT MessengerApp::DispatchMessageSync(uint32_t message, uint32_t buffer_ptr,
                                            uint32_t buffer_length) {
  // NOTE: buffer_length may be zero or valid.
  auto buffer = memory_->TranslateVirtual(buffer_ptr);
  switch (message) {
    case 0x00200002: {
      // Used on blades dashboard v5759 - 6717 when requesting to sign out.
      assert_true(!buffer_length || buffer_length == 12);
      struct message_data {
        rex::be<uint32_t> user_index;
        rex::be<uint32_t> unk_04;
        rex::be<uint32_t> unk_08;
      }* data = reinterpret_cast<message_data*>(buffer);
      static_assert_size(message_data, 12);

      REXKRNL_DEBUG("MessengerUnk200002({:08X}, {:08X}, {:08X}), unimplemented",
                    (uint32_t)data->user_index, (uint32_t)data->unk_04, (uint32_t)data->unk_08);
      return X_E_FAIL;
    }
    case 0x00200018: {
      // Used after signing out in blades 6717.
      assert_true(!buffer_length || buffer_length == 12);
      struct message_data {
        rex::be<uint32_t> user_index;
        rex::be<uint32_t> unk_04;
        rex::be<uint32_t> unk_08;
      }* data = reinterpret_cast<message_data*>(buffer);
      static_assert_size(message_data, 12);

      REXKRNL_DEBUG("MessengerUnk200018({:08X}, {:08X}, {:08X}), unimplemented",
                    (uint32_t)data->user_index, (uint32_t)data->unk_04, (uint32_t)data->unk_08);
      return X_E_FAIL;
    }
  }
  REXKRNL_ERROR(
      "Unimplemented Messenger message app={:08X}, msg={:08X}, arg1={:08X}, "
      "arg2={:08X}",
      app_id(), message, buffer_ptr, buffer_length);
  return X_E_FAIL;
}

}  // namespace apps
}  // namespace xam
}  // namespace kernel
}  // namespace rex

/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <rex/kernel/xam/apps/app.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xam/content_device.h>
#include <rex/system/xenumerator.h>
#include <rex/thread.h>

namespace rex {
namespace kernel {
namespace xam {
using namespace rex::system;
using namespace rex::system::xam;
namespace apps {
using namespace rex::system;

XamApp::XamApp(KernelState* kernel_state) : App(kernel_state, 0xFE) {}

X_HRESULT XamApp::DispatchMessageSync(uint32_t message, uint32_t buffer_ptr,
                                      uint32_t buffer_length) {
  // NOTE: buffer_length may be zero or valid.
  auto buffer = memory_->TranslateVirtual(buffer_ptr);
  switch (message) {
    case 0x0002000E: {
      struct message_data {
        rex::be<uint32_t> user_index;
        rex::be<uint32_t> unk_04;
        rex::be<uint32_t> extra_ptr;
        rex::be<uint32_t> buffer_ptr;
        rex::be<uint32_t> buffer_size;
        rex::be<uint32_t> unk_14;
        rex::be<uint32_t> length_ptr;
        rex::be<uint32_t> unk_1C;
      }* data = reinterpret_cast<message_data*>(buffer);
      REXKRNL_DEBUG(
          "XamAppEnumerateContentAggregate({}, {:08X}, {:08X}, {:08X}, {}, "
          "{:08X}, {:08X}, {:08X})",
          (uint32_t)data->user_index, (uint32_t)data->unk_04, (uint32_t)data->extra_ptr,
          (uint32_t)data->buffer_ptr, (uint32_t)data->buffer_size, (uint32_t)data->unk_14,
          (uint32_t)data->length_ptr, (uint32_t)data->unk_1C);
      auto extra = memory_->TranslateVirtual<X_KENUMERATOR_CONTENT_AGGREGATE*>(data->extra_ptr);
      auto buffer = memory_->TranslateVirtual(data->buffer_ptr);
      auto e = kernel_state_->object_table()->LookupObject<XEnumerator>(extra->handle);
      if (!e || !buffer || !extra) {
        return X_E_INVALIDARG;
      }
      assert_true(extra->magic == kXObjSignature);
      if (data->buffer_size) {
        std::memset(buffer, 0, data->buffer_size);
      }
      uint32_t item_count = 0;
      auto result = e->WriteItems(data->buffer_ptr, buffer, &item_count);

      if (data->length_ptr) {
        auto length_ptr = memory_->TranslateVirtual<be<uint32_t>*>(data->length_ptr);
        *length_ptr = item_count;
      }

      return X_HRESULT_FROM_WIN32(result);
    }
    case 0x00020021: {
      // XContentQueryVolumeDeviceType
      struct message_data {
        char root_name[64];
        rex::be<uint32_t> is_title_process;  // KeGetCurrentProcessType() < 1 ? 1 : 0
        rex::be<uint32_t> device_type_ptr;   // output
        rex::be<uint32_t> overlapped_ptr;
      }* data = reinterpret_cast<message_data*>(buffer);
      assert_true(buffer_length == sizeof(message_data));

      std::string target;
      if (!kernel_state_->file_system()->FindSymbolicLink(std::string(data->root_name) + ':',
                                                           target)) {
        return X_E_INVALIDARG;
      }

      // Only content-package volumes answer this query. ReXGlue always mounts
      // content under \Device\Content\<n>\ (see ContentPackage) and that
      // mount is always host-directory-backed, never a disc/ODD volume.
      if (!target.starts_with("\\Device\\Content\\")) {
        return X_E_INVALIDARG;
      }

      auto device_type_ptr = memory_->TranslateVirtual<rex::be<uint32_t>*>(data->device_type_ptr);
      *device_type_ptr = static_cast<uint32_t>(DeviceType::HDD);

      REXKRNL_DEBUG("XamApp(0x00020021)('{}', {:08X}, {:08X}, {:08X})", data->root_name,
                    (uint32_t)data->is_title_process, (uint32_t)data->device_type_ptr,
                    (uint32_t)data->overlapped_ptr);
      return X_E_SUCCESS;
    }
    case 0x00021012: {
      REXKRNL_DEBUG("XamApp(0x00021012)");
      return X_E_SUCCESS;
    }
    case 0x00022005: {
      // XTitleGetDeploymentType
      struct message_data {
        rex::be<uint32_t> deployment_type_ptr;  // output
        rex::be<uint32_t> overlapped_ptr;
      }* data = reinterpret_cast<message_data*>(buffer);
      assert_true(buffer_length == sizeof(message_data));

      // ReXGlue always runs a locally-compiled title directly - there's no
      // disc-streaming / background-download distinction to report, so this
      // is always "installed to HDD" (matches xenia's
      // XDeploymentType::kInstalledToHDD, value 1).
      constexpr uint32_t kXDeploymentTypeInstalledToHDD = 1;
      auto deployment_type_ptr =
          memory_->TranslateVirtual<rex::be<uint32_t>*>(data->deployment_type_ptr);
      *deployment_type_ptr = kXDeploymentTypeInstalledToHDD;

      REXKRNL_DEBUG("XamApp(0x00022005)(%.8X, %.8X)", (uint32_t)data->deployment_type_ptr,
                    (uint32_t)data->overlapped_ptr);
      return X_E_SUCCESS;
    }
  }
  REXKRNL_ERROR(
      "Unimplemented XAM message app={:08X}, msg={:08X}, arg1={:08X}, "
      "arg2={:08X}",
      app_id(), message, buffer_ptr, buffer_length);
  return X_E_FAIL;
}

}  // namespace apps
}  // namespace xam
}  // namespace kernel
}  // namespace rex

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

#include <rex/kernel/xam/private.h>
#include <rex/logging.h>
#include <rex/hook.h>
#include <rex/types.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xthread.h>
#include <rex/system/xtypes.h>

namespace rex {
namespace kernel {
namespace xam {
using namespace rex::system;

u32 XamMediaVerificationCreate_entry(u32 creation_flags) {
  return X_ERROR_SUCCESS;
}

u32 XamMediaVerificationClose_entry() {
  return X_ERROR_SUCCESS;
}

u32 XamMediaVerificationVerify_entry(u32 r3, mapped_void overlapped, mapped_void callback) {
  if (callback) {
    auto thread = XThread::GetCurrentThread();
    thread->EnqueueApc(callback.guest_address() & ~1u, 0, 0, 0);
  }
  return X_ERROR_SUCCESS;
}

struct X_SECURITY_FAILURE_INFORMATION {
  rex::be<uint32_t> size;
  rex::be<uint32_t> failed_reads;
  rex::be<uint32_t> failed_hashes;
  rex::be<uint32_t> blocks_checked;
  rex::be<uint32_t> total_blocks;
  rex::be<uint32_t> complete;
};

u32 XamMediaVerificationFailedBlocks_entry(
    ppc_ptr_t<X_SECURITY_FAILURE_INFORMATION> failure_information) {
  if (failure_information) {
    if (failure_information->size != sizeof(X_SECURITY_FAILURE_INFORMATION)) {
      return X_ERROR_NOT_ENOUGH_MEMORY;
    }

    failure_information->failed_reads = 0;
    failure_information->failed_hashes = 0;
    failure_information->blocks_checked = 0;
    failure_information->total_blocks = 0;
    failure_information->complete = 1;
  }
  return X_ERROR_SUCCESS;
}

u32 XamMediaVerificationInject_entry(u32 r3, u32 r4) {
  return X_ERROR_SUCCESS;
}

}  // namespace xam
}  // namespace kernel
}  // namespace rex

REX_EXPORT(__imp__XamMediaVerificationCreate, rex::kernel::xam::XamMediaVerificationCreate_entry)
REX_EXPORT(__imp__XamMediaVerificationClose, rex::kernel::xam::XamMediaVerificationClose_entry)
REX_EXPORT(__imp__XamMediaVerificationVerify, rex::kernel::xam::XamMediaVerificationVerify_entry)
REX_EXPORT(__imp__XamMediaVerificationFailedBlocks,
           rex::kernel::xam::XamMediaVerificationFailedBlocks_entry)
REX_EXPORT(__imp__XamMediaVerificationInject, rex::kernel::xam::XamMediaVerificationInject_entry)

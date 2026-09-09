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

#include <rex/kernel/xam/module.h>
#include <rex/kernel/xam/private.h>
#include <rex/logging.h>
#include <rex/hook.h>
#include <rex/types.h>
#include <rex/string.h>
#include <rex/system/kernel_state.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/user_module.h>
#include <rex/system/xthread.h>
#include <rex/system/xtypes.h>

#if REX_PLATFORM_WIN32
#include <rex/platform.h>
#endif

#include <fmt/format.h>

namespace rex {
namespace kernel {
namespace xam {
using namespace rex::system;
using namespace rex::system::xam;

u32 XamTaskSchedule_entry(mapped_void callback, mapped_void message,
                          ppc_ptr_t<X_TASK_ARGS> xam_task_args_ptr, mapped_u32 handle_ptr) {
  // TODO(gibbed): figure out what this is for
  *handle_ptr = 12345;

  if (xam_task_args_ptr) {
    auto v1 = xam_task_args_ptr->flags;   // 4, 8, 0x4000008, 0x402008, etc
    auto v2 = xam_task_args_ptr->value2;  // 10, 1000, 0xea60, etc

    REXKRNL_INFO("Got xam task args: v1 = {:08X}, v2 = {:08X}", v1.get(), v2.get());
  }

  uint32_t stack_size = REX_KERNEL_STATE()->GetExecutableModule()->stack_size();

  // Stack must be aligned to 16kb pages
  stack_size = std::max((uint32_t)0x4000, ((stack_size + 0xFFF) & 0xFFFFF000));

  auto thread =
      object_ref<XThread>(new XThread(REX_KERNEL_STATE(), stack_size, 0, callback.guest_address(),
                                      message.guest_address(), 0, true));

  X_STATUS result = thread->Create();

  if (XFAILED(result)) {
    // Failed!
    REXKRNL_ERROR("XAM task creation failed: {:08X}", result);
    return result;
  }

  REXKRNL_DEBUG("XAM task ({:08X}) scheduled asynchronously", callback.guest_address());

  return X_STATUS_SUCCESS;
}

u32 XamTaskShouldExit_entry(u32 r3) {
  return 0;
}

u32 XamTaskCloseHandle_entry(u32 handle) {
  REXKRNL_DEBUG("XamTaskCloseHandle({:#x}) - stub", (uint32_t)handle);
  return X_STATUS_SUCCESS;
}

}  // namespace xam
}  // namespace kernel
}  // namespace rex

REX_EXPORT(__imp__XamTaskSchedule, rex::kernel::xam::XamTaskSchedule_entry)
REX_EXPORT(__imp__XamTaskShouldExit, rex::kernel::xam::XamTaskShouldExit_entry)
REX_EXPORT(__imp__XamTaskCloseHandle, rex::kernel::xam::XamTaskCloseHandle_entry)

REX_EXPORT_STUB(__imp__XamTaskCancel);
REX_EXPORT_STUB(__imp__XamTaskCancelWaitAndCloseWaitTask);
REX_EXPORT_STUB(__imp__XamTaskCreateQueue);
REX_EXPORT_STUB(__imp__XamTaskCreateQueueEx);
REX_EXPORT_STUB(__imp__XamTaskGetAttributes);
REX_EXPORT_STUB(__imp__XamTaskGetCompletionStatus);
REX_EXPORT_STUB(__imp__XamTaskGetCurrentTask);
REX_EXPORT_STUB(__imp__XamTaskGetStatus);
REX_EXPORT_STUB(__imp__XamTaskModify);
REX_EXPORT_STUB(__imp__XamTaskQueryProperty);
REX_EXPORT_STUB(__imp__XamTaskReschedule);
REX_EXPORT_STUB(__imp__XamTaskSetCancelSubTasks);
REX_EXPORT_STUB(__imp__XamTaskWaitOnCompletion);

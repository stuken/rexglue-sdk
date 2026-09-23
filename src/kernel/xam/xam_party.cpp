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

#include <rex/kernel/xam/private.h>
#include <rex/hook.h>
#include <rex/types.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xio.h>
#include <rex/system/xtypes.h>

// Disable warnings about unused parameters for kernel functions
#pragma GCC diagnostic ignored "-Wunused-parameter"

namespace rex {
namespace kernel {
namespace xam {

// XamPartyCreate, XamPartyLeave, XamPartySendInvite, XamPartyJoinEx, and
// XamPartyRemoveLocalUsers share one function.

u32 XamPartyGetUserList_entry(u32 caller, mapped_void party_list_ptr) {
  if (caller != 1) {
    return X_E_NOT_IMPLEMENTED;
  }
  // 5345085D, 45410923
  return 0x807D0003;  // X_PARTY_E_NOT_IN_PARTY
}

// Shares the same function as XamPartyAddLocalUsers, XamPartyKickUser,
// XamPartyGetJoinable, XamPartySetJoinable, and XamPartyGetInfoEx.
u32 XamPartySendGameInvites_entry(u32 caller, u32 user_index, u32 unknown,
                                  ppc_ptr_t<system::XAM_OVERLAPPED> overlapped) {
  auto run = [](uint32_t& extended_error, uint32_t& length) -> X_RESULT {
    extended_error = X_ERROR_SUCCESS;
    length = 0;
    return X_ERROR_SUCCESS;
  };

  if (!overlapped) {
    uint32_t extended_error, length;
    return run(extended_error, length);
  }

  REX_KERNEL_STATE()->CompleteOverlappedDeferredEx(run, overlapped.guest_address());
  return X_ERROR_IO_PENDING;
}

u32 XamPartySetCustomData_entry(u32 caller, u32 user_index, mapped_void custom_data_ptr) {
  if (caller != 1) {
    return X_E_NOT_IMPLEMENTED;
  } else if (user_index >= 4) {
    return 0x807D0002;
  }
  return 0x807D0003;  // X_PARTY_E_NOT_IN_PARTY
}

u32 XamPartyGetBandwidth_entry(u32 bandwidth_type, mapped_u64 bandwidth_statistic) {
  if (bandwidth_statistic) {
    *bandwidth_statistic = 0;
  }
  return X_ERROR_SUCCESS;
}

}  // namespace xam
}  // namespace kernel
}  // namespace rex

REX_EXPORT(__imp__XamPartyGetUserList, rex::kernel::xam::XamPartyGetUserList_entry)
REX_EXPORT(__imp__XamPartySendGameInvites, rex::kernel::xam::XamPartySendGameInvites_entry)
REX_EXPORT(__imp__XamPartySetCustomData, rex::kernel::xam::XamPartySetCustomData_entry)
REX_EXPORT(__imp__XamPartyGetBandwidth, rex::kernel::xam::XamPartyGetBandwidth_entry)

REX_EXPORT_STUB(__imp__XamPartyAddLocalUsers);
REX_EXPORT_STUB(__imp__XamPartyAutomationInprocCall);
REX_EXPORT_STUB(__imp__XamPartyCreate);
REX_EXPORT_STUB(__imp__XamPartyGetAccessLevel);
REX_EXPORT_STUB(__imp__XamPartyGetFormation);
REX_EXPORT_STUB(__imp__XamPartyGetInfo);
REX_EXPORT_STUB(__imp__XamPartyGetInfoEx);
REX_EXPORT_STUB(__imp__XamPartyGetJoinable);
REX_EXPORT_STUB(__imp__XamPartyGetNetworkCounters);
REX_EXPORT_STUB(__imp__XamPartyGetRoutingTable);
REX_EXPORT_STUB(__imp__XamPartyGetState);
REX_EXPORT_STUB(__imp__XamPartyGetUserListInternal);
REX_EXPORT_STUB(__imp__XamPartyIsCoordinator);
REX_EXPORT_STUB(__imp__XamPartyJoin);
REX_EXPORT_STUB(__imp__XamPartyJoinEx);
REX_EXPORT_STUB(__imp__XamPartyKickUser);
REX_EXPORT_STUB(__imp__XamPartyLeave);
REX_EXPORT_STUB(__imp__XamPartyOverrideNatType);
REX_EXPORT_STUB(__imp__XamPartyRemoveLocalUsers);
REX_EXPORT_STUB(__imp__XamPartySendInvite);
REX_EXPORT_STUB(__imp__XamPartySendInviteDeprecated);
REX_EXPORT_STUB(__imp__XamPartySetConnectivityGraph);
REX_EXPORT_STUB(__imp__XamPartySetJoinable);
REX_EXPORT_STUB(__imp__XamPartySetTestDelay);
REX_EXPORT_STUB(__imp__XamPartySetTestFlags);

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

// Disable warnings about unused parameters for kernel functions
#pragma GCC diagnostic ignored "-Wunused-parameter"

#include <rex/kernel/xboxkrnl/private.h>
#include <rex/logging.h>
#include <rex/hook.h>
#include <rex/types.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xthread.h>
#include <rex/system/xtypes.h>

using namespace rex::system;

REXCVAR_DEFINE_BOOL(allow_mic_initialization, false, "Kernel",
                    "Enable Mic Initialization\n"
                    " Only set true when testing mic related functions");

namespace rex::kernel::xboxkrnl {

void KeEnableFpuExceptions_entry(u32 enabled) {
  // TODO(benvanik): can we do anything about exceptions?
}

enum class XMicRequestType : uint16_t {
  MicGetStatus = 0x0000,
  MicStart = 0x0001,
  MicSetGain = 0x0002,
  MicGetData = 0x0007,
  MicGetSampleRates = 0x0008,
  MicGetCapabilities = 0x0009,
  MicUnk = 0x000B,
};

enum class XMicState : uint32_t {
  MicNotConnected = 0x00000000,
  MicConnected = 0x00000001,
  MicConnected2 = 0x00000002,
  MicConnected3 = 0x00000003,
  MicRecording = 0x00000005,
};

struct X_MIC_INFO {
  rex::be<XMicRequestType> request_type;  // 0
  rex::be<uint16_t> user_index;           // 2
  rex::be<XMicState> state;               // 4

  // These fields are only for requests that use overlap. Basically anything
  // other than MicGetStatus. Seems like there is also an event object which
  // creation isn't reported via kernel call.
  rex::be<uint32_t> unk1;            // 8
  rex::be<uint32_t> event_ptr;       // 0xC
  rex::be<uint32_t> completion_ptr;  // 0x10
  rex::be<uint32_t> unk4;            // 0x14
};

struct X_MIC_CAPABILITIES {
  /* Hardcoded fields for 0x6000 type aka. MicDevice. 0x5000 type (Rmc) does not
    have any hardcoded fields. features - 0 frame_length - 0x3E8 mic_color -
    0xFF
  */
  rex::be<uint32_t> features;
  rex::be<uint16_t> format_tag;  // Always 1
  rex::be<uint16_t> channels;
  rex::be<uint32_t> sample_rates;
  rex::be<uint16_t> bits_per_sample;
  rex::be<uint16_t> frame_length;
  rex::be<uint8_t> mic_color;
  rex::be<uint16_t> vendor_id;
  rex::be<uint16_t> product_id;
  rex::be<uint16_t> revision;
  rex::be<uint32_t> device_id;
};
static_assert_size(X_MIC_CAPABILITIES, 0x1C);

struct X_MIC_DEVICE {
  X_MIC_INFO info;
  // There seems to be one huge union here that depends on the request type.
  X_MIC_CAPABILITIES capabilities;
};

u32 MicDeviceRequest_entry(ppc_ptr_t<X_MIC_DEVICE> device_ptr) {
  if (!device_ptr) {
    return X_STATUS_INVALID_PARAMETER;
  }
  REXKRNL_ERROR("MicDeviceRequest State: {:08X} Action: {:04X} USER: {:08X}",
               static_cast<uint32_t>(device_ptr->info.state.get()),
               static_cast<uint16_t>(device_ptr->info.request_type.get()),
               device_ptr->info.user_index.get());
  if (device_ptr->info.user_index > 4) {
    return X_STATUS_INVALID_PARAMETER;
  }

  if (device_ptr->info.request_type > XMicRequestType::MicUnk) {
    return X_STATUS_INVALID_DEVICE_REQUEST;
  }

  device_ptr->info.state =
      REXCVAR_GET(allow_mic_initialization) ? XMicState::MicConnected : XMicState::MicNotConnected;

  switch (device_ptr->info.request_type) {
    case XMicRequestType::MicGetStatus:
      return X_ERROR_SUCCESS;
    case XMicRequestType::MicStart:
    case XMicRequestType::MicGetData:
    case XMicRequestType::MicGetSampleRates:
      break;
    case XMicRequestType::MicGetCapabilities:
      device_ptr->capabilities.features = 0x100;
      device_ptr->capabilities.format_tag = 1;
      device_ptr->capabilities.mic_color = 0;
      break;
    default:
      break;
  }

  // This should be handled by DPC.
  if (device_ptr->info.completion_ptr) {
    // Not sure what args should be put there, but seems like negative r4 causes
    // instant bailout. For now leave it hardcoded to failure.
    uint64_t args[] = {device_ptr.guest_address(), static_cast<uint64_t>(X_STATUS_UNSUCCESSFUL)};

    REX_KERNEL_STATE()->function_dispatcher()->Execute(
        XThread::GetCurrentThread()->thread_state(), device_ptr->info.completion_ptr, args,
        rex::countof(args));
  }

  return X_STATUS_PENDING;
}

}  // namespace rex::kernel::xboxkrnl

REX_EXPORT(__imp__KeEnableFpuExceptions, rex::kernel::xboxkrnl::KeEnableFpuExceptions_entry)
REX_EXPORT(__imp__MicDeviceRequest, rex::kernel::xboxkrnl::MicDeviceRequest_entry)

REX_EXPORT_STUB(__imp__ExSetBetaFeaturesEnabled);
REX_EXPORT_STUB(__imp__ExIsBetaFeatureEnabled);
REX_EXPORT_STUB(__imp__AniBlockOnAnimation);
REX_EXPORT_STUB(__imp__AniTerminateAnimation);
REX_EXPORT_STUB(__imp__AniSetLogo);
REX_EXPORT_STUB(__imp__AniStartBootAnimation);
REX_EXPORT_STUB(__imp__EtxConsumerDisableEventType);
REX_EXPORT_STUB(__imp__EtxConsumerEnableEventType);
REX_EXPORT_STUB(__imp__EtxConsumerProcessLogs);
REX_EXPORT_STUB(__imp__EtxConsumerRegister);
REX_EXPORT_STUB(__imp__EtxConsumerUnregister);
REX_EXPORT_STUB(__imp__EtxProducerLog);
REX_EXPORT_STUB(__imp__EtxProducerLogV);
REX_EXPORT_STUB(__imp__EtxProducerRegister);
REX_EXPORT_STUB(__imp__EtxProducerUnregister);
REX_EXPORT_STUB(__imp__EtxConsumerFlushBuffers);
REX_EXPORT_STUB(__imp__EtxProducerLogXwpp);
REX_EXPORT_STUB(__imp__EtxProducerLogXwppV);
REX_EXPORT_STUB(__imp__EtxBufferRegister);
REX_EXPORT_STUB(__imp__EtxBufferUnregister);
REX_EXPORT_STUB(__imp__KeEnablePPUPerformanceMonitor);
REX_EXPORT_STUB(__imp__KeEnterUserMode);
REX_EXPORT_STUB(__imp__KeLeaveUserMode);
REX_EXPORT_STUB(__imp__KeCreateUserMode);
REX_EXPORT_STUB(__imp__KeDeleteUserMode);
REX_EXPORT_STUB(__imp__KeEnablePFMInterrupt);
REX_EXPORT_STUB(__imp__KeDisablePFMInterrupt);
REX_EXPORT_STUB(__imp__KeSetProfilerISR);
REX_EXPORT_STUB(__imp__KeGetVidInfo);
REX_EXPORT_STUB(__imp__KeExecuteOnProtectedStack);
REX_EXPORT_STUB(__imp__EmaExecute);
REX_EXPORT_STUB(__imp__ExRegisterThreadNotification);
REX_EXPORT_STUB(__imp__ExTerminateTitleProcess);
REX_EXPORT_STUB(__imp__ExFreeDebugPool);
REX_EXPORT_STUB(__imp__ExRegisterXConfigNotification);
REX_EXPORT_STUB(__imp__ExCancelAlarm);
REX_EXPORT_STUB(__imp__ExInitializeAlarm);
REX_EXPORT_STUB(__imp__ExSetAlarm);
REX_EXPORT_STUB(__imp__KeBlowFuses);
REX_EXPORT_STUB(__imp__KeGetPMWRegister);
REX_EXPORT_STUB(__imp__KeGetPRVRegister);
REX_EXPORT_STUB(__imp__KeGetSocRegister);
REX_EXPORT_STUB(__imp__KeGetSpecialPurposeRegister);
REX_EXPORT_STUB(__imp__KeSetPMWRegister);
REX_EXPORT_STUB(__imp__KeSetPowerMode);
REX_EXPORT_STUB(__imp__KeSetPRVRegister);
REX_EXPORT_STUB(__imp__KeSetSocRegister);
REX_EXPORT_STUB(__imp__KeSetSpecialPurposeRegister);
REX_EXPORT_STUB(__imp__KeCallAndBlockOnDpcRoutine);
REX_EXPORT_STUB(__imp__KeCallAndWaitForDpcRoutine);
REX_EXPORT_STUB(__imp__KeSetPageRelocationCallback);
REX_EXPORT_STUB(__imp__KeRegisterSwapNotification);

REX_EXPORT_STUB(__imp__DetroitDeviceRequest);
REX_EXPORT_STUB(__imp__IptvGetAesCtrTransform);
REX_EXPORT_STUB(__imp__IptvGetSessionKeyHash);
REX_EXPORT_STUB(__imp__IptvSetBoundaryKey);
REX_EXPORT_STUB(__imp__IptvSetSessionKey);
REX_EXPORT_STUB(__imp__IptvVerifyOmac1Signature);
REX_EXPORT_STUB(__imp__McaDeviceRequest);
REX_EXPORT_STUB(__imp__MtpdBeginTransaction);
REX_EXPORT_STUB(__imp__MtpdCancelTransaction);
REX_EXPORT_STUB(__imp__MtpdEndTransaction);
REX_EXPORT_STUB(__imp__MtpdGetCurrentDevices);
REX_EXPORT_STUB(__imp__MtpdReadData);
REX_EXPORT_STUB(__imp__MtpdReadEvent);
REX_EXPORT_STUB(__imp__MtpdResetDevice);
REX_EXPORT_STUB(__imp__MtpdSendData);
REX_EXPORT_STUB(__imp__MtpdVerifyProximity);
REX_EXPORT_STUB(__imp__NicAttach);
REX_EXPORT_STUB(__imp__NicDetach);
REX_EXPORT_STUB(__imp__NicFlushXmitQueue);
REX_EXPORT_STUB(__imp__NicGetLinkState);
REX_EXPORT_STUB(__imp__NicGetOpt);
REX_EXPORT_STUB(__imp__NicGetStats);
REX_EXPORT_STUB(__imp__NicRegisterDevice);
REX_EXPORT_STUB(__imp__NicSetOpt);
REX_EXPORT_STUB(__imp__NicSetUnicastAddress);
REX_EXPORT_STUB(__imp__NicShutdown);
REX_EXPORT_STUB(__imp__NicUnregisterDevice);
REX_EXPORT_STUB(__imp__NicUpdateMcastMembership);
REX_EXPORT_STUB(__imp__NicXmit);
REX_EXPORT_STUB(__imp__NomnilGetExtension);
REX_EXPORT_STUB(__imp__NomnilSetLed);
REX_EXPORT_STUB(__imp__NomnilStartCloseDevice);
REX_EXPORT_STUB(__imp__NullCableRequest);
REX_EXPORT_STUB(__imp__PsCamDeviceRequest);
REX_EXPORT_STUB(__imp__RmcDeviceRequest);
REX_EXPORT_STUB(__imp__TidDeviceRequest);
REX_EXPORT_STUB(__imp__TitleDeviceAuthRequest);
REX_EXPORT_STUB(__imp__UsbdAddDeviceComplete);
REX_EXPORT_STUB(__imp__UsbdCallAndBlockOnDpcRoutine);
REX_EXPORT_STUB(__imp__UsbdCancelAsyncTransfer);
REX_EXPORT_STUB(__imp__UsbdCancelTimer);
REX_EXPORT_STUB(__imp__UsbdEnableDisableRootHubPort);
REX_EXPORT_STUB(__imp__UsbdGetDeviceDescriptor);
REX_EXPORT_STUB(__imp__UsbdGetDeviceRootPortType);
REX_EXPORT_STUB(__imp__UsbdGetDeviceSpeed);
REX_EXPORT_STUB(__imp__UsbdGetDeviceTopology);
REX_EXPORT_STUB(__imp__UsbdGetEndpointDescriptor);
REX_EXPORT_STUB(__imp__UsbdGetNatalHardwareVersion);
REX_EXPORT_STUB(__imp__UsbdGetNatalHub);
REX_EXPORT_STUB(__imp__UsbdGetPortDeviceNode);
REX_EXPORT_STUB(__imp__UsbdGetRequiredDrivers);
REX_EXPORT_STUB(__imp__UsbdGetRootHubDeviceNode);
REX_EXPORT_STUB(__imp__UsbdIsDeviceAuthenticated);
REX_EXPORT_STUB(__imp__UsbdNatalHubRegisterNotificationCallback);
REX_EXPORT_STUB(__imp__UsbdOpenDefaultEndpoint);
REX_EXPORT_STUB(__imp__UsbdOpenEndpoint);
REX_EXPORT_STUB(__imp__UsbdQueueAsyncTransfer);
REX_EXPORT_STUB(__imp__UsbdQueueCloseDefaultEndpoint);
REX_EXPORT_STUB(__imp__UsbdQueueCloseEndpoint);
REX_EXPORT_STUB(__imp__UsbdQueueIsochTransfer);
REX_EXPORT_STUB(__imp__UsbdRegisterDriverObject);
REX_EXPORT_STUB(__imp__UsbdRemoveDeviceComplete);
REX_EXPORT_STUB(__imp__UsbdResetDevice);
REX_EXPORT_STUB(__imp__UsbdResetEndpoint);
REX_EXPORT_STUB(__imp__UsbdSetTimer);
REX_EXPORT_STUB(__imp__UsbdTitleDriverResetAllUnrecognizedPorts);
REX_EXPORT_STUB(__imp__UsbdTitleDriverSetUnrecognizedPort);
REX_EXPORT_STUB(__imp__UsbdUnregisterDriverObject);
REX_EXPORT_STUB(__imp__VeSetHandlers);
REX_EXPORT_STUB(__imp__VgcHandler_SetHandlers);
REX_EXPORT_STUB(__imp__VvcHandlerCancelTransfers);
REX_EXPORT_STUB(__imp__VvcHandlerRetrieveVoiceExtension);
REX_EXPORT_STUB(__imp__WifiBeginAuthentication);
REX_EXPORT_STUB(__imp__WifiCalculateRegulatoryDomain);
REX_EXPORT_STUB(__imp__WifiChannelToFrequency);
REX_EXPORT_STUB(__imp__WifiCheckCounterMeasures);
REX_EXPORT_STUB(__imp__WifiChooseAuthenCipherSetFromBSSID);
REX_EXPORT_STUB(__imp__WifiCompleteAuthentication);
REX_EXPORT_STUB(__imp__WifiDeduceNetworkType);
REX_EXPORT_STUB(__imp__WifiGetAssociationIE);
REX_EXPORT_STUB(__imp__WifiOnMICError);
REX_EXPORT_STUB(__imp__WifiPrepareAuthenticationContext);
REX_EXPORT_STUB(__imp__WifiRecvEAPOLPacket);
REX_EXPORT_STUB(__imp__WifiSelectAdHocChannel);
REX_EXPORT_STUB(__imp__XVoicedActivate);
REX_EXPORT_STUB(__imp__XVoicedClose);
REX_EXPORT_STUB(__imp__XVoicedGetBatteryStatus);
REX_EXPORT_STUB(__imp__XVoicedGetDirectionalData);
REX_EXPORT_STUB(__imp__XVoicedHeadsetPresent);
REX_EXPORT_STUB(__imp__XVoicedIsActiveProcess);
REX_EXPORT_STUB(__imp__XVoicedSendVPort);
REX_EXPORT_STUB(__imp__XVoicedSubmitPacket);

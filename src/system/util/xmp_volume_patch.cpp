/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <rex/system/util/xmp_volume_patch.h>

#include <rex/audio/audio_media_player.h>
#include <rex/kernel/xam/apps/xmp_app.h>
#include <rex/logging.h>
#include <rex/memory.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xam/app_manager.h>

namespace rex::system::util {

namespace {
// XmpApp's registered app id (see XmpApp::XmpApp -> App(kernel_state, 0xFA)).
constexpr uint32_t kXmpAppId = 0xFA;

// Convert a volume byte to a float in [0.0001, 1.0]. Uses a near-zero floor
// instead of 0.0f so AudioMediaPlayer::PlaySong's own "0.0 means unset"
// workaround doesn't override this with its default.
float ByteToVolume(uint8_t value, uint8_t max_value) {
  if (value == 0) {
    return 0.0001f;
  }
  return static_cast<float>(value) / static_cast<float>(max_value);
}
}  // namespace

bool XmpVolumePatch::Matches(const std::string& path, uint32_t length) const {
  if (path.find(config_.file_name) == std::string::npos) {
    return false;
  }
  if (config_.write_length != 0) {
    return length == config_.write_length;
  }
  return length >= config_.offset + 1;
}

void XmpVolumePatch::ApplyVolume(const uint8_t* buffer, [[maybe_unused]] uint32_t length) {
  uint8_t volume_byte = buffer[config_.offset];
  if (volume_byte > config_.max_value) {
    return;
  }

  float volume = ByteToVolume(volume_byte, config_.max_value);

  REXSYS_DEBUG("XmpVolumePatch: volume={}/{} -> {:.2f}", volume_byte, config_.max_value, volume);

  last_volume_ = volume_byte;
  auto* xmp_app =
      static_cast<rex::kernel::xam::apps::XmpApp*>(kernel_state_->app_manager()->LookupApp(kXmpAppId));
  if (xmp_app && xmp_app->media_player()) {
    xmp_app->media_player()->SetVolume(volume);
  }
}

void XmpVolumePatch::PollGuestVolume() {
  if (!settings_guest_addr_) {
    return;
  }

  auto* mem = kernel_state_->memory();
  uint32_t vol_addr = settings_guest_addr_ + config_.offset;
  uint8_t current = mem->TranslateVirtual<uint8_t*>(vol_addr)[0];

  if (current == last_volume_ || current > config_.max_value) {
    return;
  }

  last_volume_ = current;
  float volume = ByteToVolume(current, config_.max_value);

  REXSYS_DEBUG("XmpVolumePatch: polled volume change={}/{} -> {:.2f}", current, config_.max_value,
              volume);

  auto* xmp_app =
      static_cast<rex::kernel::xam::apps::XmpApp*>(kernel_state_->app_manager()->LookupApp(kXmpAppId));
  if (xmp_app && xmp_app->media_player()) {
    xmp_app->media_player()->SetVolume(volume);
  }
}

void XmpVolumePatch::OnFileWrite(const std::string& path, const uint8_t* buffer, uint32_t length,
                                 [[maybe_unused]] uint32_t guest_address) {
  if (Matches(path, length)) {
    ApplyVolume(buffer, length);
  }
}

void XmpVolumePatch::OnFileRead(const std::string& path, const uint8_t* buffer, uint32_t length,
                                uint32_t guest_address) {
  if (Matches(path, length)) {
    settings_guest_addr_ = guest_address;
    ApplyVolume(buffer, length);
  }
}

void XmpVolumePatch::OnInputPoll(uint32_t packet_number) {
  if (packet_number == last_packet_number_) {
    return;
  }
  last_packet_number_ = packet_number;
  PollGuestVolume();
}

std::unique_ptr<XmpVolumePatch> XmpVolumePatch::CreateForTitle(uint32_t title_id, KernelState* ks) {
  XmpVolumeConfig config;

  switch (title_id) {
    case 0x544307D2:  // Dead or Alive Xtreme 2
      config = {"ups.dat", 0, 0x50, 100};
      break;
    case 0x4D5307D1:  // Project Gotham Racing 3
      config = {"profile.save", 528, 19, 10};
      break;
    case 0x4D5307F9:  // Project Gotham Racing 4
      config = {"savegame", 344, 2, 10};
      break;
    default:
      return nullptr;
  }

  REXSYS_INFO("XmpVolumePatch: loading for title {:08X}", title_id);
  return std::make_unique<XmpVolumePatch>(ks, config);
}

}  // namespace rex::system::util

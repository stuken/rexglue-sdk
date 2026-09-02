/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */
#include <rex/system/xam/user_settings.h>

#include <rex/assert.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>

namespace rex {
namespace system {
namespace xam {

UserSetting::UserSetting(UserSettingId setting_id, const UserDataTypes& setting_data)
    : UserData(get_type(static_cast<uint32_t>(setting_id)), setting_data),
      setting_id_(setting_id),
      setting_source_(X_USER_PROFILE_SETTING_SOURCE::DEFAULT) {}

UserSetting::UserSetting(uint32_t setting_id, const void* wire_bytes,
                         std::span<const uint8_t> extended_data,
                         X_USER_PROFILE_SETTING_SOURCE source)
    : UserData(get_type(setting_id), wire_bytes, extended_data),
      setting_id_(static_cast<UserSettingId>(setting_id)),
      setting_source_(source) {}

std::optional<UserSetting> UserSetting::GetDefaultSetting(uint32_t setting_id) {
  // Defaults rexglue has always shipped (kept to avoid regressing existing
  // behavior), plus xenia's real-console picture key / gamer tier values.
  static const std::vector<UserSetting> kDefaults = [] {
    std::vector<UserSetting> defaults;
    defaults.emplace_back(UserSettingId::XPROFILE_GAMER_YAXIS_INVERSION, int32_t(0));
    defaults.emplace_back(UserSettingId::XPROFILE_OPTION_CONTROLLER_VIBRATION, int32_t(3));
    defaults.emplace_back(UserSettingId::XPROFILE_GAMERCARD_ZONE, int32_t(0));
    defaults.emplace_back(UserSettingId::XPROFILE_GAMERCARD_REGION, int32_t(0));
    defaults.emplace_back(UserSettingId::XPROFILE_GAMERCARD_CRED, int32_t(0xFA));
    defaults.emplace_back(UserSettingId::XPROFILE_GAMERCARD_REP, 0.0f);
    defaults.emplace_back(UserSettingId::XPROFILE_OPTION_VOICE_MUTED, int32_t(0));
    defaults.emplace_back(UserSettingId::XPROFILE_OPTION_VOICE_THRU_SPEAKERS, int32_t(0));
    defaults.emplace_back(UserSettingId::XPROFILE_OPTION_VOICE_VOLUME, int32_t(0x64));
    defaults.emplace_back(UserSettingId::XPROFILE_GAMERCARD_MOTTO, std::u16string());
    defaults.emplace_back(UserSettingId::XPROFILE_GAMERCARD_TITLES_PLAYED, int32_t(1));
    defaults.emplace_back(UserSettingId::XPROFILE_GAMERCARD_ACHIEVEMENTS_EARNED, int32_t(0));
    defaults.emplace_back(UserSettingId::XPROFILE_GAMER_DIFFICULTY, int32_t(0));
    defaults.emplace_back(UserSettingId::XPROFILE_GAMER_CONTROL_SENSITIVITY, int32_t(0));
    defaults.emplace_back(UserSettingId::XPROFILE_GAMER_PREFERRED_COLOR_FIRST,
                          static_cast<int32_t>(0xFFFF0000u));
    defaults.emplace_back(UserSettingId::XPROFILE_GAMER_PREFERRED_COLOR_SECOND,
                          static_cast<int32_t>(0xFF00FF00u));
    defaults.emplace_back(UserSettingId::XPROFILE_GAMER_ACTION_AUTO_AIM, int32_t(1));
    defaults.emplace_back(UserSettingId::XPROFILE_GAMER_ACTION_AUTO_CENTER, int32_t(0));
    defaults.emplace_back(UserSettingId::XPROFILE_GAMER_ACTION_MOVEMENT_CONTROL, int32_t(0));
    defaults.emplace_back(UserSettingId::XPROFILE_GAMER_RACE_TRANSMISSION, int32_t(0));
    defaults.emplace_back(UserSettingId::XPROFILE_GAMER_RACE_CAMERA_LOCATION, int32_t(0));
    defaults.emplace_back(UserSettingId::XPROFILE_GAMER_RACE_BRAKE_CONTROL, int32_t(0));
    defaults.emplace_back(UserSettingId::XPROFILE_GAMER_RACE_ACCELERATOR_CONTROL, int32_t(0));
    defaults.emplace_back(UserSettingId::XPROFILE_GAMERCARD_TITLE_CRED_EARNED, int32_t(0));
    defaults.emplace_back(UserSettingId::XPROFILE_GAMERCARD_TITLE_ACHIEVEMENTS_EARNED, int32_t(0));
    // Real console default: Xbox LIVE Gold. See xenia's
    // X_XAMACCOUNTINFO::AccountSubscriptionTier::kSubscriptionTierGold.
    defaults.emplace_back(UserSettingId::XPROFILE_GAMER_TIER, int32_t(6));
    // Real console gamer-picture-key format (title_id + big/small tile ids as
    // hex ASCII), ported from xenia's default_setting_values - rexglue
    // previously shipped a placeholder ASCII string here instead.
    defaults.emplace_back(UserSettingId::XPROFILE_GAMERCARD_PICTURE_KEY,
                          std::u16string(u"FFFE07D10002000200010002"));
    defaults.emplace_back(UserSettingId::XPROFILE_TITLE_SPECIFIC1, std::vector<uint8_t>());
    defaults.emplace_back(UserSettingId::XPROFILE_TITLE_SPECIFIC2, std::vector<uint8_t>());
    defaults.emplace_back(UserSettingId::XPROFILE_TITLE_SPECIFIC3, std::vector<uint8_t>());
    return defaults;
  }();

  for (const auto& setting : kDefaults) {
    if (setting.get_setting_id() == setting_id) {
      return std::make_optional<UserSetting>(setting);
    }
  }

  const auto type = UserData::get_type(setting_id);
  switch (type) {
    case X_USER_DATA_TYPE::CONTEXT:
      return std::make_optional<UserSetting>(static_cast<UserSettingId>(setting_id), uint32_t(0));
    case X_USER_DATA_TYPE::INT32:
    case X_USER_DATA_TYPE::UNSET:
      return std::make_optional<UserSetting>(static_cast<UserSettingId>(setting_id), int32_t(0));
    case X_USER_DATA_TYPE::INT64:
    case X_USER_DATA_TYPE::DATETIME:
      return std::make_optional<UserSetting>(static_cast<UserSettingId>(setting_id), int64_t(0));
    case X_USER_DATA_TYPE::DOUBLE:
      return std::make_optional<UserSetting>(static_cast<UserSettingId>(setting_id), 0.0);
    case X_USER_DATA_TYPE::WSTRING:
      return std::make_optional<UserSetting>(static_cast<UserSettingId>(setting_id),
                                             std::u16string());
    case X_USER_DATA_TYPE::FLOAT:
      return std::make_optional<UserSetting>(static_cast<UserSettingId>(setting_id), 0.0f);
    case X_USER_DATA_TYPE::BINARY:
      return std::make_optional<UserSetting>(static_cast<UserSettingId>(setting_id),
                                             std::vector<uint8_t>());
    default:
      assert_always();
  }

  REXSYS_ERROR("UserSetting::GetDefaultSetting: unknown X_USER_DATA_TYPE {}",
              static_cast<uint8_t>(type));
  return std::nullopt;
}

void UserSetting::WriteToGuest(void* data_dest, uint32_t& extended_data_address) const {
  if (!data_dest) {
    return;
  }

  WriteWireTo(data_dest);

  if (!requires_additional_data()) {
    return;
  }

  const auto extended_data = get_extended_data();
  if (extended_data.empty()) {
    return;
  }

  auto* wire = static_cast<X_USER_DATA_WIRE*>(data_dest);
  if (wire->type == X_USER_DATA_TYPE::BINARY) {
    wire->data.binary.size = static_cast<uint32_t>(extended_data.size());
    wire->data.binary.ptr = extended_data_address;
  } else {
    wire->data.unicode.size = static_cast<uint32_t>(extended_data.size());
    wire->data.unicode.ptr = extended_data_address;
  }

  std::memcpy(REX_KERNEL_MEMORY()->TranslateVirtual(extended_data_address), extended_data.data(),
             extended_data.size());

  extended_data_address += static_cast<uint32_t>(get_max_size(get_setting_id()));
}

std::vector<uint8_t> UserSetting::Serialize() const {
  std::vector<uint8_t> data(sizeof(util::X_XDBF_GPD_SETTING_HEADER) + get_extended_data().size());

  util::X_XDBF_GPD_SETTING_HEADER header = {};
  header.setting_id = get_setting_id();
  header.base_data = *get_data();

  std::memcpy(data.data(), &header, sizeof(header));
  if (!get_extended_data().empty()) {
    std::memcpy(data.data() + sizeof(header), get_extended_data().data(), get_extended_data().size());
  }
  return data;
}

}  // namespace xam
}  // namespace system
}  // namespace rex

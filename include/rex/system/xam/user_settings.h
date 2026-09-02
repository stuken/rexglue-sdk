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
#pragma once

#include <algorithm>
#include <array>
#include <optional>
#include <set>
#include <span>
#include <vector>

#include <rex/system/util/gpd_info.h>
#include <rex/system/xam/user_data.h>

namespace rex {
namespace system {
namespace xam {

// Mirrors the "from" field games see in X_USER_PROFILE_SETTING - ported
// verbatim from xenia's X_USER_PROFILE_SETTING_SOURCE.
enum class X_USER_PROFILE_SETTING_SOURCE : uint32_t {
  NO_VALUE = 0,
  DEFAULT = 1,           // Default value taken from default OS values.
  TITLE = 2,             // Value written by title or OS.
  PERMISSION_DENIED = 3,
};

constexpr uint32_t kMaxUserSettingId = 0x58;

constexpr uint32_t SettingKey(X_USER_DATA_TYPE type, uint16_t size, uint16_t id) {
  return static_cast<uint32_t>(type) << 28 | size << 16 | id;
}

// Real Xbox 360 XPROFILE_* setting IDs, ported verbatim from xenia's
// user_settings.h (UserSettingId enum / SettingKey helper). Values must not
// be changed - they're wire-format IDs games actually use.
enum class UserSettingId : uint32_t {
  XPROFILE_PERMISSIONS = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0),
  XPROFILE_GAMER_TYPE = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 1),
  XPROFILE_GAMER_YAXIS_INVERSION = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 2),
  XPROFILE_OPTION_CONTROLLER_VIBRATION = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 3),
  XPROFILE_GAMERCARD_ZONE = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 4),
  XPROFILE_GAMERCARD_REGION = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 5),
  XPROFILE_GAMERCARD_CRED = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 6),
  XPROFILE_GAMER_PRESENCE_USER_STATE = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 7),
  XPROFILE_GAMERCARD_HAS_VISION = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 8),

  XPROFILE_OPTION_VOICE_MUTED = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0xC),
  XPROFILE_OPTION_VOICE_THRU_SPEAKERS = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0xD),
  XPROFILE_OPTION_VOICE_VOLUME = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0xE),

  XPROFILE_GAMERCARD_TITLES_PLAYED = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x12),
  XPROFILE_GAMERCARD_ACHIEVEMENTS_EARNED = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x13),
  XPROFILE_GAMER_DIFFICULTY = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x15),
  XPROFILE_GAMER_CONTROL_SENSITIVITY = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x18),
  XPROFILE_GAMER_PREFERRED_COLOR_FIRST = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x1D),
  XPROFILE_GAMER_PREFERRED_COLOR_SECOND = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x1E),
  XPROFILE_GAMER_ACTION_AUTO_AIM = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x22),
  XPROFILE_GAMER_ACTION_AUTO_CENTER = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x23),
  XPROFILE_GAMER_ACTION_MOVEMENT_CONTROL = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x24),
  XPROFILE_GAMER_RACE_TRANSMISSION = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x26),
  XPROFILE_GAMER_RACE_CAMERA_LOCATION = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x27),
  XPROFILE_GAMER_RACE_BRAKE_CONTROL = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x28),
  XPROFILE_GAMER_RACE_ACCELERATOR_CONTROL = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x29),
  XPROFILE_GAMERCARD_TITLE_CRED_EARNED = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x38),
  XPROFILE_GAMERCARD_TITLE_ACHIEVEMENTS_EARNED =
      SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x39),
  XPROFILE_GAMER_TIER = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x3A),
  XPROFILE_MESSENGER_SIGNUP_STATE = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x3B),
  XPROFILE_MESSENGER_AUTO_SIGNIN = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x3C),
  XPROFILE_SAVE_WINDOWS_LIVE_PASSWORD = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x3D),
  XPROFILE_FRIENDSAPP_SHOW_BUDDIES = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x3E),
  XPROFILE_GAMERCARD_SERVICE_TYPE_FLAGS = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x3F),
  XPROFILE_ENABLE_TUTORIALS = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x40),
  XPROFILE_ENABLE_SUBTITLES = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x41),
  XPROFILE_UNKNOWN_42 = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x42),
  XPROFILE_UNKNOWN_43 = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x43),
  XPROFILE_AIM_SENSITIVITY_XAXIS = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x44),
  XPROFILE_AIM_SENSITIVITY_YAXIS = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x45),
  XPROFILE_UNKNOWN_46 = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x46),
  XPROFILE_TENURE_LEVEL = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x47),
  XPROFILE_TENURE_MILESTONE = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x48),

  XPROFILE_UNKNOWN_49 = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x49),
  XPROFILE_SHOW_DAMAGE_INDICATORS = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x4A),
  XPROFILE_SUBSCRIPTION_TYPE_LENGTH_IN_MONTHS =
      SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x4B),
  XPROFILE_SUBSCRIPTION_PAYMENT_TYPE = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x4C),
  XPROFILE_PEC_INFO = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x4D),
  XPROFILE_NUI_BIOMETRIC_SIGNIN = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x4E),
  XPROFILE_GFWL_VADNORMAL = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x4F),
  XPROFILE_UNKNOWN_50 = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x50),
  XPROFILE_MINIMAP_AUTOROTATE = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x51),
  XPROFILE_BEACONS_SOCIAL_NETWORK_SHARING = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x52),
  XPROFILE_USER_PREFERENCES = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x53),
  XPROFILE_XBOXONE_GAMERSCORE = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x57),

  WEB_EMAIL_FORMAT = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x2000),
  WEB_FLAGS = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x2001),
  WEB_SPAM = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x2002),
  WEB_FAVORITE_GENRE = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x2003),
  WEB_FAVORITE_GAME = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x2004),
  WEB_FAVORITE_GAME1 = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x2005),
  WEB_FAVORITE_GAME2 = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x2006),
  WEB_FAVORITE_GAME3 = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x2007),
  WEB_FAVORITE_GAME4 = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x2008),
  WEB_FAVORITE_GAME5 = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x2009),
  WEB_PLATFORMS_OWNED = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x200A),
  WEB_CONNECTION_SPEED = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x200B),
  WEB_FLASH = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x200C),
  WEB_VIDEO_PREFERENCE = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x200D),
  XPROFILE_CRUX_MEDIA_STYLE1 = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x3EA),
  XPROFILE_CRUX_MEDIA_STYLE2 = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x3EB),
  XPROFILE_CRUX_MEDIA_STYLE3 = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x3EC),
  XPROFILE_CRUX_TOP_ALBUM1 = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x3ED),
  XPROFILE_CRUX_TOP_ALBUM2 = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x3EE),
  XPROFILE_CRUX_TOP_ALBUM3 = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x3EF),
  XPROFILE_CRUX_TOP_ALBUM4 = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x3F0),
  XPROFILE_CRUX_TOP_ALBUM5 = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x3F1),
  XPROFILE_CRUX_BKGD_IMAGE = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x3F3),

  XPROFILE_GAMERCARD_USER_LOCATION = SettingKey(X_USER_DATA_TYPE::WSTRING, 0x52, 0x41),
  XPROFILE_GAMERCARD_USER_NAME = SettingKey(X_USER_DATA_TYPE::WSTRING, 0x104, 0x40),
  XPROFILE_GAMERCARD_USER_URL = SettingKey(X_USER_DATA_TYPE::WSTRING, 0x190, 0x42),
  XPROFILE_GAMERCARD_USER_BIO = SettingKey(X_USER_DATA_TYPE::WSTRING, kMaxUserDataSize, 0x43),

  XPROFILE_CRUX_BIO = SettingKey(X_USER_DATA_TYPE::WSTRING, kMaxUserDataSize, 0x3FA),
  XPROFILE_CRUX_BG_SMALL_PRIVATE = SettingKey(X_USER_DATA_TYPE::WSTRING, 0x64, 0x3FB),
  XPROFILE_CRUX_BG_LARGE_PRIVATE = SettingKey(X_USER_DATA_TYPE::WSTRING, 0x64, 0x3FC),
  XPROFILE_CRUX_BG_SMALL_PUBLIC = SettingKey(X_USER_DATA_TYPE::WSTRING, 0x64, 0x3FD),
  XPROFILE_CRUX_BG_LARGE_PUBLIC = SettingKey(X_USER_DATA_TYPE::WSTRING, 0x64, 0x3FE),

  XPROFILE_GAMERCARD_PICTURE_KEY = SettingKey(X_USER_DATA_TYPE::WSTRING, 0x64, 0xF),
  XPROFILE_GAMERCARD_PERSONAL_PICTURE = SettingKey(X_USER_DATA_TYPE::WSTRING, 0x64, 0x10),
  XPROFILE_GAMERCARD_MOTTO = SettingKey(X_USER_DATA_TYPE::WSTRING, 0x2C, 0x11),
  XPROFILE_GFWL_RECDEVICEDESC = SettingKey(X_USER_DATA_TYPE::WSTRING, 200, 0x49),

  XPROFILE_GFWL_PLAYDEVICEDESC = SettingKey(X_USER_DATA_TYPE::WSTRING, 200, 0x4B),
  XPROFILE_CRUX_MEDIA_PICTURE = SettingKey(X_USER_DATA_TYPE::WSTRING, 0x64, 0x3E8),
  XPROFILE_CRUX_MEDIA_MOTTO = SettingKey(X_USER_DATA_TYPE::WSTRING, 0x100, 0x3F6),

  XPROFILE_GAMERCARD_REP = SettingKey(X_USER_DATA_TYPE::FLOAT, sizeof(float), 0xB),
  XPROFILE_GFWL_VOLUMELEVEL = SettingKey(X_USER_DATA_TYPE::FLOAT, sizeof(float), 0x4C),
  XPROFILE_GFWL_RECLEVEL = SettingKey(X_USER_DATA_TYPE::FLOAT, sizeof(float), 0x4D),
  XPROFILE_GFWL_PLAYDEVICE = SettingKey(X_USER_DATA_TYPE::BINARY, 0x10, 0x4A),

  XPROFILE_VIDEO_METADATA = SettingKey(X_USER_DATA_TYPE::BINARY, 0x20, 0x4A),

  XPROFILE_CRUX_OFFLINE_ID = SettingKey(X_USER_DATA_TYPE::BINARY, 0x34, 0x3F2),

  XPROFILE_UNK_61180050 = SettingKey(X_USER_DATA_TYPE::BINARY, 280, 0x50),

  XPROFILE_JUMP_IN_LIST = SettingKey(X_USER_DATA_TYPE::BINARY, kMaxUserDataSize, 0x51),

  XPROFILE_GAMERCARD_PARTY_ADDR = SettingKey(X_USER_DATA_TYPE::BINARY, 0x62, 0x54),

  XPROFILE_CRUX_TOP_MUSIC = SettingKey(X_USER_DATA_TYPE::BINARY, 0xA8, 0x3F5),

  XPROFILE_CRUX_TOP_MEDIAID1 = SettingKey(X_USER_DATA_TYPE::BINARY, 0x10, 0x3F7),
  XPROFILE_CRUX_TOP_MEDIAID2 = SettingKey(X_USER_DATA_TYPE::BINARY, 0x10, 0x3F8),
  XPROFILE_CRUX_TOP_MEDIAID3 = SettingKey(X_USER_DATA_TYPE::BINARY, 0x10, 0x3F9),

  XPROFILE_GAMERCARD_AVATAR_INFO_1 = SettingKey(X_USER_DATA_TYPE::BINARY, kMaxUserDataSize, 0x44),
  XPROFILE_GAMERCARD_AVATAR_INFO_2 = SettingKey(X_USER_DATA_TYPE::BINARY, kMaxUserDataSize, 0x45),
  XPROFILE_GAMERCARD_PARTY_INFO = SettingKey(X_USER_DATA_TYPE::BINARY, 0x100, 0x46),

  XPROFILE_TITLE_SPECIFIC1 = SettingKey(X_USER_DATA_TYPE::BINARY, kMaxUserDataSize, 0x3FFF),
  XPROFILE_TITLE_SPECIFIC2 = SettingKey(X_USER_DATA_TYPE::BINARY, kMaxUserDataSize, 0x3FFE),
  XPROFILE_TITLE_SPECIFIC3 = SettingKey(X_USER_DATA_TYPE::BINARY, kMaxUserDataSize, 0x3FFD),

  XPROFILE_CRUX_LAST_CHANGE_TIME = SettingKey(X_USER_DATA_TYPE::DATETIME, sizeof(uint64_t), 0x3F4),
  XPROFILE_TENURE_NEXT_MILESTONE_DATE = SettingKey(X_USER_DATA_TYPE::DATETIME, sizeof(uint64_t), 0x49),
  XPROFILE_LAST_LIVE_SIGNIN = SettingKey(X_USER_DATA_TYPE::DATETIME, sizeof(uint64_t), 0x4F),
};

inline constexpr std::array<UserSettingId, 115> known_settings = {
    UserSettingId::XPROFILE_PERMISSIONS,
    UserSettingId::XPROFILE_GAMER_TYPE,
    UserSettingId::XPROFILE_GAMER_YAXIS_INVERSION,
    UserSettingId::XPROFILE_OPTION_CONTROLLER_VIBRATION,
    UserSettingId::XPROFILE_GAMERCARD_ZONE,
    UserSettingId::XPROFILE_GAMERCARD_REGION,
    UserSettingId::XPROFILE_GAMERCARD_CRED,
    UserSettingId::XPROFILE_GAMER_PRESENCE_USER_STATE,
    UserSettingId::XPROFILE_GAMERCARD_HAS_VISION,
    UserSettingId::XPROFILE_OPTION_VOICE_MUTED,
    UserSettingId::XPROFILE_OPTION_VOICE_THRU_SPEAKERS,
    UserSettingId::XPROFILE_OPTION_VOICE_VOLUME,
    UserSettingId::XPROFILE_GAMERCARD_TITLES_PLAYED,
    UserSettingId::XPROFILE_GAMERCARD_ACHIEVEMENTS_EARNED,
    UserSettingId::XPROFILE_GAMER_DIFFICULTY,
    UserSettingId::XPROFILE_GAMER_CONTROL_SENSITIVITY,
    UserSettingId::XPROFILE_GAMER_PREFERRED_COLOR_FIRST,
    UserSettingId::XPROFILE_GAMER_PREFERRED_COLOR_SECOND,
    UserSettingId::XPROFILE_GAMER_ACTION_AUTO_AIM,
    UserSettingId::XPROFILE_GAMER_ACTION_AUTO_CENTER,
    UserSettingId::XPROFILE_GAMER_ACTION_MOVEMENT_CONTROL,
    UserSettingId::XPROFILE_GAMER_RACE_TRANSMISSION,
    UserSettingId::XPROFILE_GAMER_RACE_CAMERA_LOCATION,
    UserSettingId::XPROFILE_GAMER_RACE_BRAKE_CONTROL,
    UserSettingId::XPROFILE_GAMER_RACE_ACCELERATOR_CONTROL,
    UserSettingId::XPROFILE_GAMERCARD_TITLE_CRED_EARNED,
    UserSettingId::XPROFILE_GAMERCARD_TITLE_ACHIEVEMENTS_EARNED,
    UserSettingId::XPROFILE_GAMER_TIER,
    UserSettingId::XPROFILE_MESSENGER_SIGNUP_STATE,
    UserSettingId::XPROFILE_MESSENGER_AUTO_SIGNIN,
    UserSettingId::XPROFILE_SAVE_WINDOWS_LIVE_PASSWORD,
    UserSettingId::XPROFILE_FRIENDSAPP_SHOW_BUDDIES,
    UserSettingId::XPROFILE_GAMERCARD_SERVICE_TYPE_FLAGS,
    UserSettingId::XPROFILE_ENABLE_TUTORIALS,
    UserSettingId::XPROFILE_ENABLE_SUBTITLES,
    UserSettingId::XPROFILE_UNKNOWN_42,
    UserSettingId::XPROFILE_UNKNOWN_43,
    UserSettingId::XPROFILE_AIM_SENSITIVITY_XAXIS,
    UserSettingId::XPROFILE_AIM_SENSITIVITY_YAXIS,
    UserSettingId::XPROFILE_UNKNOWN_46,
    UserSettingId::XPROFILE_TENURE_LEVEL,
    UserSettingId::XPROFILE_TENURE_MILESTONE,
    UserSettingId::XPROFILE_UNKNOWN_49,
    UserSettingId::XPROFILE_SHOW_DAMAGE_INDICATORS,
    UserSettingId::XPROFILE_SUBSCRIPTION_TYPE_LENGTH_IN_MONTHS,
    UserSettingId::XPROFILE_SUBSCRIPTION_PAYMENT_TYPE,
    UserSettingId::XPROFILE_PEC_INFO,
    UserSettingId::XPROFILE_NUI_BIOMETRIC_SIGNIN,
    UserSettingId::XPROFILE_GFWL_VADNORMAL,
    UserSettingId::XPROFILE_UNKNOWN_50,
    UserSettingId::XPROFILE_MINIMAP_AUTOROTATE,
    UserSettingId::XPROFILE_BEACONS_SOCIAL_NETWORK_SHARING,
    UserSettingId::XPROFILE_USER_PREFERENCES,
    UserSettingId::XPROFILE_XBOXONE_GAMERSCORE,
    UserSettingId::WEB_EMAIL_FORMAT,
    UserSettingId::WEB_FLAGS,
    UserSettingId::WEB_SPAM,
    UserSettingId::WEB_FAVORITE_GENRE,
    UserSettingId::WEB_FAVORITE_GAME,
    UserSettingId::WEB_FAVORITE_GAME1,
    UserSettingId::WEB_FAVORITE_GAME2,
    UserSettingId::WEB_FAVORITE_GAME3,
    UserSettingId::WEB_FAVORITE_GAME4,
    UserSettingId::WEB_FAVORITE_GAME5,
    UserSettingId::WEB_PLATFORMS_OWNED,
    UserSettingId::WEB_CONNECTION_SPEED,
    UserSettingId::WEB_FLASH,
    UserSettingId::WEB_VIDEO_PREFERENCE,
    UserSettingId::XPROFILE_CRUX_MEDIA_STYLE1,
    UserSettingId::XPROFILE_CRUX_MEDIA_STYLE2,
    UserSettingId::XPROFILE_CRUX_MEDIA_STYLE3,
    UserSettingId::XPROFILE_CRUX_TOP_ALBUM1,
    UserSettingId::XPROFILE_CRUX_TOP_ALBUM2,
    UserSettingId::XPROFILE_CRUX_TOP_ALBUM3,
    UserSettingId::XPROFILE_CRUX_TOP_ALBUM4,
    UserSettingId::XPROFILE_CRUX_TOP_ALBUM5,
    UserSettingId::XPROFILE_CRUX_BKGD_IMAGE,
    UserSettingId::XPROFILE_GAMERCARD_USER_LOCATION,
    UserSettingId::XPROFILE_GAMERCARD_USER_NAME,
    UserSettingId::XPROFILE_GAMERCARD_USER_URL,
    UserSettingId::XPROFILE_GAMERCARD_USER_BIO,
    UserSettingId::XPROFILE_CRUX_BIO,
    UserSettingId::XPROFILE_CRUX_BG_SMALL_PRIVATE,
    UserSettingId::XPROFILE_CRUX_BG_LARGE_PRIVATE,
    UserSettingId::XPROFILE_CRUX_BG_SMALL_PUBLIC,
    UserSettingId::XPROFILE_CRUX_BG_LARGE_PUBLIC,
    UserSettingId::XPROFILE_GAMERCARD_PICTURE_KEY,
    UserSettingId::XPROFILE_GAMERCARD_PERSONAL_PICTURE,
    UserSettingId::XPROFILE_GAMERCARD_MOTTO,
    UserSettingId::XPROFILE_GFWL_RECDEVICEDESC,
    UserSettingId::XPROFILE_GFWL_PLAYDEVICEDESC,
    UserSettingId::XPROFILE_CRUX_MEDIA_PICTURE,
    UserSettingId::XPROFILE_CRUX_MEDIA_MOTTO,
    UserSettingId::XPROFILE_GAMERCARD_REP,
    UserSettingId::XPROFILE_GFWL_VOLUMELEVEL,
    UserSettingId::XPROFILE_GFWL_RECLEVEL,
    UserSettingId::XPROFILE_GFWL_PLAYDEVICE,
    UserSettingId::XPROFILE_VIDEO_METADATA,
    UserSettingId::XPROFILE_CRUX_OFFLINE_ID,
    UserSettingId::XPROFILE_UNK_61180050,
    UserSettingId::XPROFILE_JUMP_IN_LIST,
    UserSettingId::XPROFILE_GAMERCARD_PARTY_ADDR,
    UserSettingId::XPROFILE_CRUX_TOP_MUSIC,
    UserSettingId::XPROFILE_CRUX_TOP_MEDIAID1,
    UserSettingId::XPROFILE_CRUX_TOP_MEDIAID2,
    UserSettingId::XPROFILE_CRUX_TOP_MEDIAID3,
    UserSettingId::XPROFILE_GAMERCARD_AVATAR_INFO_1,
    UserSettingId::XPROFILE_GAMERCARD_AVATAR_INFO_2,
    UserSettingId::XPROFILE_GAMERCARD_PARTY_INFO,
    UserSettingId::XPROFILE_TITLE_SPECIFIC1,
    UserSettingId::XPROFILE_TITLE_SPECIFIC2,
    UserSettingId::XPROFILE_TITLE_SPECIFIC3,
    UserSettingId::XPROFILE_CRUX_LAST_CHANGE_TIME,
    UserSettingId::XPROFILE_TENURE_NEXT_MILESTONE_DATE,
    UserSettingId::XPROFILE_LAST_LIVE_SIGNIN,
};

inline const std::set<UserSettingId> title_writable_settings = {
    UserSettingId::XPROFILE_TITLE_SPECIFIC1,
    UserSettingId::XPROFILE_TITLE_SPECIFIC2,
    UserSettingId::XPROFILE_TITLE_SPECIFIC3,
};

// True if setting_id is either a "core" setting (id sub-field < kMaxUserSettingId)
// or one of the extended settings above.
inline bool IsKnownUserSettingId(uint32_t setting_id) {
  AttributeKey key;
  key.value = setting_id;
  if (key.id < kMaxUserSettingId) {
    return true;
  }
  return std::find(known_settings.cbegin(), known_settings.cend(),
                   static_cast<UserSettingId>(setting_id)) != known_settings.cend();
}

// GPD-shaped replacement for rexglue's ad-hoc UserProfile::Setting hierarchy
// (item 1.3). Ported from xenia's UserSetting (user_settings.h/.cc) - ties a
// setting's wire-format value directly to how it's stored in a GPD entry, so
// XamUserWriteProfileSettings/XamUserReadProfileSettingsEx and real .gpd
// persistence share one representation instead of two.
class UserSetting : public UserData {
 public:
  UserSetting(const UserSetting&) = default;
  UserSetting(UserSetting&&) = default;

  // From host (defaults, or host/recomp code writing a setting directly).
  UserSetting(UserSettingId setting_id, const UserDataTypes& setting_data);

  // From guest (XamUserWriteProfileSettings) or a GPD setting entry:
  // wire_bytes is a 16-byte X_USER_DATA_WIRE-shaped blob, extended_data the
  // already-resolved BINARY/WSTRING payload (empty span if not needed).
  UserSetting(uint32_t setting_id, const void* wire_bytes, std::span<const uint8_t> extended_data,
             X_USER_PROFILE_SETTING_SOURCE source);

  static std::optional<UserSetting> GetDefaultSetting(uint32_t setting_id);

  // Writes this setting's 16-byte header into data_dest (a
  // X_USER_PROFILE_SETTING::data-shaped destination), and any BINARY/WSTRING
  // payload into guest memory at extended_data_address, advancing it by this
  // setting's max size so the caller can pack multiple settings back-to-back.
  void WriteToGuest(void* data_dest, uint32_t& extended_data_address) const;

  std::vector<uint8_t> Serialize() const;

  uint32_t get_setting_id() const { return static_cast<uint32_t>(setting_id_); }
  X_USER_PROFILE_SETTING_SOURCE get_setting_source() const { return setting_source_; }

  static bool is_setting_valid(uint32_t setting_id) { return IsKnownUserSettingId(setting_id); }

  bool is_title_specific() const { return is_title_specific(static_cast<uint32_t>(setting_id_)); }

 private:
  UserSettingId setting_id_;
  X_USER_PROFILE_SETTING_SOURCE setting_source_ = X_USER_PROFILE_SETTING_SOURCE::NO_VALUE;

  static bool is_title_specific(uint32_t setting_id) { return (setting_id & 0x3F00) == 0x3F00; }
};

}  // namespace xam
}  // namespace system
}  // namespace rex

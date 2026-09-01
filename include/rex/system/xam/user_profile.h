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

#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <fmt/format.h>

#include <rex/memory.h>
#include <rex/stream.h>
#include <rex/system/xtypes.h>

namespace rex::system {
class KernelState;
}

namespace rex {
namespace system {
namespace xam {

struct X_USER_PROFILE_SETTING_DATA {
  // UserProfile::Setting::Type. Appears to be 8-in-32 field, and the upper 24
  // are not always zeroed by the game.
  uint8_t type;
  uint8_t unk_1[3];
  rex::be<uint32_t> unk_4;
  // TODO(sabretooth): not sure if this is a union, but it seems likely.
  // Haven't run into cases other than "binary data" yet.
  union {
    rex::be<int32_t> s32;
    rex::be<int64_t> s64;
    rex::be<uint32_t> u32;
    rex::be<double> f64;
    struct {
      rex::be<uint32_t> size;
      rex::be<uint32_t> ptr;
    } unicode;
    rex::be<float> f32;
    struct {
      rex::be<uint32_t> size;
      rex::be<uint32_t> ptr;
    } binary;
    rex::be<uint64_t> filetime;
  };
};
static_assert_size(X_USER_PROFILE_SETTING_DATA, 16);

struct X_USER_PROFILE_SETTING {
  rex::be<uint32_t> from;
  rex::be<uint32_t> unk04;
  union {
    rex::be<uint32_t> user_index;
    rex::be<uint64_t> xuid;
  };
  rex::be<uint32_t> setting_id;
  rex::be<uint32_t> unk14;
  union {
    uint8_t data_bytes[sizeof(X_USER_PROFILE_SETTING_DATA)];
    X_USER_PROFILE_SETTING_DATA data;
  };
};
static_assert_size(X_USER_PROFILE_SETTING, 40);

class UserProfile {
 public:
  class SettingByteStream : public stream::ByteStream {
   public:
    SettingByteStream(uint32_t ptr, uint8_t* data, size_t data_length, size_t offset = 0)
        : stream::ByteStream(data, data_length, offset), ptr_(ptr) {}

    uint32_t ptr() const { return static_cast<uint32_t>(ptr_ + offset()); }

   private:
    uint32_t ptr_;
  };
  struct Setting {
    enum class Type {
      CONTENT = 0,
      INT32 = 1,
      INT64 = 2,
      DOUBLE = 3,
      WSTRING = 4,
      FLOAT = 5,
      BINARY = 6,
      DATETIME = 7,
      UNSET = 0xFF,
    };
    union Key {
      uint32_t value;
      struct {
        uint32_t id : 14;
        uint32_t unk : 2;
        uint32_t size : 12;
        uint32_t type : 4;
      };
    };
    uint32_t setting_id;
    Type type;
    size_t size;
    bool is_set;
    uint32_t loaded_title_id;
    Setting(uint32_t setting_id, Type type, size_t size, bool is_set)
        : setting_id(setting_id), type(type), size(size), is_set(is_set), loaded_title_id(0) {}
    virtual ~Setting() = default;
    virtual void Append(X_USER_PROFILE_SETTING_DATA* data, SettingByteStream* stream) {
      (void)stream;
      data->type = static_cast<uint8_t>(type);
    }
    virtual std::vector<uint8_t> Serialize() const { return std::vector<uint8_t>(); }
    virtual void Deserialize(std::vector<uint8_t>) {}
    bool is_title_specific() const { return (setting_id & 0x3F00) == 0x3F00; }
  };
  struct Int32Setting : public Setting {
    Int32Setting(uint32_t setting_id, int32_t value)
        : Setting(setting_id, Type::INT32, 4, true), value(value) {}
    int32_t value;
    void Append(X_USER_PROFILE_SETTING_DATA* data, SettingByteStream* stream) override {
      Setting::Append(data, stream);
      data->s32 = value;
    }
  };
  struct Int64Setting : public Setting {
    Int64Setting(uint32_t setting_id, int64_t value)
        : Setting(setting_id, Type::INT64, 8, true), value(value) {}
    int64_t value;
    void Append(X_USER_PROFILE_SETTING_DATA* data, SettingByteStream* stream) override {
      Setting::Append(data, stream);
      data->s64 = value;
    }
  };
  struct DoubleSetting : public Setting {
    DoubleSetting(uint32_t setting_id, double value)
        : Setting(setting_id, Type::DOUBLE, 8, true), value(value) {}
    double value;
    void Append(X_USER_PROFILE_SETTING_DATA* data, SettingByteStream* stream) override {
      Setting::Append(data, stream);
      data->f64 = value;
    }
  };
  struct UnicodeSetting : public Setting {
    UnicodeSetting(uint32_t setting_id, const std::u16string& value)
        : Setting(setting_id, Type::WSTRING, 8, true), value(value) {}
    std::u16string value;
    void Append(X_USER_PROFILE_SETTING_DATA* data, SettingByteStream* stream) override {
      Setting::Append(data, stream);
      if (value.empty()) {
        data->unicode.size = 0;
        data->unicode.ptr = 0;
      } else {
        size_t count = value.size() + 1;
        size_t size = 2 * count;
        assert_true(size <= std::numeric_limits<uint32_t>::max());
        data->unicode.size = static_cast<uint32_t>(size);
        data->unicode.ptr = stream->ptr();
        auto buffer = reinterpret_cast<uint16_t*>(&stream->data()[stream->offset()]);
        stream->Advance(size);
        memory::copy_and_swap(buffer, (uint16_t*)value.data(), count);
      }
    }
  };
  struct FloatSetting : public Setting {
    FloatSetting(uint32_t setting_id, float value)
        : Setting(setting_id, Type::FLOAT, 4, true), value(value) {}
    float value;
    void Append(X_USER_PROFILE_SETTING_DATA* data, SettingByteStream* stream) override {
      Setting::Append(data, stream);
      data->f32 = value;
    }
  };
  struct BinarySetting : public Setting {
    BinarySetting(uint32_t setting_id) : Setting(setting_id, Type::BINARY, 8, false), value() {}
    BinarySetting(uint32_t setting_id, const std::vector<uint8_t>& value)
        : Setting(setting_id, Type::BINARY, 8, true), value(value) {}
    std::vector<uint8_t> value;
    void Append(X_USER_PROFILE_SETTING_DATA* data, SettingByteStream* stream) override {
      Setting::Append(data, stream);
      if (value.empty()) {
        data->binary.size = 0;
        data->binary.ptr = 0;
      } else {
        size_t size = value.size();
        assert_true(size <= std::numeric_limits<uint32_t>::max());
        data->binary.size = static_cast<uint32_t>(size);
        data->binary.ptr = stream->ptr();
        stream->Write(value.data(), size);
      }
    }
    std::vector<uint8_t> Serialize() const override {
      return std::vector<uint8_t>(value.data(), value.data() + value.size());
    }
    void Deserialize(std::vector<uint8_t> data) override {
      value = data;
      is_set = true;
    }
  };
  struct DateTimeSetting : public Setting {
    DateTimeSetting(uint32_t setting_id, int64_t value)
        : Setting(setting_id, Type::DATETIME, 8, true), value(value) {}
    int64_t value;
    void Append(X_USER_PROFILE_SETTING_DATA* data, SettingByteStream* stream) override {
      Setting::Append(data, stream);
      data->filetime = value;
    }
  };

  UserProfile();

  uint64_t xuid() const { return xuid_; }
  std::string name() const { return name_; }
  uint32_t signin_state() const { return 1; }
  uint32_t type() const { return 1 | 2; /* local | online profile? */ }

  void set_kernel_state(KernelState* ks) { kernel_state_ = ks; }

  void AddSetting(std::unique_ptr<Setting> setting);
  Setting* GetSetting(uint32_t setting_id);

 private:
  uint64_t xuid_;
  std::string name_;
  std::vector<std::unique_ptr<Setting>> setting_list_;
  std::unordered_map<uint32_t, Setting*> settings_;
  KernelState* kernel_state_ = nullptr;

  void LoadSetting(UserProfile::Setting*);
  void SaveSetting(UserProfile::Setting*);
};

// Real Xbox 360 XPROFILE_* setting IDs, ported verbatim from xenia's
// user_settings.h (xenia's UserSettingId enum / SettingKey helper), used
// here only to validate that a setting_id a title reads/writes is one a
// real console would recognize. Values must not be changed - they're the
// wire-format setting IDs games actually use.
constexpr uint32_t kMaxUserSettingId = 0x58;
constexpr uint32_t kMaxUserDataSize = 0x03E8;

constexpr uint32_t SettingKey(UserProfile::Setting::Type type, uint16_t size, uint16_t id) {
  return static_cast<uint32_t>(type) << 28 | size << 16 | id;
}

enum class UserSettingId : uint32_t {
  XPROFILE_PERMISSIONS = SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 0),
  XPROFILE_GAMER_TYPE = SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 1),
  XPROFILE_GAMER_YAXIS_INVERSION =
      SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 2),
  XPROFILE_OPTION_CONTROLLER_VIBRATION =
      SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 3),
  XPROFILE_GAMERCARD_ZONE = SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 4),
  XPROFILE_GAMERCARD_REGION = SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 5),
  XPROFILE_GAMERCARD_CRED = SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 6),
  XPROFILE_GAMER_PRESENCE_USER_STATE =
      SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 7),
  XPROFILE_GAMERCARD_HAS_VISION =
      SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 8),

  XPROFILE_OPTION_VOICE_MUTED =
      SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 0xC),
  XPROFILE_OPTION_VOICE_THRU_SPEAKERS =
      SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 0xD),
  XPROFILE_OPTION_VOICE_VOLUME =
      SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 0xE),

  XPROFILE_GAMERCARD_TITLES_PLAYED =
      SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 0x12),
  XPROFILE_GAMERCARD_ACHIEVEMENTS_EARNED =
      SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 0x13),
  XPROFILE_GAMER_DIFFICULTY = SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 0x15),
  XPROFILE_GAMER_CONTROL_SENSITIVITY =
      SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 0x18),
  XPROFILE_GAMER_PREFERRED_COLOR_FIRST =
      SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 0x1D),
  XPROFILE_GAMER_PREFERRED_COLOR_SECOND =
      SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 0x1E),
  XPROFILE_GAMER_ACTION_AUTO_AIM =
      SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 0x22),
  XPROFILE_GAMER_ACTION_AUTO_CENTER =
      SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 0x23),
  XPROFILE_GAMER_ACTION_MOVEMENT_CONTROL =
      SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 0x24),
  XPROFILE_GAMER_RACE_TRANSMISSION =
      SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 0x26),
  XPROFILE_GAMER_RACE_CAMERA_LOCATION =
      SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 0x27),
  XPROFILE_GAMER_RACE_BRAKE_CONTROL =
      SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 0x28),
  XPROFILE_GAMER_RACE_ACCELERATOR_CONTROL =
      SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 0x29),
  XPROFILE_GAMERCARD_TITLE_CRED_EARNED =
      SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 0x38),
  XPROFILE_GAMERCARD_TITLE_ACHIEVEMENTS_EARNED =
      SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 0x39),
  XPROFILE_GAMER_TIER = SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 0x3A),
  XPROFILE_MESSENGER_SIGNUP_STATE =
      SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 0x3B),
  XPROFILE_MESSENGER_AUTO_SIGNIN =
      SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 0x3C),
  XPROFILE_SAVE_WINDOWS_LIVE_PASSWORD =
      SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 0x3D),
  XPROFILE_FRIENDSAPP_SHOW_BUDDIES =
      SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 0x3E),
  XPROFILE_GAMERCARD_SERVICE_TYPE_FLAGS =
      SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 0x3F),
  XPROFILE_ENABLE_TUTORIALS =
      SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 0x40),
  XPROFILE_ENABLE_SUBTITLES =
      SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 0x41),
  XPROFILE_UNKNOWN_42 = SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 0x42),
  XPROFILE_UNKNOWN_43 = SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 0x43),
  XPROFILE_AIM_SENSITIVITY_XAXIS =
      SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 0x44),
  XPROFILE_AIM_SENSITIVITY_YAXIS =
      SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 0x45),
  XPROFILE_UNKNOWN_46 = SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 0x46),
  XPROFILE_TENURE_LEVEL = SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 0x47),
  XPROFILE_TENURE_MILESTONE =
      SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 0x48),

  XPROFILE_UNKNOWN_49 = SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 0x49),
  XPROFILE_SHOW_DAMAGE_INDICATORS =
      SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 0x4A),
  XPROFILE_SUBSCRIPTION_TYPE_LENGTH_IN_MONTHS =
      SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 0x4B),
  XPROFILE_SUBSCRIPTION_PAYMENT_TYPE =
      SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 0x4C),
  XPROFILE_PEC_INFO = SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 0x4D),
  // set by XamUserNuiEnableBiometric
  XPROFILE_NUI_BIOMETRIC_SIGNIN =
      SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 0x4E),
  XPROFILE_GFWL_VADNORMAL = SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 0x4F),
  XPROFILE_UNKNOWN_50 = SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 0x50),
  XPROFILE_MINIMAP_AUTOROTATE =
      SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 0x51),
  XPROFILE_BEACONS_SOCIAL_NETWORK_SHARING =
      SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 0x52),
  XPROFILE_USER_PREFERENCES =
      SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 0x53),
  // "XboxOneGamerscore" inside dash.xex
  XPROFILE_XBOXONE_GAMERSCORE =
      SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 0x57),

  WEB_EMAIL_FORMAT = SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 0x2000),
  WEB_FLAGS = SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 0x2001),
  WEB_SPAM = SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 0x2002),
  WEB_FAVORITE_GENRE = SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 0x2003),
  WEB_FAVORITE_GAME = SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 0x2004),
  WEB_FAVORITE_GAME1 = SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 0x2005),
  WEB_FAVORITE_GAME2 = SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 0x2006),
  WEB_FAVORITE_GAME3 = SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 0x2007),
  WEB_FAVORITE_GAME4 = SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 0x2008),
  WEB_FAVORITE_GAME5 = SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 0x2009),
  WEB_PLATFORMS_OWNED = SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 0x200A),
  WEB_CONNECTION_SPEED = SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 0x200B),
  WEB_FLASH = SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 0x200C),
  WEB_VIDEO_PREFERENCE = SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 0x200D),
  XPROFILE_CRUX_MEDIA_STYLE1 =
      SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 0x3EA),
  XPROFILE_CRUX_MEDIA_STYLE2 =
      SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 0x3EB),
  XPROFILE_CRUX_MEDIA_STYLE3 =
      SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 0x3EC),
  XPROFILE_CRUX_TOP_ALBUM1 =
      SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 0x3ED),
  XPROFILE_CRUX_TOP_ALBUM2 =
      SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 0x3EE),
  XPROFILE_CRUX_TOP_ALBUM3 =
      SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 0x3EF),
  XPROFILE_CRUX_TOP_ALBUM4 =
      SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 0x3F0),
  XPROFILE_CRUX_TOP_ALBUM5 =
      SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 0x3F1),
  XPROFILE_CRUX_BKGD_IMAGE =
      SettingKey(UserProfile::Setting::Type::INT32, sizeof(uint32_t), 0x3F3),

  XPROFILE_GAMERCARD_USER_LOCATION =
      SettingKey(UserProfile::Setting::Type::WSTRING, 0x52, 0x41),
  XPROFILE_GAMERCARD_USER_NAME = SettingKey(UserProfile::Setting::Type::WSTRING, 0x104, 0x40),
  XPROFILE_GAMERCARD_USER_URL = SettingKey(UserProfile::Setting::Type::WSTRING, 0x190, 0x42),
  XPROFILE_GAMERCARD_USER_BIO =
      SettingKey(UserProfile::Setting::Type::WSTRING, kMaxUserDataSize, 0x43),

  XPROFILE_CRUX_BIO = SettingKey(UserProfile::Setting::Type::WSTRING, kMaxUserDataSize, 0x3FA),
  XPROFILE_CRUX_BG_SMALL_PRIVATE =
      SettingKey(UserProfile::Setting::Type::WSTRING, 0x64, 0x3FB),
  XPROFILE_CRUX_BG_LARGE_PRIVATE =
      SettingKey(UserProfile::Setting::Type::WSTRING, 0x64, 0x3FC),
  XPROFILE_CRUX_BG_SMALL_PUBLIC =
      SettingKey(UserProfile::Setting::Type::WSTRING, 0x64, 0x3FD),
  XPROFILE_CRUX_BG_LARGE_PUBLIC =
      SettingKey(UserProfile::Setting::Type::WSTRING, 0x64, 0x3FE),

  XPROFILE_GAMERCARD_PICTURE_KEY = SettingKey(UserProfile::Setting::Type::WSTRING, 0x64, 0xF),
  XPROFILE_GAMERCARD_PERSONAL_PICTURE =
      SettingKey(UserProfile::Setting::Type::WSTRING, 0x64, 0x10),
  XPROFILE_GAMERCARD_MOTTO = SettingKey(UserProfile::Setting::Type::WSTRING, 0x2C, 0x11),
  XPROFILE_GFWL_RECDEVICEDESC = SettingKey(UserProfile::Setting::Type::WSTRING, 200, 0x49),

  XPROFILE_GFWL_PLAYDEVICEDESC = SettingKey(UserProfile::Setting::Type::WSTRING, 200, 0x4B),
  XPROFILE_CRUX_MEDIA_PICTURE = SettingKey(UserProfile::Setting::Type::WSTRING, 0x64, 0x3E8),
  XPROFILE_CRUX_MEDIA_MOTTO = SettingKey(UserProfile::Setting::Type::WSTRING, 0x100, 0x3F6),

  XPROFILE_GAMERCARD_REP = SettingKey(UserProfile::Setting::Type::FLOAT, sizeof(float), 0xB),
  XPROFILE_GFWL_VOLUMELEVEL = SettingKey(UserProfile::Setting::Type::FLOAT, sizeof(float), 0x4C),
  XPROFILE_GFWL_RECLEVEL = SettingKey(UserProfile::Setting::Type::FLOAT, sizeof(float), 0x4D),
  XPROFILE_GFWL_PLAYDEVICE = SettingKey(UserProfile::Setting::Type::BINARY, 0x10, 0x4A),

  XPROFILE_VIDEO_METADATA = SettingKey(UserProfile::Setting::Type::BINARY, 0x20, 0x4A),

  XPROFILE_CRUX_OFFLINE_ID = SettingKey(UserProfile::Setting::Type::BINARY, 0x34, 0x3F2),

  XPROFILE_UNK_61180050 = SettingKey(UserProfile::Setting::Type::BINARY, 280, 0x50),

  XPROFILE_JUMP_IN_LIST = SettingKey(UserProfile::Setting::Type::BINARY, kMaxUserDataSize, 0x51),

  XPROFILE_GAMERCARD_PARTY_ADDR = SettingKey(UserProfile::Setting::Type::BINARY, 0x62, 0x54),

  XPROFILE_CRUX_TOP_MUSIC = SettingKey(UserProfile::Setting::Type::BINARY, 0xA8, 0x3F5),

  XPROFILE_CRUX_TOP_MEDIAID1 = SettingKey(UserProfile::Setting::Type::BINARY, 0x10, 0x3F7),
  XPROFILE_CRUX_TOP_MEDIAID2 = SettingKey(UserProfile::Setting::Type::BINARY, 0x10, 0x3F8),
  XPROFILE_CRUX_TOP_MEDIAID3 = SettingKey(UserProfile::Setting::Type::BINARY, 0x10, 0x3F9),

  XPROFILE_GAMERCARD_AVATAR_INFO_1 =
      SettingKey(UserProfile::Setting::Type::BINARY, kMaxUserDataSize, 0x44),
  XPROFILE_GAMERCARD_AVATAR_INFO_2 =
      SettingKey(UserProfile::Setting::Type::BINARY, kMaxUserDataSize, 0x45),
  XPROFILE_GAMERCARD_PARTY_INFO = SettingKey(UserProfile::Setting::Type::BINARY, 0x100, 0x46),

  XPROFILE_TITLE_SPECIFIC1 =
      SettingKey(UserProfile::Setting::Type::BINARY, kMaxUserDataSize, 0x3FFF),
  XPROFILE_TITLE_SPECIFIC2 =
      SettingKey(UserProfile::Setting::Type::BINARY, kMaxUserDataSize, 0x3FFE),
  XPROFILE_TITLE_SPECIFIC3 =
      SettingKey(UserProfile::Setting::Type::BINARY, kMaxUserDataSize, 0x3FFD),

  XPROFILE_CRUX_LAST_CHANGE_TIME =
      SettingKey(UserProfile::Setting::Type::DATETIME, sizeof(uint64_t), 0x3F4),
  // aka ProfileDateTimeCreated?
  XPROFILE_TENURE_NEXT_MILESTONE_DATE =
      SettingKey(UserProfile::Setting::Type::DATETIME, sizeof(uint64_t), 0x49),
  // named "LastOnLIVE" in Velocity
  XPROFILE_LAST_LIVE_SIGNIN =
      SettingKey(UserProfile::Setting::Type::DATETIME, sizeof(uint64_t), 0x4F),
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

// True if setting_id is either a "core" setting (id sub-field < kMaxUserSettingId,
// matching xenia's is_setting_valid) or one of the extended settings above.
inline bool IsKnownUserSettingId(uint32_t setting_id) {
  UserProfile::Setting::Key key;
  key.value = setting_id;
  if (key.id < kMaxUserSettingId) {
    return true;
  }
  return std::find(known_settings.cbegin(), known_settings.cend(),
                   static_cast<UserSettingId>(setting_id)) != known_settings.cend();
}

}  // namespace xam
}  // namespace system
}  // namespace rex

// fmt formatter for UserProfile::Setting::Type
template <>
struct fmt::formatter<rex::system::xam::UserProfile::Setting::Type> : fmt::formatter<int> {
  template <typename FormatContext>
  auto format(rex::system::xam::UserProfile::Setting::Type t, FormatContext& ctx) const {
    return fmt::formatter<int>::format(static_cast<int>(t), ctx);
  }
};

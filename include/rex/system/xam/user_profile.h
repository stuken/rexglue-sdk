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

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <fmt/format.h>

#include <rex/memory.h>
#include <rex/system/achievement_store.h>
#include <rex/system/util/gpd_info_profile.h>
#include <rex/system/util/gpd_info_title.h>
#include <rex/system/xam/user_settings.h>
#include <rex/system/xtypes.h>

namespace rex::system {
class KernelState;
}

namespace rex {
namespace system {
namespace xam {

// Wire-format structs, kept as-is - verified byte-compatible with the real
// console's X_USER_DATA/X_USER_PROFILE_SETTING layout during this session
// (the unk04/unk14 fields are compiler-inserted alignment padding, not
// unknown data; see XAM_PORT_AUDIT.md's Phase 1 session log).
struct X_USER_PROFILE_SETTING_DATA {
  // X_USER_DATA_TYPE, stored as a raw byte since it's part of the wire
  // format read directly from/written directly to guest memory.
  uint8_t type;
  uint8_t unk_1[3];
  rex::be<uint32_t> unk_4;
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

// Single-user, GPD-backed profile. Replaces the old ad-hoc
// UserProfile::Setting hierarchy (item 1.3) with real UserSetting/.gpd
// persistence, and folds the (single-user-scoped) responsibilities of
// xenia's UserTracker directly onto this class (items 2.2/2.4) - see
// XAM_PORT_AUDIT.md §3 for why a full XUID-keyed tracker isn't needed here.
class UserProfile {
 public:
  UserProfile();

  uint64_t xuid() const { return xuid_; }
  std::string name() const { return name_; }
  uint32_t signin_state() const { return 1; }
  uint32_t type() const { return 1 | 2; /* local | online profile? */ }

  void set_kernel_state(KernelState* ks);

  // Reads a setting: current title's GPD first, then the dashboard GPD, then
  // a real-console default if neither has ever stored it. Returns nullopt
  // only for a setting_id that isn't a recognized XPROFILE_* ID at all.
  std::optional<UserSetting> GetSetting(uint32_t setting_id);

  // Writes a setting into the appropriate GPD (the current title's GPD for
  // title-specific IDs, the dashboard GPD otherwise) and persists it to
  // disk immediately. No-op (with a warning) for an unrecognized setting_id.
  void AddSetting(const UserSetting& setting);

  // Adds/refreshes this title's dashboard played-titles entry and ensures
  // its GPD is loaded. Call once per title load.
  void TrackTitle(uint32_t title_id, const std::string& title_name, uint32_t achievement_count,
                  uint32_t total_gamerscore);

  // Persists a real GPD achievement entry for a just-unlocked achievement
  // and updates the dashboard's summary settings (GAMERCARD_CRED /
  // ACHIEVEMENTS_EARNED) and per-title counters, mirroring xenia's
  // UserTracker::UnlockAchievement. No-op if the achievement was already
  // recorded as unlocked in the GPD.
  void OnAchievementUnlocked(const AchievementInfo& info, uint64_t unlock_filetime);

  // Dashboard played-titles list, for XamUserCreateTitlesPlayedEnumerator.
  // Pointers are valid only as long as this UserProfile's dashboard GPD
  // isn't mutated (e.g. by TrackTitle/OnAchievementUnlocked) - callers
  // should copy any entry they need to keep past that.
  std::vector<const util::X_XDBF_GPD_TITLE_PLAYED*> GetPlayedTitles() const {
    return dashboard_gpd_.GetTitlesInfo();
  }
  std::u16string GetTitleName(uint32_t title_id) const { return dashboard_gpd_.GetTitleName(title_id); }

 private:
  uint64_t xuid_;
  std::string name_;
  KernelState* kernel_state_ = nullptr;

  util::GpdInfoProfile dashboard_gpd_;
  std::unordered_map<uint32_t, util::GpdInfoTitle> games_gpd_;

  util::GpdInfoTitle& EnsureTitleGpd(uint32_t title_id);
  void UpdateSettingValue(uint32_t title_id, UserSettingId setting_id, int32_t difference);

  void LoadProfileGpds();
  std::filesystem::path GpdPath(uint32_t title_id) const;
  std::vector<uint8_t> LoadGpdBytes(uint32_t title_id) const;
  bool WriteGpd(uint32_t title_id);
};

}  // namespace xam
}  // namespace system
}  // namespace rex

// fmt formatter for X_USER_DATA_TYPE (the raw byte in
// X_USER_PROFILE_SETTING_DATA::type is cast to this at call sites for
// logging).
template <>
struct fmt::formatter<rex::system::xam::X_USER_DATA_TYPE> : fmt::formatter<int> {
  template <typename FormatContext>
  auto format(rex::system::xam::X_USER_DATA_TYPE t, FormatContext& ctx) const {
    return fmt::formatter<int>::format(static_cast<int>(t), ctx);
  }
};

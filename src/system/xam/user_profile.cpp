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

#include <chrono>
#include <cstdio>
#include <sstream>

#include <fmt/format.h>

#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xam/user_profile.h>

namespace rex {
namespace system {
namespace xam {

namespace {
// Xbox FILETIME: 100ns intervals since 1601-01-01, matching
// AchievementManager::CurrentFileTime() (kept duplicated rather than shared -
// the two call sites predate each other and neither owns a natural home for
// this one-off conversion).
uint64_t CurrentGuestFileTime() {
  using namespace std::chrono;
  auto now = system_clock::now().time_since_epoch();
  auto intervals = duration_cast<duration<int64_t, std::ratio<1, 10'000'000>>>(now);
  constexpr uint64_t kEpochDelta = 116'444'736'000'000'000ULL;
  return static_cast<uint64_t>(intervals.count()) + kEpochDelta;
}
}  // namespace

UserProfile::UserProfile() {
  // 58410A1F checks the user XUID against a mask of 0x00C0000000000000 (3<<54),
  // if non-zero, it prevents the user from playing the game.
  // "You do not have permissions to perform this operation."
  xuid_ = 0xB13EBABEBABEBABE;
  name_ = "User";
}

void UserProfile::set_kernel_state(KernelState* ks) {
  kernel_state_ = ks;
  LoadProfileGpds();
}

void UserProfile::LoadProfileGpds() {
  auto dashboard_bytes = LoadGpdBytes(util::kDashboardID);
  if (!dashboard_bytes.empty()) {
    util::GpdInfoProfile loaded(dashboard_bytes);
    if (loaded.IsValid()) {
      dashboard_gpd_ = std::move(loaded);
    }
  }

  for (const auto* title_data : dashboard_gpd_.GetTitlesInfo()) {
    const uint32_t title_id = uint32_t(title_data->title_id);
    auto title_bytes = LoadGpdBytes(title_id);
    if (title_bytes.empty()) {
      continue;
    }
    util::GpdInfoTitle loaded(title_id, title_bytes);
    if (loaded.IsValid()) {
      games_gpd_.emplace(title_id, std::move(loaded));
    }
  }
}

std::filesystem::path UserProfile::GpdPath(uint32_t title_id) const {
  if (!kernel_state_ || !kernel_state_->emulator()) {
    return {};
  }
  const auto root = kernel_state_->emulator()->user_data_root();
  if (root.empty()) {
    return {};
  }
  const std::string filename =
      title_id == util::kDashboardID ? "dashboard.gpd" : fmt::format("{:08X}.gpd", title_id);
  return root / "profile" / filename;
}

std::vector<uint8_t> UserProfile::LoadGpdBytes(uint32_t title_id) const {
  auto path = GpdPath(title_id);
  if (path.empty()) {
    return {};
  }

  auto file = rex::filesystem::OpenFile(path, "rb");
  if (!file) {
    return {};
  }

  fseek(file, 0, SEEK_END);
  const long size = ftell(file);
  fseek(file, 0, SEEK_SET);

  std::vector<uint8_t> data(size > 0 ? size_t(size) : 0);
  if (!data.empty()) {
    fread(data.data(), 1, data.size(), file);
  }
  fclose(file);
  return data;
}

bool UserProfile::WriteGpd(uint32_t title_id) {
  const util::GpdInfo* gpd = nullptr;
  if (title_id == util::kDashboardID) {
    gpd = &dashboard_gpd_;
  } else {
    auto it = games_gpd_.find(title_id);
    if (it == games_gpd_.end()) {
      return false;
    }
    gpd = &it->second;
  }

  auto path = GpdPath(title_id);
  if (path.empty()) {
    return false;
  }

  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);

  const auto data = gpd->Serialize();
  auto file = rex::filesystem::OpenFile(path, "wb");
  if (!file) {
    REXSYS_WARN("UserProfile: failed to write GPD {}", path.string());
    return false;
  }
  fwrite(data.data(), 1, data.size(), file);
  fclose(file);
  return true;
}

util::GpdInfoTitle& UserProfile::EnsureTitleGpd(uint32_t title_id) {
  auto it = games_gpd_.find(title_id);
  if (it != games_gpd_.end()) {
    return it->second;
  }

  auto bytes = LoadGpdBytes(title_id);
  auto inserted =
      games_gpd_.emplace(title_id, bytes.empty() ? util::GpdInfoTitle(title_id)
                                                 : util::GpdInfoTitle(title_id, bytes));
  if (!inserted.second || !inserted.first->second.IsValid()) {
    inserted.first->second = util::GpdInfoTitle(title_id);
  }
  return inserted.first->second;
}

void UserProfile::TrackTitle(uint32_t title_id, const std::string& title_name,
                             uint32_t achievement_count, uint32_t total_gamerscore) {
  if (!title_id) {
    return;
  }

  auto* title_info = dashboard_gpd_.GetTitleInfo(title_id);
  if (!title_info) {
    dashboard_gpd_.AddNewTitle(title_id, title_name, achievement_count, total_gamerscore);
    UpdateSettingValue(util::kDashboardID, UserSettingId::XPROFILE_GAMERCARD_TITLES_PLAYED, 1);
    title_info = dashboard_gpd_.GetTitleInfo(title_id);
  } else if (uint32_t(title_info->achievements_count) != achievement_count) {
    // Achievement catalog changed since this title was last tracked (e.g. an
    // edited achievements.toml) - keep the dashboard's summary in sync.
    util::X_XDBF_GPD_TITLE_PLAYED updated = *title_info;
    updated.achievements_count = achievement_count;
    updated.gamerscore_total = total_gamerscore;
    dashboard_gpd_.UpdateTitleInfo(title_id, updated);
    title_info = dashboard_gpd_.GetTitleInfo(title_id);
  }

  if (title_info) {
    title_info->last_played = CurrentGuestFileTime();
  }

  EnsureTitleGpd(title_id);
  WriteGpd(util::kDashboardID);
}

void UserProfile::OnAchievementUnlocked(const AchievementInfo& info, uint64_t unlock_filetime) {
  if (!kernel_state_) {
    return;
  }
  const uint32_t title_id = kernel_state_->title_id();
  if (!title_id) {
    return;
  }

  auto* title_info = dashboard_gpd_.GetTitleInfo(title_id);
  if (!title_info) {
    // TrackTitle() should always run before any achievement can unlock.
    REXSYS_WARN("UserProfile::OnAchievementUnlocked: title {:08X} isn't tracked yet", title_id);
    return;
  }

  auto& title_gpd = EnsureTitleGpd(title_id);
  title_gpd.AddAchievement(info);

  auto* achievement = title_gpd.GetAchievementEntry(info.id);
  if (!achievement || achievement->is_achievement_unlocked()) {
    return;
  }

  achievement->flags =
      uint32_t(achievement->flags) | static_cast<uint32_t>(util::AchievementFlags::kAchieved);
  achievement->unlock_time = unlock_filetime;

  title_info->achievements_unlocked = uint32_t(title_info->achievements_unlocked) + 1;
  title_info->gamerscore_earned = uint32_t(title_info->gamerscore_earned) + info.gamerscore;

  UpdateSettingValue(util::kDashboardID, UserSettingId::XPROFILE_GAMERCARD_CRED,
                     static_cast<int32_t>(info.gamerscore));
  UpdateSettingValue(util::kDashboardID, UserSettingId::XPROFILE_GAMERCARD_ACHIEVEMENTS_EARNED, 1);
  UpdateSettingValue(title_id, UserSettingId::XPROFILE_GAMERCARD_TITLE_CRED_EARNED,
                     static_cast<int32_t>(info.gamerscore));
  UpdateSettingValue(title_id, UserSettingId::XPROFILE_GAMERCARD_TITLE_ACHIEVEMENTS_EARNED, 1);

  WriteGpd(title_id);
  WriteGpd(util::kDashboardID);
}

void UserProfile::UpdateSettingValue(uint32_t title_id, UserSettingId setting_id,
                                     int32_t difference) {
  util::GpdInfo* gpd = title_id == util::kDashboardID
                          ? static_cast<util::GpdInfo*>(&dashboard_gpd_)
                          : static_cast<util::GpdInfo*>(&EnsureTitleGpd(title_id));

  int32_t new_value = difference;
  if (auto* header = gpd->GetSetting(static_cast<uint32_t>(setting_id))) {
    new_value = int32_t(header->base_data.data.s32) + difference;
  }

  UserSetting updated(setting_id, UserDataTypes(new_value));
  gpd->UpsertSetting(updated);
}

std::optional<UserSetting> UserProfile::GetSetting(uint32_t setting_id) {
  const uint32_t title_id = kernel_state_ ? kernel_state_->title_id() : 0;

  if (title_id) {
    auto it = games_gpd_.find(title_id);
    if (it != games_gpd_.end()) {
      if (auto* header = it->second.GetSetting(setting_id)) {
        return UserSetting(setting_id, &header->base_data, it->second.GetSettingData(setting_id),
                           X_USER_PROFILE_SETTING_SOURCE::TITLE);
      }
    }
  }

  if (auto* header = dashboard_gpd_.GetSetting(setting_id)) {
    return UserSetting(setting_id, &header->base_data, dashboard_gpd_.GetSettingData(setting_id),
                       X_USER_PROFILE_SETTING_SOURCE::TITLE);
  }

  if (!UserSetting::is_setting_valid(setting_id)) {
    REXSYS_DEBUG("Requested profile setting {:08X} is not a recognized XPROFILE_* ID", setting_id);
    return std::nullopt;
  }

  return UserSetting::GetDefaultSetting(setting_id);
}

void UserProfile::AddSetting(const UserSetting& setting) {
  const uint32_t setting_id = setting.get_setting_id();
  if (!UserSetting::is_setting_valid(setting_id)) {
    REXSYS_WARN("Rejecting write to unrecognized profile setting {:08X}", setting_id);
    return;
  }

  const uint32_t title_id = kernel_state_ ? kernel_state_->title_id() : 0;

  if (setting.is_title_specific() && title_id) {
    EnsureTitleGpd(title_id).UpsertSetting(setting);
    WriteGpd(title_id);
  } else {
    dashboard_gpd_.UpsertSetting(setting);
    WriteGpd(util::kDashboardID);
  }
}

}  // namespace xam
}  // namespace system
}  // namespace rex

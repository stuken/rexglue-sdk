/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */
#pragma once

#include <string>
#include <vector>

#include <rex/system/achievement_store.h>
#include <rex/system/util/gpd_info.h>

namespace rex {
namespace system {
namespace util {

// Per-title GPD: achievement definitions/unlock state for one title.
// Ported from xenia's GpdInfoTitle (kernel/xam/xdbf/gpd_info_title.h/.cc),
// adapted to source achievement metadata from rexglue's AchievementInfo
// (already loaded from the title's XDBF resource + achievements.toml, see
// KernelState::LoadAchievementsData) rather than a live SpaInfo/XDBF read.
class GpdInfoTitle : public GpdInfo {
 public:
  GpdInfoTitle() : GpdInfo(uint32_t(-1)) {}
  explicit GpdInfoTitle(uint32_t title_id) : GpdInfo(title_id) {}
  GpdInfoTitle(uint32_t title_id, const std::vector<uint8_t>& buffer) : GpdInfo(title_id, buffer) {}
  ~GpdInfoTitle() = default;

  std::vector<uint32_t> GetAchievementsIds() const;

  // No-ops if the achievement already has an entry (matches xenia: an
  // achievement's catalog data is written once, unlock state is a later
  // in-place field update via GetAchievementEntry()).
  void AddAchievement(const AchievementInfo& info);

  X_XDBF_GPD_ACHIEVEMENT* GetAchievementEntry(uint32_t id);
  std::u16string GetAchievementTitle(uint32_t id);
  std::u16string GetAchievementDescription(uint32_t id);
  std::u16string GetAchievementUnachievedDescription(uint32_t id);

  uint32_t GetTotalGamerscore();
  uint32_t GetGamerscore();
  uint32_t GetAchievementCount();
  uint32_t GetUnlockedAchievementCount();

 private:
  const char16_t* GetAchievementTitlePtr(uint32_t id);
  const char16_t* GetAchievementDescriptionPtr(uint32_t id);
  const char16_t* GetAchievementUnachievedDescriptionPtr(uint32_t id);
};

}  // namespace util
}  // namespace system
}  // namespace rex

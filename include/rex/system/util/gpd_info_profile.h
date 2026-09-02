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

#include <rex/system/util/gpd_info.h>

namespace rex {
namespace system {
namespace util {

// Sentinel title ID for the dashboard/profile GPD itself, matching xenia's
// kDashboardID and the real Xbox 360 profile format.
inline constexpr uint32_t kDashboardID = 0xFFFE07D1;

// Dashboard GPD: the played-titles list plus profile-wide settings. Ported
// from xenia's GpdInfoProfile (kernel/xam/xdbf/gpd_info_profile.h/.cc).
class GpdInfoProfile : public GpdInfo {
 public:
  GpdInfoProfile() : GpdInfo(kDashboardID) {}
  explicit GpdInfoProfile(const std::vector<uint8_t>& buffer) : GpdInfo(kDashboardID, buffer) {}
  ~GpdInfoProfile() = default;

  void AddNewTitle(uint32_t title_id, const std::string& title_name, uint32_t achievement_count,
                   uint32_t total_gamerscore);
  bool RemoveTitle(uint32_t title_id);
  void UpdateTitleInfo(uint32_t title_id, const X_XDBF_GPD_TITLE_PLAYED& title_data);

  std::vector<const X_XDBF_GPD_TITLE_PLAYED*> GetTitlesInfo() const;
  X_XDBF_GPD_TITLE_PLAYED* GetTitleInfo(uint32_t title_id);

  std::u16string GetTitleName(uint32_t title_id) const;
};

}  // namespace util
}  // namespace system
}  // namespace rex

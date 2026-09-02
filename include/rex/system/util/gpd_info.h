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

#include <span>
#include <string>
#include <vector>

#include <rex/memory.h>
#include <rex/system/util/xdbf_io.h>
#include <rex/system/xam/user_data.h>

namespace rex {
namespace system {
namespace xam {
class UserSetting;
}  // namespace xam

namespace util {

// GPD section IDs. Distinct from XdbfSection (xdbf_utils.h), which navigates
// a title's read-only SPA/XDBF resource - a .gpd file is a different use of
// the same outer XDBF container format.
enum class GpdSection : uint16_t {
  kAchievement = 0x1,  // Title GPD only.
  kImage = 0x2,
  kSetting = 0x3,
  kTitle = 0x4,  // Dashboard GPD only.
  kString = 0x5,
  kProtectedAchievement = 0x6,  // GFWL only.
};

enum class AchievementFlags : uint32_t {
  kTypeMask = 0x7,
  kShowUnachieved = 0x8,
  kAchievedOnline = 0x10000,
  kAchieved = 0x20000,
  kNotAchievable = 0x40000,
  kWasNotAchievable = 0x80000,
  kPlatformMask = 0x700000,
  kColorizable = 0x1000000,
};

struct X_XDBF_AVATARAWARDS_COUNTER {
  uint8_t earned;
  uint8_t possible;
};
static_assert(sizeof(X_XDBF_AVATARAWARDS_COUNTER) == 2);

#pragma pack(push, 1)
struct X_XDBF_GPD_ACHIEVEMENT {
  rex::be<uint32_t> magic;
  rex::be<uint32_t> id;
  rex::be<uint32_t> image_id;
  rex::be<uint32_t> gamerscore;
  rex::be<uint32_t> flags;
  rex::be<uint64_t> unlock_time;
  // Followed in the entry by: wchar_t* title, description, unlocked_description.

  bool is_achievement_unlocked() const {
    return uint32_t(flags) & static_cast<uint32_t>(AchievementFlags::kAchieved);
  }
};
static_assert(sizeof(X_XDBF_GPD_ACHIEVEMENT) == 0x1C);

// Real Xbox 360 on-disk title-played record. Avatar award counters are kept
// (zeroed) purely for byte-compatible layout - rexglue has no avatar system
// (see XAM_PORT_AUDIT.md item 6.1, closed as not a target).
struct X_XDBF_GPD_TITLE_PLAYED {
  rex::be<uint32_t> title_id;
  rex::be<uint32_t> achievements_count;
  rex::be<uint32_t> achievements_unlocked;
  rex::be<uint32_t> gamerscore_total;
  rex::be<uint32_t> gamerscore_earned;
  rex::be<uint16_t> online_achievement_count;

  X_XDBF_AVATARAWARDS_COUNTER all_avatar_awards;
  X_XDBF_AVATARAWARDS_COUNTER male_avatar_awards;
  X_XDBF_AVATARAWARDS_COUNTER female_avatar_awards;
  // 1 - offline unlocked, must be synced. 2 - achievement unlocked, image
  // missing. 0x10 - avatar unlocked, avatar missing.
  rex::be<uint32_t> flags;
  rex::be<uint64_t> last_played;  // Xbox FILETIME.
  // Followed in the entry by: wchar_t title_name[] (variable length).

  bool include_in_enumerator() const { return achievements_count != 0; }
};
static_assert(sizeof(X_XDBF_GPD_TITLE_PLAYED) == 0x28);

struct X_XDBF_GPD_SETTING_HEADER {
  rex::be<uint32_t> setting_id;
  rex::be<uint32_t> unknown_1;
  xam::X_USER_DATA_WIRE base_data;

  bool RequiresBuffer() const {
    return base_data.type == xam::X_USER_DATA_TYPE::BINARY ||
           base_data.type == xam::X_USER_DATA_TYPE::WSTRING;
  }
};
static_assert(sizeof(X_XDBF_GPD_SETTING_HEADER) == 0x18);
#pragma pack(pop)

// Writable per-user GPD (Game Profile Data) container: achievements,
// settings, images, and (dashboard GPD only) the title-played list. Ported
// from xenia's GpdInfo (kernel/xam/xdbf/gpd_info.h/.cc). Single-user scoped:
// one instance per (dashboard | title) file, owned directly by UserProfile.
class GpdInfo : public XdbfFile {
 public:
  GpdInfo();
  explicit GpdInfo(uint32_t title_id);
  GpdInfo(uint32_t title_id, const std::vector<uint8_t>& buffer);
  ~GpdInfo() = default;

  // A GPD always contains at least one free entry that marks EOF.
  bool IsValid() const { return !free_entries_.empty(); }
  uint32_t title_id() const { return title_id_; }

  std::span<const uint8_t> GetImage(uint64_t id) const;
  void AddImage(uint32_t id, std::span<const uint8_t> image_data);

  X_XDBF_GPD_SETTING_HEADER* GetSetting(uint32_t id);
  std::span<const uint8_t> GetSettingData(uint32_t id);
  void UpsertSetting(const xam::UserSetting& setting_data);

  std::u16string GetString(uint32_t id) const;
  void AddString(uint32_t id, const std::u16string& string_data);

  std::vector<uint8_t> Serialize() const;

 protected:
  static bool IsSyncEntry(const XdbfEntry* entry);
  static bool IsEntryOfSection(const XdbfEntry* entry, GpdSection section);

  void UpsertEntry(XdbfEntry* entry);
  void DeleteEntry(const XdbfEntry* entry);

  uint32_t FindFreeLocation(uint32_t entry_size);

 private:
  static constexpr uint32_t kBaseEntryCount = 512;

  uint32_t title_id_ = 0;

  void InsertEntry(XdbfEntry* entry);
  std::vector<const XdbfEntry*> GetSortedEntries() const;

  void ResizeEntryTable();
  void MarkSpaceAsFree(uint32_t offset, uint32_t size);
};

}  // namespace util
}  // namespace system
}  // namespace rex

/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */
#include <rex/system/util/gpd_info_title.h>

#include <rex/memory/utils.h>
#include <rex/string.h>
#include <rex/string/utf8.h>

namespace rex {
namespace system {
namespace util {

namespace {
uint32_t U16BytesWithTerminator(size_t char_count) {
  return static_cast<uint32_t>((char_count + 1) * sizeof(char16_t));
}
}  // namespace

X_XDBF_GPD_ACHIEVEMENT* GpdInfoTitle::GetAchievementEntry(uint32_t id) {
  XdbfEntry* entry = GetEntry(static_cast<uint16_t>(GpdSection::kAchievement), id);
  if (!entry) {
    return nullptr;
  }
  return reinterpret_cast<X_XDBF_GPD_ACHIEVEMENT*>(entry->data.data());
}

const char16_t* GpdInfoTitle::GetAchievementTitlePtr(uint32_t id) {
  X_XDBF_GPD_ACHIEVEMENT* achievement = GetAchievementEntry(id);
  if (!achievement) {
    return nullptr;
  }
  return reinterpret_cast<const char16_t*>(achievement + 1);
}

const char16_t* GpdInfoTitle::GetAchievementDescriptionPtr(uint32_t id) {
  const char16_t* title_ptr = GetAchievementTitlePtr(id);
  if (!title_ptr) {
    return nullptr;
  }
  return title_ptr + GetAchievementTitle(id).length() + 1;
}

const char16_t* GpdInfoTitle::GetAchievementUnachievedDescriptionPtr(uint32_t id) {
  const char16_t* description_ptr = GetAchievementDescriptionPtr(id);
  if (!description_ptr) {
    return nullptr;
  }
  return description_ptr + GetAchievementDescription(id).length() + 1;
}

std::u16string GpdInfoTitle::GetAchievementTitle(uint32_t id) {
  const char16_t* ptr = GetAchievementTitlePtr(id);
  return ptr ? memory::load_and_swap<std::u16string>(ptr) : std::u16string();
}

std::u16string GpdInfoTitle::GetAchievementDescription(uint32_t id) {
  const char16_t* ptr = GetAchievementDescriptionPtr(id);
  return ptr ? memory::load_and_swap<std::u16string>(ptr) : std::u16string();
}

std::u16string GpdInfoTitle::GetAchievementUnachievedDescription(uint32_t id) {
  const char16_t* ptr = GetAchievementUnachievedDescriptionPtr(id);
  return ptr ? memory::load_and_swap<std::u16string>(ptr) : std::u16string();
}

std::vector<uint32_t> GpdInfoTitle::GetAchievementsIds() const {
  std::vector<uint32_t> ids;
  for (const auto& entry : entries_) {
    if (IsSyncEntry(&entry) || !IsEntryOfSection(&entry, GpdSection::kAchievement)) {
      continue;
    }
    ids.push_back(static_cast<uint32_t>(uint64_t(entry.info.id)));
  }
  return ids;
}

void GpdInfoTitle::AddAchievement(const AchievementInfo& info) {
  if (GetEntry(static_cast<uint16_t>(GpdSection::kAchievement), info.id)) {
    return;
  }

  const std::u16string label = rex::string::to_utf16(info.label);
  const std::u16string description = rex::string::to_utf16(info.description);
  const std::u16string unachieved = rex::string::to_utf16(info.unachieved_description);

  X_XDBF_GPD_ACHIEVEMENT header = {};
  header.magic = sizeof(X_XDBF_GPD_ACHIEVEMENT);
  header.id = info.id;
  header.image_id = info.image_id;
  header.gamerscore = info.gamerscore;
  header.flags = info.flags;
  header.unlock_time = 0;

  const uint32_t strings_size = U16BytesWithTerminator(label.size()) +
                                U16BytesWithTerminator(description.size()) +
                                U16BytesWithTerminator(unachieved.size());
  const uint32_t entry_size = sizeof(X_XDBF_GPD_ACHIEVEMENT) + strings_size;

  XdbfEntry new_entry(info.id, static_cast<uint16_t>(GpdSection::kAchievement), entry_size);

  uint8_t* write_ptr = new_entry.data.data();
  std::memcpy(write_ptr, &header, sizeof(X_XDBF_GPD_ACHIEVEMENT));
  write_ptr += sizeof(X_XDBF_GPD_ACHIEVEMENT);

  rex::string::copy_and_swap_truncating(reinterpret_cast<char16_t*>(write_ptr), label, label.length() + 1);
  write_ptr += U16BytesWithTerminator(label.size());

  rex::string::copy_and_swap_truncating(reinterpret_cast<char16_t*>(write_ptr), description,
                                        description.length() + 1);
  write_ptr += U16BytesWithTerminator(description.size());

  rex::string::copy_and_swap_truncating(reinterpret_cast<char16_t*>(write_ptr), unachieved,
                                        unachieved.length() + 1);

  UpsertEntry(&new_entry);
}

uint32_t GpdInfoTitle::GetTotalGamerscore() {
  uint32_t gamerscore = 0;
  for (const uint32_t id : GetAchievementsIds()) {
    gamerscore += uint32_t(GetAchievementEntry(id)->gamerscore);
  }
  return gamerscore;
}

uint32_t GpdInfoTitle::GetGamerscore() {
  uint32_t gamerscore = 0;
  for (const uint32_t id : GetAchievementsIds()) {
    const auto* entry = GetAchievementEntry(id);
    if (entry->is_achievement_unlocked()) {
      gamerscore += uint32_t(entry->gamerscore);
    }
  }
  return gamerscore;
}

uint32_t GpdInfoTitle::GetAchievementCount() {
  return static_cast<uint32_t>(GetAchievementsIds().size());
}

uint32_t GpdInfoTitle::GetUnlockedAchievementCount() {
  uint32_t count = 0;
  for (const uint32_t id : GetAchievementsIds()) {
    if (GetAchievementEntry(id)->is_achievement_unlocked()) {
      ++count;
    }
  }
  return count;
}

}  // namespace util
}  // namespace system
}  // namespace rex

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
#include <rex/system/util/gpd_info_profile.h>

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

std::vector<const X_XDBF_GPD_TITLE_PLAYED*> GpdInfoProfile::GetTitlesInfo() const {
  std::vector<const X_XDBF_GPD_TITLE_PLAYED*> entries;
  for (const auto& entry : entries_) {
    if (IsSyncEntry(&entry) || !IsEntryOfSection(&entry, GpdSection::kTitle)) {
      continue;
    }
    entries.push_back(reinterpret_cast<const X_XDBF_GPD_TITLE_PLAYED*>(entry.data.data()));
  }
  return entries;
}

X_XDBF_GPD_TITLE_PLAYED* GpdInfoProfile::GetTitleInfo(uint32_t title_id) {
  for (auto& entry : entries_) {
    if (IsSyncEntry(&entry) || !IsEntryOfSection(&entry, GpdSection::kTitle)) {
      continue;
    }
    if (static_cast<uint32_t>(uint64_t(entry.info.id)) != title_id) {
      continue;
    }
    return reinterpret_cast<X_XDBF_GPD_TITLE_PLAYED*>(entry.data.data());
  }
  return nullptr;
}

std::u16string GpdInfoProfile::GetTitleName(uint32_t title_id) const {
  const XdbfEntry* entry = GetEntry(static_cast<uint16_t>(GpdSection::kTitle), title_id);
  if (!entry) {
    return std::u16string();
  }
  return memory::load_and_swap<std::u16string>(
      reinterpret_cast<const char16_t*>(entry->data.data() + sizeof(X_XDBF_GPD_TITLE_PLAYED)));
}

void GpdInfoProfile::AddNewTitle(uint32_t title_id, const std::string& title_name,
                                 uint32_t achievement_count, uint32_t total_gamerscore) {
  X_XDBF_GPD_TITLE_PLAYED title_gpd_data = {};
  title_gpd_data.title_id = title_id;
  title_gpd_data.achievements_count = achievement_count;
  title_gpd_data.gamerscore_total = total_gamerscore;

  const std::u16string title_name_u16 = rex::string::to_utf16(title_name);
  const uint32_t entry_size =
      sizeof(X_XDBF_GPD_TITLE_PLAYED) + U16BytesWithTerminator(title_name_u16.size());

  XdbfEntry entry(title_id, static_cast<uint16_t>(GpdSection::kTitle), entry_size);
  std::memcpy(entry.data.data(), &title_gpd_data, sizeof(X_XDBF_GPD_TITLE_PLAYED));

  rex::string::copy_and_swap_truncating(
      reinterpret_cast<char16_t*>(entry.data.data() + sizeof(X_XDBF_GPD_TITLE_PLAYED)), title_name_u16,
      title_name_u16.size() + 1);

  UpsertEntry(&entry);
}

bool GpdInfoProfile::RemoveTitle(uint32_t title_id) {
  const XdbfEntry* entry = GetEntry(static_cast<uint16_t>(GpdSection::kTitle), title_id);
  if (!entry) {
    return false;
  }
  DeleteEntry(entry);
  return true;
}

void GpdInfoProfile::UpdateTitleInfo(uint32_t title_id, const X_XDBF_GPD_TITLE_PLAYED& title_data) {
  X_XDBF_GPD_TITLE_PLAYED* current_info = GetTitleInfo(title_id);
  if (!current_info) {
    return;
  }
  std::memcpy(current_info, &title_data, sizeof(X_XDBF_GPD_TITLE_PLAYED));
}

}  // namespace util
}  // namespace system
}  // namespace rex

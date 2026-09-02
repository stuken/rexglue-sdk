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
#include <rex/system/util/gpd_info.h>

#include <algorithm>

#include <rex/assert.h>
#include <rex/math.h>
#include <rex/memory/utils.h>
#include <rex/string.h>
#include <rex/system/xam/user_settings.h>

namespace rex {
namespace system {
namespace util {

namespace {
uint32_t U16BytesWithTerminator(size_t char_count) {
  return static_cast<uint32_t>((char_count + 1) * sizeof(char16_t));
}

std::u16string ReadU16AndSwap(const char16_t* ptr) {
  return memory::load_and_swap<std::u16string>(ptr);
}
}  // namespace

GpdInfo::GpdInfo() : XdbfFile(), title_id_(uint32_t(-1)) {}

GpdInfo::GpdInfo(uint32_t title_id) : XdbfFile(), title_id_(title_id) {
  header_.magic = kXdbfContainerMagic;
  header_.version = kXdbfContainerVersion;
  header_.entry_count = kBaseEntryCount;
  header_.free_count = kBaseEntryCount;
  header_.free_used = 1;

  // A single free entry spanning the whole (empty) data area marks EOF.
  XbdfFileLoc loc;
  loc.size = 0xFFFFFFFF;
  loc.offset = 0;
  free_entries_.push_back(loc);
}

GpdInfo::GpdInfo(uint32_t title_id, const std::vector<uint8_t>& buffer)
    : XdbfFile({buffer.data(), buffer.size()}), title_id_(title_id) {}

std::span<const uint8_t> GpdInfo::GetImage(uint64_t id) const {
  const XdbfEntry* entry = GetEntry(static_cast<uint16_t>(GpdSection::kImage), id);
  if (!entry) {
    return {};
  }
  return {entry->data.data(), entry->data.size()};
}

void GpdInfo::AddImage(uint32_t id, std::span<const uint8_t> image_data) {
  XdbfEntry* entry = GetEntry(static_cast<uint16_t>(GpdSection::kImage), id);
  if (entry || image_data.empty()) {
    return;
  }

  XdbfEntry new_entry(id, static_cast<uint16_t>(GpdSection::kImage),
                      static_cast<uint32_t>(image_data.size()));
  std::memcpy(new_entry.data.data(), image_data.data(), image_data.size());
  UpsertEntry(&new_entry);
}

X_XDBF_GPD_SETTING_HEADER* GpdInfo::GetSetting(uint32_t id) {
  XdbfEntry* entry = GetEntry(static_cast<uint16_t>(GpdSection::kSetting), id);
  if (!entry) {
    return nullptr;
  }
  return reinterpret_cast<X_XDBF_GPD_SETTING_HEADER*>(entry->data.data());
}

std::span<const uint8_t> GpdInfo::GetSettingData(uint32_t id) {
  X_XDBF_GPD_SETTING_HEADER* setting = GetSetting(id);
  if (!setting || !setting->RequiresBuffer()) {
    return {};
  }

  const uint32_t size = uint32_t(setting->base_data.data.binary.size);
  const uint8_t* data_ptr = reinterpret_cast<const uint8_t*>(setting + 1);
  return {data_ptr, size};
}

void GpdInfo::UpsertSetting(const xam::UserSetting& setting_data) {
  const auto serialized_data = setting_data.Serialize();

  XdbfEntry new_entry(setting_data.get_setting_id(), static_cast<uint16_t>(GpdSection::kSetting),
                      static_cast<uint32_t>(serialized_data.size()));
  std::memcpy(new_entry.data.data(), serialized_data.data(), serialized_data.size());
  UpsertEntry(&new_entry);
}

std::u16string GpdInfo::GetString(uint32_t id) const {
  const XdbfEntry* entry = GetEntry(static_cast<uint16_t>(GpdSection::kString), id);
  if (!entry) {
    return {};
  }
  return ReadU16AndSwap(reinterpret_cast<const char16_t*>(entry->data.data()));
}

void GpdInfo::AddString(uint32_t id, const std::u16string& string_data) {
  XdbfEntry* entry = GetEntry(static_cast<uint16_t>(GpdSection::kString), id);
  if (entry != nullptr) {
    return;
  }

  const uint32_t entry_size = U16BytesWithTerminator(string_data.size());
  XdbfEntry new_entry(id, static_cast<uint16_t>(GpdSection::kString), entry_size);

  rex::string::copy_and_swap_truncating(reinterpret_cast<char16_t*>(new_entry.data.data()),
                                        string_data, string_data.length() + 1);
  UpsertEntry(&new_entry);
}

std::vector<uint8_t> GpdInfo::Serialize() const {
  const uint32_t entries_table_size = static_cast<uint32_t>(sizeof(XbdfEntry)) * uint32_t(header_.entry_count);
  const uint32_t free_table_size = static_cast<uint32_t>(sizeof(XbdfFileLoc)) * uint32_t(header_.free_count);
  const uint32_t gpd_size =
      static_cast<uint32_t>(sizeof(XbdfHeader)) + entries_table_size + free_table_size + CalculateEntriesSize();

  std::vector<uint8_t> out(gpd_size);

  uint8_t* write_ptr = out.data();
  std::memcpy(write_ptr, &header_, sizeof(XbdfHeader));
  write_ptr += sizeof(XbdfHeader);

  // Entries are sorted by section lowest-to-highest on disk.
  std::vector<const XdbfEntry*> sorted = GetSortedEntries();

  for (const auto* entry : sorted) {
    std::memcpy(write_ptr, &entry->info, sizeof(XbdfEntry));
    write_ptr += sizeof(XbdfEntry);
  }

  const uint32_t empty_entries_count = uint32_t(header_.entry_count) - static_cast<uint32_t>(sorted.size());
  write_ptr = std::fill_n(write_ptr, size_t(empty_entries_count) * sizeof(XbdfEntry), uint8_t(0));

  for (const auto& loc : free_entries_) {
    std::memcpy(write_ptr, &loc, sizeof(XbdfFileLoc));
    write_ptr += sizeof(XbdfFileLoc);
  }

  const uint32_t empty_free_entries_count =
      uint32_t(header_.free_count) - static_cast<uint32_t>(free_entries_.size());
  write_ptr = std::fill_n(write_ptr, size_t(empty_free_entries_count) * sizeof(XbdfFileLoc), uint8_t(0));

  for (const auto* entry : sorted) {
    if (!entry->info.size) {
      continue;
    }
    std::memcpy(write_ptr + uint32_t(entry->info.offset), entry->data.data(), entry->data.size());
  }
  return out;
}

bool GpdInfo::IsSyncEntry(const XdbfEntry* entry) {
  return uint64_t(entry->info.id) == 0x100000000ULL || uint64_t(entry->info.id) == 0x200000000ULL;
}

bool GpdInfo::IsEntryOfSection(const XdbfEntry* entry, GpdSection section) {
  return uint16_t(entry->info.section) == static_cast<uint16_t>(section);
}

void GpdInfo::UpsertEntry(XdbfEntry* updated_entry) {
  auto* entry = GetEntry(updated_entry->info.section, updated_entry->info.id);
  if (entry) {
    DeleteEntry(entry);
  }
  InsertEntry(updated_entry);
}

uint32_t GpdInfo::FindFreeLocation(uint32_t entry_size) {
  assert_false(free_entries_.empty());

  uint32_t offset = uint32_t(free_entries_.back().offset);

  auto itr = std::find_if(free_entries_.begin(), free_entries_.end(),
                          [entry_size](const XbdfFileLoc& entry) { return uint32_t(entry.size) == entry_size; });

  if (itr != free_entries_.end()) {
    offset = uint32_t(itr->offset);
    header_.free_used = uint32_t(header_.free_used) - 1;
    free_entries_.erase(itr);
    return offset;
  }

  itr = std::find_if(free_entries_.begin(), free_entries_.end(),
                     [entry_size](const XbdfFileLoc& entry) { return uint32_t(entry.size) > entry_size; });

  // There is always at least one entry (the EOF marker), so itr is valid.
  offset = uint32_t(itr->offset);
  itr->offset = uint32_t(itr->offset) + entry_size;
  itr->size = uint32_t(itr->size) - entry_size;

  return offset;
}

void GpdInfo::InsertEntry(XdbfEntry* entry) {
  ResizeEntryTable();

  entry->info.offset = FindFreeLocation(uint32_t(entry->info.size));
  entries_.push_back(*entry);
  header_.entry_used = uint32_t(header_.entry_used) + 1;
}

void GpdInfo::DeleteEntry(const XdbfEntry* entry) {
  // Don't actually remove data from the buffer, just release the entry-table
  // slot and mark its space free for reuse.
  MarkSpaceAsFree(uint32_t(entry->info.offset), uint32_t(entry->info.size));

  auto itr = std::find_if(entries_.begin(), entries_.end(), [entry](const XdbfEntry& candidate) {
    return uint16_t(entry->info.section) == uint16_t(candidate.info.section) &&
           uint64_t(candidate.info.id) == uint64_t(entry->info.id);
  });

  if (itr != entries_.end()) {
    entries_.erase(itr);
  }
  header_.entry_used = uint32_t(header_.entry_used) - 1;
}

std::vector<const XdbfEntry*> GpdInfo::GetSortedEntries() const {
  std::vector<const XdbfEntry*> sorted_entries;
  for (const auto& entry : entries_) {
    sorted_entries.push_back(&entry);
  }

  std::sort(sorted_entries.begin(), sorted_entries.end(), [](const XdbfEntry* a, const XdbfEntry* b) {
    if (uint16_t(a->info.section) == uint16_t(b->info.section)) {
      return uint64_t(a->info.id) < uint64_t(b->info.id);
    }
    return uint16_t(a->info.section) < uint16_t(b->info.section);
  });

  return sorted_entries;
}

void GpdInfo::ResizeEntryTable() {
  // Offsets are relative to the end of the entry tables, so growing them
  // doesn't require recalculating any existing entry's offset.
  if (uint32_t(header_.entry_used) >= uint32_t(header_.entry_count)) {
    header_.entry_count = rex::round_up(uint32_t(header_.entry_count) + 1, kBaseEntryCount, true);
  }

  if (uint32_t(header_.free_used) >= uint32_t(header_.free_count)) {
    header_.free_count = rex::round_up(uint32_t(header_.free_used) + 1, kBaseEntryCount, true);
  }
}

void GpdInfo::MarkSpaceAsFree(uint32_t offset, uint32_t size) {
  XbdfFileLoc loc;
  loc.size = size;
  loc.offset = offset;

  ResizeEntryTable();
  free_entries_.insert(free_entries_.begin(), loc);
  header_.free_used = uint32_t(header_.free_used) + 1;
}

}  // namespace util
}  // namespace system
}  // namespace rex

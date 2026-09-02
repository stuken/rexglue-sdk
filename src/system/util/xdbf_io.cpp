/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Emulator. All rights reserved.                        *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */
#include <rex/system/util/xdbf_io.h>

namespace rex {
namespace system {
namespace util {

XdbfFile::XdbfFile(std::span<const uint8_t> buffer) {
  if (buffer.size() <= sizeof(XbdfHeader)) {
    return;
  }

  const uint8_t* ptr = buffer.data();

  if (!LoadHeader(reinterpret_cast<const XbdfHeader*>(ptr))) {
    return;
  }

  ptr += sizeof(XbdfHeader);

  const XbdfFileLoc* free_ptr =
      reinterpret_cast<const XbdfFileLoc*>(ptr + (sizeof(XbdfEntry) * uint32_t(header_.entry_count)));

  const uint8_t* data_ptr = reinterpret_cast<const uint8_t*>(free_ptr) +
                            (sizeof(XbdfFileLoc) * uint32_t(header_.free_count));

  LoadEntries(reinterpret_cast<const XbdfEntry*>(ptr), data_ptr);
  LoadFreeEntries(free_ptr);
}

bool XdbfFile::LoadHeader(const XbdfHeader* header) {
  if (!header || header->magic != kXdbfContainerMagic) {
    return false;
  }

  std::memcpy(&header_, header, sizeof(XbdfHeader));
  return true;
}

uint32_t XdbfFile::CalculateEntriesSize() const {
  // XDBF always contains at least 1 free entry that marks EOF, which lets us
  // use it to get the size of the data actually in the file.
  return uint32_t(free_entries_.back().offset);
}

void XdbfFile::LoadEntries(const XbdfEntry* table_of_content, const uint8_t* data_ptr) {
  if (!table_of_content || !data_ptr) {
    return;
  }

  const uint32_t entry_used = header_.entry_used;
  for (uint32_t i = 0; i < entry_used; i++) {
    entries_.push_back(XdbfEntry(table_of_content++, data_ptr));
  }
}

void XdbfFile::LoadFreeEntries(const XbdfFileLoc* free_entries) {
  const uint32_t free_used = header_.free_used;
  for (uint32_t i = 0; i < free_used; i++) {
    free_entries_.push_back(*free_entries);
    free_entries++;
  }
}

XdbfEntry* XdbfFile::GetEntry(uint16_t section, uint64_t id) {
  for (XdbfEntry& entry : entries_) {
    if (uint64_t(entry.info.id) != id || uint16_t(entry.info.section) != section) {
      continue;
    }
    return &entry;
  }
  return nullptr;
}

const XdbfEntry* XdbfFile::GetEntry(uint16_t section, uint64_t id) const {
  for (const XdbfEntry& entry : entries_) {
    if (uint64_t(entry.info.id) != id || uint16_t(entry.info.section) != section) {
      continue;
    }
    return &entry;
  }
  return nullptr;
}

uint32_t XdbfFile::CalculateDataStartOffset() const {
  const uint32_t entry_size = static_cast<uint32_t>(sizeof(XbdfEntry)) * uint32_t(header_.entry_count);
  const uint32_t free_size = static_cast<uint32_t>(sizeof(XbdfFileLoc)) * uint32_t(header_.free_count);
  return static_cast<uint32_t>(sizeof(XbdfHeader)) + entry_size + free_size;
}

}  // namespace util
}  // namespace system
}  // namespace rex

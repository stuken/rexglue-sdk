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
#pragma once

#include <cstring>
#include <span>
#include <vector>

#include <rex/memory.h>
#include <rex/system/util/xdbf_utils.h>

namespace rex {
namespace system {
namespace util {

// Public (header-visible) counterpart of xdbf_utils.cpp's file-local magic
// constant - needed here so a writable container can stamp a fresh header.
inline constexpr memory::fourcc_t kXdbfContainerMagic = memory::make_fourcc("XDBF");
inline constexpr uint32_t kXdbfContainerVersion = 0x10000;

// One mutable XDBF entry: header info plus owned data bytes. Reuses
// rexglue's existing (read-only-oriented) XbdfEntry for the on-disk header
// shape. Ported from xenia's xdbf_io.h Entry.
struct XdbfEntry {
  XbdfEntry info{};
  std::vector<uint8_t> data;

  XdbfEntry() { info.id = 0; info.offset = 0; info.section = 0; info.size = 0; }

  // Offset must be filled in externally (by the container) before use.
  XdbfEntry(uint64_t id, uint16_t section, uint32_t size) {
    info.id = id;
    info.section = section;
    info.size = size;
    data.resize(size);
  }

  XdbfEntry(const XbdfEntry* entry, const uint8_t* entry_data) {
    info = *entry;
    const uint32_t size = info.size;
    data.resize(size);
    std::memcpy(data.data(), entry_data + uint32_t(info.offset), size);
  }
};

// Wraps a writable, in-memory XDBF (XboxDataBaseFormat) container: the
// entry-table/free-space bookkeeping shared by every .gpd file, independent
// of what a given section's entries mean. Ported from xenia's XdbfFile
// (kernel/xam/xdbf/xdbf_io.h/.cc).
class XdbfFile {
 public:
  XdbfFile() = default;
  explicit XdbfFile(std::span<const uint8_t> buffer);
  ~XdbfFile() = default;

  const XdbfEntry* GetEntry(uint16_t section, uint64_t id) const;

 protected:
  XbdfHeader header_ = {};
  std::vector<XdbfEntry> entries_ = {};
  std::vector<XbdfFileLoc> free_entries_ = {};

  XdbfEntry* GetEntry(uint16_t section, uint64_t id);
  uint32_t CalculateDataStartOffset() const;
  uint32_t CalculateEntriesSize() const;

 private:
  bool LoadHeader(const XbdfHeader* header);
  void LoadEntries(const XbdfEntry* table_of_content, const uint8_t* data_ptr);
  void LoadFreeEntries(const XbdfFileLoc* free_entries);
};

}  // namespace util
}  // namespace system
}  // namespace rex

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
#include <rex/system/xam/user_data.h>

#include <algorithm>

#include <rex/assert.h>
#include <rex/memory/utils.h>

namespace rex {
namespace system {
namespace xam {

namespace {
// count includes the null terminator.
uint32_t U16BytesWithTerminator(size_t char_count) {
  return static_cast<uint32_t>((char_count + 1) * sizeof(char16_t));
}
}  // namespace

UserData::UserData(X_USER_DATA_TYPE data_type, const UserDataTypes& value) {
  wire_ = {};
  wire_.type = data_type;

  switch (data_type) {
    case X_USER_DATA_TYPE::BINARY: {
      extended_data_ = std::get<std::vector<uint8_t>>(value);
      if (extended_data_.size() > kMaxUserDataSize) {
        extended_data_.resize(kMaxUserDataSize);
      }
      wire_.data.binary.size = static_cast<uint32_t>(extended_data_.size());
      break;
    }
    case X_USER_DATA_TYPE::WSTRING: {
      std::u16string str = std::get<std::u16string>(value);
      const uint32_t max_chars = (kMaxUserDataSize / sizeof(char16_t)) - 1;
      if (str.size() > max_chars) {
        str.resize(max_chars);
      }

      const uint32_t byte_size = U16BytesWithTerminator(str.size());
      wire_.data.unicode.size = byte_size;

      // str.data() is guaranteed to have a valid null terminator at
      // str.size() since C++11, so this covers the terminator too.
      extended_data_.resize(byte_size);
      memory::copy_and_swap(reinterpret_cast<uint16_t*>(extended_data_.data()),
                            reinterpret_cast<const uint16_t*>(str.data()), str.size() + 1);
      break;
    }
    case X_USER_DATA_TYPE::INT32:
      wire_.data.s32 = std::get<int32_t>(value);
      break;
    case X_USER_DATA_TYPE::FLOAT:
      wire_.data.f32 = std::get<float>(value);
      break;
    case X_USER_DATA_TYPE::CONTEXT:
      wire_.data.u32 = std::get<uint32_t>(value);
      break;
    case X_USER_DATA_TYPE::DOUBLE:
      wire_.data.f64 = std::get<double>(value);
      break;
    case X_USER_DATA_TYPE::DATETIME:
    case X_USER_DATA_TYPE::INT64:
      wire_.data.s64 = std::get<int64_t>(value);
      break;
    default:
      assert_always();
  }
}

UserData::UserData(X_USER_DATA_TYPE data_type, const void* wire_bytes,
                   std::span<const uint8_t> extended_data) {
  wire_ = {};
  if (wire_bytes) {
    std::memcpy(&wire_, wire_bytes, sizeof(wire_));
  }
  wire_.type = data_type;

  if (!requires_additional_data() || extended_data.empty()) {
    return;
  }

  const size_t size = std::min<size_t>(extended_data.size(), kMaxUserDataSize);
  extended_data_.assign(extended_data.begin(), extended_data.begin() + size);

  if (wire_.type == X_USER_DATA_TYPE::BINARY) {
    wire_.data.binary.size = static_cast<uint32_t>(size);
  } else {
    wire_.data.unicode.size = static_cast<uint32_t>(size);
  }
}

}  // namespace xam
}  // namespace system
}  // namespace rex

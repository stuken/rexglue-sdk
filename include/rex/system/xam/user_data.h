/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */
#pragma once

#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include <rex/memory.h>

namespace rex {
namespace system {
namespace xam {

// Same values/meaning as the "type" byte of rexglue's wire-format
// X_USER_PROFILE_SETTING_DATA - ported verbatim from xenia's
// X_USER_DATA_TYPE (values are wire data, not something to improvise).
enum class X_USER_DATA_TYPE : uint8_t {
  CONTEXT = 0,
  INT32 = 1,
  INT64 = 2,
  DOUBLE = 3,
  WSTRING = 4,
  FLOAT = 5,
  BINARY = 6,
  DATETIME = 7,
  UNSET = 0xFF,
};

// A setting/property id packs its type and (for variable-length types) its
// maximum size into the high bits, id in the low bits. Ported from xenia's
// AttributeKey.
union AttributeKey {
  uint32_t value;
  struct {
    uint32_t id : 16;
    uint32_t size : 12;
    uint32_t type : 4;
  };
};

using UserDataTypes = std::variant<uint32_t, int32_t, float, int64_t, double, std::u16string,
                                   std::vector<uint8_t>>;

constexpr uint32_t kMaxUserDataSize = 0x03E8;

// Byte-for-byte layout of the 16-byte "type + 8-byte union" tail that both
// rexglue's X_USER_PROFILE_SETTING_DATA and a GPD setting entry's base_data
// carry. Kept standalone (rather than referencing X_USER_PROFILE_SETTING_DATA
// directly) so this header doesn't need to depend on user_profile.h - callers
// hand UserData a raw pointer to the 16-byte wire blob instead.
struct alignas(8) X_USER_DATA_WIRE {
  X_USER_DATA_TYPE type;
  uint8_t reserved[7];
  union {
    rex::be<int32_t> s32;
    rex::be<int64_t> s64;
    rex::be<uint32_t> u32;
    rex::be<double> f64;
    rex::be<float> f32;
    struct {
      rex::be<uint32_t> size;
      rex::be<uint32_t> ptr;
    } unicode;
    struct {
      rex::be<uint32_t> size;
      rex::be<uint32_t> ptr;
    } binary;
    rex::be<uint64_t> filetime;
  } data;
};
static_assert(sizeof(X_USER_DATA_WIRE) == 16);

// Host-side representation of one X_USER_DATA-shaped value (a profile
// setting or a XUSER_PROPERTY). Owns any variable-length payload (BINARY or
// WSTRING) alongside the fixed 16-byte type+union header. Ported from
// xenia's UserData (user_data.h/.cc); this is the single-user-scoped
// replacement for rexglue's ad-hoc UserProfile::Setting hierarchy.
class UserData {
 public:
  X_USER_DATA_TYPE get_type() const { return wire_.type; }

  const X_USER_DATA_WIRE* get_data() const { return &wire_; }
  std::span<const uint8_t> get_extended_data() const {
    return {extended_data_.data(), extended_data_.size()};
  }

  bool requires_additional_data() const {
    return wire_.type == X_USER_DATA_TYPE::BINARY || wire_.type == X_USER_DATA_TYPE::WSTRING;
  }

  static AttributeKey get_attribute_key(uint32_t id) { return AttributeKey{.value = id}; }

  static X_USER_DATA_TYPE get_type(uint32_t id) {
    return static_cast<X_USER_DATA_TYPE>(get_attribute_key(id).type);
  }

  static uint16_t get_max_size(uint32_t id) {
    return std::min<uint16_t>(get_attribute_key(id).size, kMaxUserDataSize);
  }

  static bool requires_additional_data(uint32_t id) {
    const auto type = get_type(id);
    return type == X_USER_DATA_TYPE::BINARY || type == X_USER_DATA_TYPE::WSTRING;
  }

  size_t get_data_size() const { return sizeof(X_USER_DATA_WIRE) + extended_data_.size(); }

  // Copies the 16-byte type+union header to a guest/host destination. The
  // caller is responsible for placing any extended (BINARY/WSTRING) payload
  // separately - UserData has no guest-memory access of its own.
  void WriteWireTo(void* destination) const { std::memcpy(destination, &wire_, sizeof(wire_)); }

 protected:
  UserData() = default;
  UserData(const UserData&) = default;
  UserData(UserData&&) = default;
  UserData& operator=(const UserData&) = default;
  UserData& operator=(UserData&&) = default;
  ~UserData() = default;

  // From host (recomp/kernel code constructing a value to hand to the guest).
  // WSTRING values are ordinary host-order std::u16string - swapping to
  // guest byte order happens once here, not at every later write.
  UserData(X_USER_DATA_TYPE data_type, const UserDataTypes& value);

  // From guest or from a GPD entry: wire_bytes points at a 16-byte
  // X_USER_DATA_WIRE-shaped blob (e.g. &X_USER_PROFILE_SETTING::data, or a
  // GPD setting header's base_data). extended_data is the already-resolved
  // BINARY/WSTRING payload bytes (guest-order, empty span if the type
  // doesn't need one) - the caller is responsible for translating any guest
  // pointer/size in the wire blob before calling this, since UserData has
  // no guest-memory access of its own.
  UserData(X_USER_DATA_TYPE data_type, const void* wire_bytes,
           std::span<const uint8_t> extended_data = {});

  X_USER_DATA_WIRE wire_ = {};
  std::vector<uint8_t> extended_data_ = {};
};

}  // namespace xam
}  // namespace system
}  // namespace rex

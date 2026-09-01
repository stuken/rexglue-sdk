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

#include <rex/audio/asf_demuxer.h>

#include <algorithm>
#include <cstring>

namespace rex::audio {

namespace {
// GUID byte sequences exactly as stored on disk (little-endian fields
// pre-flattened), transcribed from FFmpeg's libavformat/asf.c so they can be
// memcmp'd directly against file bytes with no endian handling needed.
constexpr uint8_t kHeaderGuid[16] = {0x30, 0x26, 0xB2, 0x75, 0x8E, 0x66, 0xCF, 0x11,
                                     0xA6, 0xD9, 0x00, 0xAA, 0x00, 0x62, 0xCE, 0x6C};
constexpr uint8_t kFilePropertiesGuid[16] = {0xA1, 0xDC, 0xAB, 0x8C, 0x47, 0xA9, 0xCF, 0x11,
                                             0x8E, 0xE4, 0x00, 0xC0, 0x0C, 0x20, 0x53, 0x65};
constexpr uint8_t kStreamPropertiesGuid[16] = {0x91, 0x07, 0xDC, 0xB7, 0xB7, 0xA9, 0xCF, 0x11,
                                               0x8E, 0xE6, 0x00, 0xC0, 0x0C, 0x20, 0x53, 0x65};
constexpr uint8_t kAudioStreamTypeGuid[16] = {0x40, 0x9E, 0x69, 0xF8, 0x4D, 0x5B, 0xCF, 0x11,
                                              0xA8, 0xFD, 0x00, 0x80, 0x5F, 0x5C, 0x44, 0x2B};
constexpr uint8_t kDataGuid[16] = {0x36, 0x26, 0xB2, 0x75, 0x8E, 0x66, 0xCF, 0x11,
                                   0xA6, 0xD9, 0x00, 0xAA, 0x00, 0x62, 0xCE, 0x6C};
}  // namespace

bool AsfDemuxer::Open(const uint8_t* data, size_t size) {
  data_ = data;
  size_ = size;
  pos_ = 0;
  if (!ParseHeaderObject()) {
    return false;
  }
  return has_audio_stream_ && packet_size_ > 0;
}

bool AsfDemuxer::ParseHeaderObject() {
  if (!MatchGuid(kHeaderGuid)) {
    return false;
  }
  if (AtEnd(14)) {  // Header Size(8) + Number of Header Objects(4) + Reserved(1+1)
    return false;
  }
  ReadU64LE();
  ReadU32LE();
  ReadU8();
  ReadU8();

  for (;;) {
    if (AtEnd(24)) {
      return false;  // truncated before a Data Object was found
    }
    const size_t object_start = pos_;
    const uint8_t* guid = data_ + pos_;
    pos_ += 16;
    const uint64_t object_size = ReadU64LE();
    if (object_size < 24 || object_start + object_size > size_) {
      return false;
    }
    const size_t object_body_end = object_start + static_cast<size_t>(object_size);

    if (std::memcmp(guid, kDataGuid, 16) == 0) {
      // File ID(16) + Total Data Packets(8) + Reserved(2) precede the actual
      // packet stream.
      if (AtEnd(26)) {
        return false;
      }
      Skip(16 + 8 + 2);
      data_start_ = pos_;
      // The declared object size is unreliable for streamed/broadcast files;
      // just use the rest of the buffer and let per-packet bounds checks in
      // LoadNextPacket() find the real end.
      data_end_ = size_;
      break;
    }

    if (std::memcmp(guid, kFilePropertiesGuid, 16) == 0) {
      if (!ParseFilePropertiesObject(object_body_end)) {
        return false;
      }
    } else if (std::memcmp(guid, kStreamPropertiesGuid, 16) == 0) {
      if (!ParseStreamPropertiesObject(object_body_end)) {
        return false;
      }
    }
    // Anything else (comments, extended content description, extended
    // stream properties, padding, ...) is irrelevant to raw audio playback -
    // unconditionally skip to the object's declared end, regardless of how
    // much of it was actually consumed above.
    pos_ = object_body_end;
  }

  return true;
}

bool AsfDemuxer::ParseFilePropertiesObject(size_t /*object_body_end*/) {
  if (AtEnd(80)) {
    return false;
  }
  Skip(16);  // File ID
  Skip(8);   // File Size
  Skip(8);   // Creation Date
  Skip(8);   // Data Packets Count
  Skip(8);   // Play Duration
  Skip(8);   // Send Duration
  Skip(4);   // Preroll
  Skip(4);   // Flags padding (a reserved dword ahead of the real Flags field)
  Skip(4);   // Flags
  Skip(4);   // Minimum Data Packet Size
  packet_size_ = ReadU32LE();  // Maximum Data Packet Size - the fixed on-disk packet slot size
  Skip(4);   // Maximum Bitrate
  return packet_size_ > 0;
}

bool AsfDemuxer::ParseStreamPropertiesObject(size_t object_body_end) {
  if (AtEnd(16)) {
    return false;
  }
  const bool is_audio = std::memcmp(data_ + pos_, kAudioStreamTypeGuid, 16) == 0;
  Skip(16);  // Stream Type GUID

  if (AtEnd(16 + 8 + 4 + 4 + 2 + 4)) {
    return false;
  }
  Skip(16);  // Error Correction Type GUID
  Skip(8);   // Time Offset
  const uint32_t type_specific_size = ReadU32LE();
  Skip(4);   // Error Correction Data Length
  const uint32_t stream_id = ReadU16LE() & 0x7F;
  Skip(4);   // Reserved (Stream Number is followed by a reserved dword)

  if (!is_audio || has_audio_stream_) {
    // Not audio, or a later audio stream in a multi-stream file - only the
    // first usable audio stream is tracked; other streams' payloads are
    // skipped by ID during packet parsing.
    return true;
  }

  if (AtEnd(18) || pos_ + type_specific_size > object_body_end) {
    return true;  // malformed - just don't select this stream, not fatal to the file
  }

  const size_t type_specific_start = pos_;
  AudioInfo info;
  info.format_tag = ReadU16LE();
  info.channels = ReadU16LE();
  info.sample_rate = ReadU32LE();
  info.avg_bytes_per_sec = ReadU32LE();
  info.block_align = ReadU16LE();
  info.bits_per_sample = ReadU16LE();
  const uint16_t cb_size = ReadU16LE();
  const size_t type_specific_declared_end = type_specific_start + type_specific_size;
  const size_t type_specific_remaining =
      type_specific_declared_end > pos_ ? type_specific_declared_end - pos_ : 0;
  const size_t extra_len = std::min({size_t(cb_size), type_specific_remaining, Remaining()});
  info.extra_data.assign(data_ + pos_, data_ + pos_ + extra_len);

  // Move to the end of the declared type-specific data regardless of
  // exactly how many bytes cb_size/extra_len accounted for.
  pos_ = std::min(type_specific_declared_end, object_body_end);

  // Only WMAv2 (0x0161) and WMAPro (0x0162) decoders are vendored (see
  // thirdparty/ffmpeg-overlay/codec_list.c) - anything else (WMAv1, WMA
  // Lossless, ...) can't be decoded here.
  if (info.format_tag != 0x0161 && info.format_tag != 0x0162) {
    return true;
  }

  // Optional audio "spread" (interleaving) extension occupies the room
  // between the end of the type-specific data and the end of this object,
  // if any.
  if (object_body_end >= pos_ + 8) {
    ds_span_ = ReadU8();
    ds_packet_size_ = ReadU16LE();
    ds_chunk_size_ = ReadU16LE();
    Skip(2);  // Silence Data length
    Skip(1);  // Silence Data
    if (ds_span_ > 1 && (!ds_chunk_size_ || (ds_packet_size_ / ds_chunk_size_ <= 1) ||
                        ds_packet_size_ % ds_chunk_size_)) {
      ds_span_ = 0;  // parameters don't make sense - disable de-interleaving
    }
  }

  has_audio_stream_ = true;
  audio_stream_id_ = stream_id;
  audio_info_ = std::move(info);
  return true;
}

bool AsfDemuxer::LoadNextPacket() {
  packet_loaded_ = false;
  if (packet_size_ == 0 || pos_ + packet_size_ > data_end_) {
    return false;
  }
  const size_t packet_start = pos_;
  packet_slot_end_ = packet_start + packet_size_;

  if (AtEnd(2)) {
    return false;
  }
  uint8_t c = ReadU8();
  if (c & 0x80) {  // Error Correction Present
    if (!(c & 0x60)) {  // Error Correction Length Type == 0 (the only form real files use)
      const size_t ec_len = c & 0x0F;
      if (packet_start + 1 + ec_len >= packet_slot_end_) {
        pos_ = packet_slot_end_;
        packet_segments_left_ = 0;
        packet_loaded_ = true;
        return true;
      }
      Skip(ec_len);
    }
    if (AtEnd(1)) {
      return false;
    }
    c = ReadU8();  // the real "Length Type Flags" byte
  }
  if (AtEnd(1)) {
    return false;
  }
  const uint8_t property = ReadU8();

  packet_flags_ = c;
  packet_property_ = property;

  const uint32_t packet_length = ReadByWidthType((c >> 5) & 3, packet_size_);
  ReadByWidthType((c >> 1) & 3, 0);  // Sequence - always ignored
  const uint32_t padsize = ReadByWidthType((c >> 3) & 3, 0);

  if (AtEnd(6)) {
    return false;
  }
  Skip(4);  // Send Time
  Skip(2);  // Duration

  if (packet_flags_ & 0x01) {  // Multiple Payloads Present
    if (AtEnd(1)) {
      return false;
    }
    packet_segsizetype_ = ReadU8();
    packet_segments_left_ = packet_segsizetype_ & 0x3F;
  } else {
    packet_segsizetype_ = 0x80;
    packet_segments_left_ = 1;
  }

  if (packet_length == 0 || padsize >= packet_length ||
      packet_start + packet_length > packet_slot_end_ ||
      pos_ > packet_start + packet_length - padsize) {
    // Malformed packet header - resync at the next fixed-size packet slot
    // rather than guessing.
    pos_ = packet_slot_end_;
    packet_segments_left_ = 0;
    packet_loaded_ = true;
    return true;
  }

  packet_data_end_ = packet_start + packet_length - padsize;
  packet_loaded_ = true;
  return true;
}

AsfDemuxer::PayloadResult AsfDemuxer::ConsumeNextPayload(std::vector<uint8_t>* out_frame) {
  if (in_chain_) {
    if (pos_ >= chain_end_ || AtEnd(1)) {
      in_chain_ = false;
      return PayloadResult::kNeedMore;
    }
    const uint8_t sub_len = ReadU8();
    if (pos_ + sub_len > chain_end_ || AtEnd(sub_len)) {
      in_chain_ = false;  // desynced - bail out of the chain rather than misreading past it
      pos_ = chain_end_;
      return PayloadResult::kNeedMore;
    }
    const uint8_t* sub_data = data_ + pos_;
    Skip(sub_len);
    if (pos_ >= chain_end_) {
      in_chain_ = false;
    }
    if (chain_stream_id_ == audio_stream_id_ && sub_len > 0) {
      out_frame->assign(sub_data, sub_data + sub_len);
      return PayloadResult::kFrameReady;
    }
    return PayloadResult::kNeedMore;
  }

  if (packet_segments_left_ <= 0) {
    return PayloadResult::kNeedMore;
  }

  if (AtEnd(1)) {
    packet_segments_left_ = 0;
    return PayloadResult::kNeedMore;
  }
  const uint8_t stream_byte = ReadU8();
  const uint32_t stream_id = stream_byte & 0x7F;
  packet_segments_left_--;

  const uint32_t media_object_number_width = (packet_property_ >> 4) & 3;
  const uint32_t frag_offset_width = (packet_property_ >> 2) & 3;
  const uint32_t replic_size_width = packet_property_ & 3;

  ReadByWidthType(media_object_number_width, 0);  // Media Object Number - unused, sequencing not tracked
  const uint32_t frag_offset = ReadByWidthType(frag_offset_width, 0);
  const uint32_t replic_size = ReadByWidthType(replic_size_width, 0);

  if (replic_size >= 8) {
    if (AtEnd(8) || pos_ + (replic_size - 8) > packet_data_end_) {
      packet_segments_left_ = 0;
      return PayloadResult::kNeedMore;
    }
    const uint32_t object_size = ReadU32LE();
    ReadU32LE();  // Presentation Time - unused, we don't track timestamps
    Skip(replic_size - 8);  // Payload Extension System data, if any - not parsed

    uint32_t payload_length;
    if (packet_flags_ & 0x01) {
      payload_length = ReadByWidthType((packet_segsizetype_ >> 6) & 3, 0);
    } else {
      payload_length = static_cast<uint32_t>(packet_data_end_ - pos_);
    }
    if (payload_length == 0 || pos_ + payload_length > packet_data_end_ || object_size == 0 ||
        object_size > (64u << 20)) {
      packet_segments_left_ = 0;
      return PayloadResult::kNeedMore;
    }

    const uint8_t* payload_data = data_ + pos_;
    Skip(payload_length);

    if (stream_id != audio_stream_id_) {
      return PayloadResult::kNeedMore;
    }

    if (frag_offset == 0) {
      reassembly_buffer_.assign(object_size, 0);
      reassembly_offset_ = 0;
      reassembly_active_ = true;
      reassembly_object_size_ = object_size;
    }
    if (!reassembly_active_ || object_size != reassembly_object_size_ ||
        frag_offset != reassembly_offset_ ||
        frag_offset + payload_length > reassembly_object_size_) {
      // Out-of-order/unexpected fragment - drop the in-progress object
      // rather than assembling something wrong from it.
      reassembly_active_ = false;
      return PayloadResult::kNeedMore;
    }

    std::memcpy(reassembly_buffer_.data() + frag_offset, payload_data, payload_length);
    reassembly_offset_ += payload_length;
    if (reassembly_offset_ != reassembly_object_size_) {
      return PayloadResult::kNeedMore;
    }

    reassembly_active_ = false;
    ApplyAudioSpreadDeinterleave(&reassembly_buffer_);
    *out_frame = std::move(reassembly_buffer_);
    reassembly_buffer_.clear();
    return PayloadResult::kFrameReady;
  }

  if (replic_size == 1) {
    if (AtEnd(1)) {
      packet_segments_left_ = 0;
      return PayloadResult::kNeedMore;
    }
    ReadU8();  // Presentation Time Delta - unused, we don't track timestamps

    uint32_t payload_length;
    if (packet_flags_ & 0x01) {
      payload_length = ReadByWidthType((packet_segsizetype_ >> 6) & 3, 0);
    } else {
      payload_length = static_cast<uint32_t>(packet_data_end_ - pos_);
    }
    if (payload_length == 0 || pos_ + payload_length > packet_data_end_) {
      packet_segments_left_ = 0;
      return PayloadResult::kNeedMore;
    }

    in_chain_ = true;
    chain_stream_id_ = stream_id;
    chain_end_ = pos_ + payload_length;
    return PayloadResult::kNeedMore;
  }

  // A replicated-data length of 2-7 is never valid per the ASF spec (and per
  // FFmpeg's own demuxer, which treats it as an error) - resync at the next
  // packet rather than guessing what it means.
  packet_segments_left_ = 0;
  return PayloadResult::kNeedMore;
}

bool AsfDemuxer::ReadNextFrame(std::vector<uint8_t>* out_frame) {
  if (!has_audio_stream_) {
    return false;
  }
  for (;;) {
    if (!packet_loaded_ || (!in_chain_ && packet_segments_left_ <= 0)) {
      if (packet_loaded_) {
        pos_ = packet_slot_end_;  // skip any trailing padding of the packet just finished
      }
      if (!LoadNextPacket()) {
        return false;
      }
      continue;
    }
    const PayloadResult result = ConsumeNextPayload(out_frame);
    if (result == PayloadResult::kFrameReady) {
      return true;
    }
    if (result == PayloadResult::kError) {
      return false;
    }
  }
}

void AsfDemuxer::ApplyAudioSpreadDeinterleave(std::vector<uint8_t>* buffer) const {
  if (ds_span_ <= 1 || ds_chunk_size_ == 0 ||
      buffer->size() != size_t(ds_packet_size_) * ds_span_) {
    return;
  }
  std::vector<uint8_t> out(buffer->size());
  const size_t rows = size_t(ds_packet_size_) / ds_chunk_size_;
  size_t offset = 0;
  while (offset < buffer->size()) {
    const size_t off = offset / ds_chunk_size_;
    const size_t row = off / ds_span_;
    const size_t col = off % ds_span_;
    const size_t idx = row + col * rows;
    const size_t src_offset = idx * ds_chunk_size_;
    if (src_offset + ds_chunk_size_ > buffer->size()) {
      return;  // parameters don't actually fit this object - leave it as-is
    }
    std::memcpy(out.data() + offset, buffer->data() + src_offset, ds_chunk_size_);
    offset += ds_chunk_size_;
  }
  *buffer = std::move(out);
}

uint8_t AsfDemuxer::ReadU8() {
  if (AtEnd(1)) {
    pos_ = size_;
    return 0;
  }
  return data_[pos_++];
}

uint16_t AsfDemuxer::ReadU16LE() {
  if (AtEnd(2)) {
    pos_ = size_;
    return 0;
  }
  const uint16_t v = uint16_t(data_[pos_]) | (uint16_t(data_[pos_ + 1]) << 8);
  pos_ += 2;
  return v;
}

uint32_t AsfDemuxer::ReadU32LE() {
  if (AtEnd(4)) {
    pos_ = size_;
    return 0;
  }
  const uint32_t v = uint32_t(data_[pos_]) | (uint32_t(data_[pos_ + 1]) << 8) |
                     (uint32_t(data_[pos_ + 2]) << 16) | (uint32_t(data_[pos_ + 3]) << 24);
  pos_ += 4;
  return v;
}

uint64_t AsfDemuxer::ReadU64LE() {
  if (AtEnd(8)) {
    pos_ = size_;
    return 0;
  }
  const uint64_t lo = ReadU32LE();
  const uint64_t hi = ReadU32LE();
  return lo | (hi << 32);
}

uint32_t AsfDemuxer::ReadByWidthType(uint32_t width_type, uint32_t default_value) {
  switch (width_type & 3) {
    case 1:
      return AtEnd(1) ? default_value : ReadU8();
    case 2:
      return AtEnd(2) ? default_value : ReadU16LE();
    case 3:
      return AtEnd(4) ? default_value : ReadU32LE();
    default:
      return default_value;
  }
}

void AsfDemuxer::Skip(size_t n) { pos_ = (pos_ + n <= size_) ? pos_ + n : size_; }

bool AsfDemuxer::MatchGuid(const uint8_t guid[16]) {
  if (AtEnd(16)) {
    return false;
  }
  const bool match = std::memcmp(data_ + pos_, guid, 16) == 0;
  pos_ += 16;
  return match;
}

}  // namespace rex::audio

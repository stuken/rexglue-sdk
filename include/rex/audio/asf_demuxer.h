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

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace rex::audio {

// Minimal single-audio-stream ASF (Advanced Systems Format) container
// demuxer, used to feed the vendored WMAv2/WMAPro codecs (already built into
// this project's FFmpeg, see thirdparty/CMakeLists.txt) real compressed WMA
// frames extracted from a real .wma file, without needing libavformat (which
// isn't built here - only libavutil+libavcodec are).
//
// Ported directly from FFmpeg's own libavformat/asfdec_f.c/asf.h (vendored
// on disk in thirdparty/FFmpeg even though not compiled into this project's
// CMake build - used here purely as an authoritative reference for the
// bit-packed packet format, not linked against), stripped of everything this
// use case doesn't need: timestamps/seeking, multi-stream muxing, video,
// encryption, and the AVFormatContext/AVIOContext plumbing. Only decodes
// against an in-memory buffer (a whole song file, matching how
// AudioMediaPlayer loads MP3s) - no streaming/incremental-parse support.
//
// Deliberately NOT handled (all rare enough for ripped-CD-style XMP audio to
// be an acceptable gap, and each fails closed - ReadNextFrame() just returns
// false/skips - rather than mis-parsing): DRM/content encryption, multiple
// audio streams in one file (only the first is used), and any packet whose
// replicated-data-length field isn't one of the two forms FFmpeg's own
// demuxer treats as valid (0/1/>=8 - "invalid" middle values 2-7).
class AsfDemuxer {
 public:
  struct AudioInfo {
    uint16_t format_tag = 0;
    uint16_t channels = 0;
    uint32_t sample_rate = 0;
    uint32_t avg_bytes_per_sec = 0;
    uint16_t block_align = 0;
    uint16_t bits_per_sample = 0;
    // Codec-specific data straight from the WAVEFORMATEX-shaped Type-Specific
    // Data in the Stream Properties Object - handed to AVCodecContext::
    // extradata verbatim; the WMA/WMAPro decoders parse it themselves.
    std::vector<uint8_t> extra_data;
  };

  // Parses the ASF Header Object: locates the file's packet size, the first
  // usable audio stream (if any), and the start of the Data Object. `data`
  // must stay valid for the lifetime of this object - it is referenced, not
  // copied. Returns false if this isn't a valid ASF file or has no audio
  // stream with a format this demuxer/the vendored codecs recognize.
  bool Open(const uint8_t* data, size_t size);

  const AudioInfo& audio_info() const { return audio_info_; }

  // Extracts the next complete compressed audio frame (a fragmented media
  // object reassembled across packets if needed, and de-interleaved if the
  // stream uses ASF's audio "spread" error-resilience scheme) belonging to
  // the tracked audio stream. Returns false at end of data or on an
  // unrecoverable parse error.
  bool ReadNextFrame(std::vector<uint8_t>* out_frame);

 private:
  bool ParseHeaderObject();
  bool ParseFilePropertiesObject(size_t object_body_end);
  bool ParseStreamPropertiesObject(size_t object_body_end);

  bool LoadNextPacket();
  // Reverses ASF's audio "spread" interleaving (used by some encoders for
  // error resilience) on a just-completed, fully reassembled media object,
  // if the stream uses it and the object's size matches one full
  // interleaved super-frame. No-op otherwise.
  void ApplyAudioSpreadDeinterleave(std::vector<uint8_t>* buffer) const;
  // Consumes one payload from the current packet, updating reassembly state
  // as needed. Returns false only on unrecoverable error; a false return
  // does not necessarily mean no frame was produced - check `frame_out` /
  // *out_frame separately via the completed flag.
  enum class PayloadResult { kNeedMore, kFrameReady, kError };
  PayloadResult ConsumeNextPayload(std::vector<uint8_t>* out_frame);

  // Byte-cursor helpers over the in-memory buffer. All return 0 (or false/
  // no-op) past end of buffer rather than reading out of bounds; callers
  // that need to distinguish real values from the OOB fallback check
  // `AtEnd()`/`Remaining()` first.
  bool AtEnd(size_t need = 1) const { return pos_ + need > size_; }
  size_t Remaining() const { return pos_ <= size_ ? size_ - pos_ : 0; }
  uint8_t ReadU8();
  uint16_t ReadU16LE();
  uint32_t ReadU32LE();
  uint64_t ReadU64LE();
  // Reads a 0/1/2/4-byte little-endian value per ASF's 2-bit "Length Type"
  // encoding (0=>default_value, 1=>1 byte, 2=>2 bytes, 3=>4 bytes).
  uint32_t ReadByWidthType(uint32_t width_type, uint32_t default_value);
  void Skip(size_t n);
  bool MatchGuid(const uint8_t guid[16]);

  const uint8_t* data_ = nullptr;
  size_t size_ = 0;
  size_t pos_ = 0;

  uint32_t packet_size_ = 0;  // File Properties Object's packet size (fixed on-disk slot size).
  size_t data_start_ = 0;     // Absolute offset of the first data packet.
  size_t data_end_ = 0;       // Absolute offset one past the last usable data byte.

  bool has_audio_stream_ = false;
  uint32_t audio_stream_id_ = 0;  // ASF stream number (1-127) of the tracked stream.
  AudioInfo audio_info_;

  // Audio "spread" de-interleaving parameters from the Stream Properties
  // Object; span <= 1 means the stream isn't interleaved.
  uint8_t ds_span_ = 0;
  uint16_t ds_packet_size_ = 0;
  uint16_t ds_chunk_size_ = 0;

  // Per-packet cursor state, valid only while `packet_loaded_` is true.
  bool packet_loaded_ = false;
  size_t packet_slot_end_ = 0;  // packet_start + packet_size_ (fixed on-disk slot).
  size_t packet_data_end_ = 0;  // End of real (non-padding) bytes in this packet.
  uint8_t packet_flags_ = 0;    // "Length Type Flags" byte.
  uint8_t packet_property_ = 0; // "Replicated Data" field-width flags, shared by every payload in this packet.
  uint8_t packet_segsizetype_ = 0;
  int packet_segments_left_ = 0;

  // "Compressed payload" chain state: several small frames sharing one
  // payload container, each self-delimited by a 1-byte length prefix.
  bool in_chain_ = false;
  uint32_t chain_stream_id_ = 0;
  size_t chain_end_ = 0;  // Absolute offset where the chain's payload container ends.

  // Fragment reassembly state for the tracked audio stream (only one stream
  // is ever tracked, so a single buffer suffices).
  std::vector<uint8_t> reassembly_buffer_;
  uint32_t reassembly_object_size_ = 0;
  uint32_t reassembly_offset_ = 0;
  bool reassembly_active_ = false;
};

}  // namespace rex::audio

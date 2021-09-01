/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <grpc/support/port_platform.h>

#include "src/core/ext/transport/chttp2/transport/hpack_encoder.h"

#include <assert.h>
#include <string.h>

/* This is here for grpc_is_binary_header
 * TODO(murgatroid99): Remove this
 */
#include <grpc/grpc.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/ext/transport/chttp2/transport/bin_encoder.h"
#include "src/core/ext/transport/chttp2/transport/hpack_utils.h"
#include "src/core/ext/transport/chttp2/transport/varint.h"
#include "src/core/lib/debug/stats.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/surface/validate_metadata.h"
#include "src/core/lib/transport/metadata.h"
#include "src/core/lib/transport/static_metadata.h"
#include "src/core/lib/transport/timeout_encoding.h"

namespace {
/* (Maybe-cuckoo) hpack encoder hash table implementation.

   This hashtable implementation is a subset of a proper cuckoo hash; while we
   have fallback cells that a value can be hashed to if the first cell is full,
   we do not attempt to iteratively rearrange entries into backup cells to get
   things to fit. Instead, if both a cell and the backup cell for a value are
   occupied, the older existing entry is evicted.

   Note that we can disable backup-cell picking by setting
   GRPC_HPACK_ENCODER_USE_CUCKOO_HASH to 0. In that case, we simply evict an
   existing entry rather than try to use a backup. Hence, "maybe-cuckoo."
   TODO(arjunroy): Add unit tests for hashtable implementation. */
#define GRPC_HPACK_ENCODER_USE_CUCKOO_HASH 1
#define HASH_FRAGMENT_MASK (GRPC_CHTTP2_HPACKC_NUM_VALUES - 1)
#define HASH_FRAGMENT_1(x) ((x)&HASH_FRAGMENT_MASK)
#define HASH_FRAGMENT_2(x) \
  (((x) >> GRPC_CHTTP2_HPACKC_NUM_VALUES_BITS) & HASH_FRAGMENT_MASK)
#define HASH_FRAGMENT_3(x) \
  (((x) >> (GRPC_CHTTP2_HPACKC_NUM_VALUES_BITS * 2)) & HASH_FRAGMENT_MASK)
#define HASH_FRAGMENT_4(x) \
  (((x) >> (GRPC_CHTTP2_HPACKC_NUM_VALUES_BITS * 3)) & HASH_FRAGMENT_MASK)

/* don't consider adding anything bigger than this to the hpack table */
constexpr size_t kMaxDecoderSpaceUsage = 512;
constexpr size_t kDataFrameHeaderSize = 9;

} /* namespace */

/* fills p (which is expected to be kDataFrameHeaderSize bytes long)
 * with a data frame header */
static void FillHeader(uint8_t* p, uint8_t type, uint32_t id, size_t len,
                       uint8_t flags) {
  /* len is the current frame size (i.e. for the frame we're finishing).
     We finish a frame if:
     1) We called ensure_space(), (i.e. add_tiny_header_data()) and adding
        'need_bytes' to the frame would cause us to exceed st->max_frame_size.
     2) We called add_header_data, and adding the slice would cause us to exceed
        st->max_frame_size.
     3) We're done encoding the header.

     Thus, len is always <= st->max_frame_size.
     st->max_frame_size is derived from GRPC_CHTTP2_SETTINGS_MAX_FRAME_SIZE,
     which has a max allowable value of 16777215 (see chttp_transport.cc).
     Thus, the following assert can be a debug assert. */
  GPR_DEBUG_ASSERT(len < 16777316);
  *p++ = static_cast<uint8_t>(len >> 16);
  *p++ = static_cast<uint8_t>(len >> 8);
  *p++ = static_cast<uint8_t>(len);
  *p++ = type;
  *p++ = flags;
  *p++ = static_cast<uint8_t>(id >> 24);
  *p++ = static_cast<uint8_t>(id >> 16);
  *p++ = static_cast<uint8_t>(id >> 8);
  *p++ = static_cast<uint8_t>(id);
}

size_t HPackCompressor::Framer::CurrentFrameSize() const {
  const size_t frame_size =
      output_->length - prefix_.output_length_at_start_of_frame;
  GPR_DEBUG_ASSERT(frame_size <= max_frame_size_);
  return frame_size;
}

// finish a frame - fill in the previously reserved header
void HPackCompressor::Framer::FinishFrame(bool is_header_boundary) {
  const uint8_t type = is_first_frame_ ? GRPC_CHTTP2_FRAME_HEADER
                                       : GRPC_CHTTP2_FRAME_CONTINUATION;
  uint8_t flags = 0;
  // per the HTTP/2 spec:
  //   A HEADERS frame carries the END_STREAM flag that signals the end of a
  //   stream. However, a HEADERS frame with the END_STREAM flag set can be
  //   followed by CONTINUATION frames on the same stream. Logically, the
  //   CONTINUATION frames are part of the HEADERS frame.
  // Thus, we add the END_STREAM flag to the HEADER frame (the first frame).
  if (is_first_frame_ && is_end_of_stream_) {
    flags |= GRPC_CHTTP2_DATA_FLAG_END_STREAM;
  }
  // per the HTTP/2 spec:
  //   A HEADERS frame without the END_HEADERS flag set MUST be followed by
  //   a CONTINUATION frame for the same stream.
  // Thus, we add the END_HEADER flag to the last frame.
  if (is_header_boundary) {
    flags |= GRPC_CHTTP2_DATA_FLAG_END_HEADERS;
  }
  FillHeader(GRPC_SLICE_START_PTR(output_->slices[prefix_.header_idx]), type,
             stream_id_, CurrentFrameSize(), flags);
  stats_->framing_bytes += kDataFrameHeaderSize;
  is_first_frame_ = false;
}

// begin a new frame: reserve off header space, remember how many bytes we'd
// output before beginning
HPackCompressor::Framer::FramePrefix HPackCompressor::Framer::BeginFrame() {
  grpc_slice reserved;
  reserved.refcount = nullptr;
  reserved.data.inlined.length = kDataFrameHeaderSize;
  return FramePrefix{grpc_slice_buffer_add_indexed(st->output, reserved),
                     st->output->length};
}

// make sure that the current frame is of the type desired, and has sufficient
// space to add at least about_to_add bytes -- finishes the current frame if
// needed
void HPackCompressor::Framer::EnsureSpace(size_t need_bytes) {
  if (GPR_LIKELY(CurrentFrameSize() + need_bytes <= max_frame_size_)) {
    return;
  }
  FinishFrame(0);
  prefix_ = BeginFrame();
}

void HPackCompressor::Framer::Add(grpc_slice slice) {
  const size_t len = GRPC_SLICE_LENGTH(slice);
  if (len == 0) return;
  const size_t remaining = st->max_frame_size - current_frame_size(st);
  if (len <= remaining) {
    stats_->header_bytes += len;
    grpc_slice_buffer_add(output_, slice);
  } else {
    stats_->header_bytes += remaining;
    grpc_slice_buffer_add(output_, grpc_slice_split_head(&slice, remaining));
    FinishFrame(0);
    prefix_ = BeginFrame();
    Add(slice);
  }
}

uint8_t* HPackCompressor::Framer::AddTiny(size_t len) {
  EnsureSpace(st, len);
  stats_->header_bytes += len;
  return grpc_slice_buffer_tiny_add(st->output, len);
}

// Add a key to the dynamic table. Both key and value will be added to table at
// the decoder.
void HPackCompressor::AddKeyWithIndex(grpc_slice_refcount* key_ref,
                                      uint32_t new_index, uint32_t key_hash) {
  key_index_.Insert(KeySliceRef(key_ref, key_hash), new_index);
}

/* add an element to the decoder table */
void HPackCompressor::AddElemWithIndex(grpc_mdelem elem, uint32_t new_index,
                                       uint32_t elem_hash, uint32_t key_hash) {
  GPR_DEBUG_ASSERT(GRPC_MDELEM_IS_INTERNED(elem));
  elem_index_->Insert(KeyElem(elem, elem_hash), new_index);
  AddKeyWithIndex(GRPC_MDKEY(elem).refcount, new_index, key_hash);
}

void HPackCompressor::AddElem(grpc_mdelem elem, size_t elem_size,
                              uint32_t elem_hash, uint32_t key_hash) {
  uint32_t new_index = table_.AllocateIndex(elem_size);
  if (new_index != 0) {
    AddElemWithIndex(elem, new_index, elem_hash, key_hash);
  }
}

void HPackCompressor::AddKey(grpc_mdelem elem, size_t elem_size,
                             uint32_t key_hash) {
  uint32_t new_index = table_.AllocateIndex(elem_size);
  if (new_index != 0) {
    AddKeyWithIndex(GRPC_MDKEY(elem).refcount, new_index, key_hash);
  }
}

void HPackCompressor::Framer::EmitIndexed(uint32_t elem_index) {
  GRPC_STATS_INC_HPACK_SEND_INDEXED();
  uint32_t len = GRPC_CHTTP2_VARINT_LENGTH(elem_index, 1);
  GRPC_CHTTP2_WRITE_VARINT(elem_index, 1, 0x80, AddTiny(len), len);
}

struct WireValue {
  WireValue(uint8_t huffman_prefix, bool insert_null_before_wire_value,
             const grpc_slice& slice)
      : data(slice),
        huffman_prefix(huffman_prefix),
        insert_null_before_wire_value(insert_null_before_wire_value),
        length(GRPC_SLICE_LENGTH(slice) +
               (insert_null_before_wire_value ? 1 : 0)) {}
  // While wire_value is const from the POV of hpack encoder code, actually
  // adding it to a slice buffer will possibly split the slice.
  const grpc_slice data;
  const uint8_t huffman_prefix;
  const bool insert_null_before_wire_value;
  const size_t length;
};

template <bool mdkey_definitely_interned>
static wire_value get_wire_value(grpc_mdelem elem, bool true_binary_enabled) {
  const bool is_bin_hdr =
      mdkey_definitely_interned
          ? grpc_is_refcounted_slice_binary_header(GRPC_MDKEY(elem))
          : grpc_is_binary_header_internal(GRPC_MDKEY(elem));
  const grpc_slice& value = GRPC_MDVALUE(elem);
  if (is_bin_hdr) {
    if (true_binary_enabled) {
      GRPC_STATS_INC_HPACK_SEND_BINARY();
      return wire_value(0x00, true, grpc_slice_ref_internal(value));
    } else {
      GRPC_STATS_INC_HPACK_SEND_BINARY_BASE64();
      return wire_value(0x80, false,
                        grpc_chttp2_base64_encode_and_huffman_compress(value));
    }
  } else {
    /* TODO(ctiller): opportunistically compress non-binary headers */
    GRPC_STATS_INC_HPACK_SEND_UNCOMPRESSED();
    return wire_value(0x00, false, grpc_slice_ref_internal(value));
  }
}

static uint32_t wire_value_length(const wire_value& v) {
  GPR_DEBUG_ASSERT(v.length <= UINT32_MAX);
  return static_cast<uint32_t>(v.length);
}

namespace {
enum class EmitLitHdrType { INC_IDX, NO_IDX };

enum class EmitLitHdrVType { INC_IDX_V, NO_IDX_V };
}  // namespace

template <EmitLitHdrType type>
static void emit_lithdr(grpc_chttp2_hpack_compressor* /*c*/, uint32_t key_index,
                        grpc_mdelem elem, framer_state* st) {
  switch (type) {
    case EmitLitHdrType::INC_IDX:
      GRPC_STATS_INC_HPACK_SEND_LITHDR_INCIDX();
      break;
    case EmitLitHdrType::NO_IDX:
      GRPC_STATS_INC_HPACK_SEND_LITHDR_NOTIDX();
      break;
  }
  const uint32_t len_pfx = type == EmitLitHdrType::INC_IDX
                               ? GRPC_CHTTP2_VARINT_LENGTH(key_index, 2)
                               : GRPC_CHTTP2_VARINT_LENGTH(key_index, 4);
  const wire_value value =
      get_wire_value<true>(elem, st->use_true_binary_metadata);
  const uint32_t len_val = wire_value_length(value);
  const uint32_t len_val_len = GRPC_CHTTP2_VARINT_LENGTH(len_val, 1);
  GPR_DEBUG_ASSERT(len_pfx + len_val_len < GRPC_SLICE_INLINED_SIZE);
  uint8_t* data = add_tiny_header_data(
      st,
      len_pfx + len_val_len + (value.insert_null_before_wire_value ? 1 : 0));
  switch (type) {
    case EmitLitHdrType::INC_IDX:
      GRPC_CHTTP2_WRITE_VARINT(key_index, 2, 0x40, data, len_pfx);
      break;
    case EmitLitHdrType::NO_IDX:
      GRPC_CHTTP2_WRITE_VARINT(key_index, 4, 0x00, data, len_pfx);
      break;
  }
  GRPC_CHTTP2_WRITE_VARINT(len_val, 1, value.huffman_prefix, &data[len_pfx],
                           len_val_len);
  if (value.insert_null_before_wire_value) {
    data[len_pfx + len_val_len] = 0;
  }
  Add(value.data);
}

template <EmitLitHdrVType type>
static void emit_lithdr_v(grpc_chttp2_hpack_compressor* /*c*/, grpc_mdelem elem,
                          framer_state* st) {
  switch (type) {
    case EmitLitHdrVType::INC_IDX_V:
      GRPC_STATS_INC_HPACK_SEND_LITHDR_INCIDX_V();
      break;
    case EmitLitHdrVType::NO_IDX_V:
      GRPC_STATS_INC_HPACK_SEND_LITHDR_NOTIDX_V();
      break;
  }
  GRPC_STATS_INC_HPACK_SEND_UNCOMPRESSED();
  const uint32_t len_key =
      static_cast<uint32_t>(GRPC_SLICE_LENGTH(GRPC_MDKEY(elem)));
  const wire_value value =
      type == EmitLitHdrVType::INC_IDX_V
          ? get_wire_value<true>(elem, st->use_true_binary_metadata)
          : get_wire_value<false>(elem, st->use_true_binary_metadata);
  const uint32_t len_val = wire_value_length(value);
  const uint32_t len_key_len = GRPC_CHTTP2_VARINT_LENGTH(len_key, 1);
  GPR_DEBUG_ASSERT(len_key <= UINT32_MAX);
  GPR_DEBUG_ASSERT(1 + len_key_len < GRPC_SLICE_INLINED_SIZE);
  uint8_t* key_buf = add_tiny_header_data(st, 1 + len_key_len);
  key_buf[0] = type == EmitLitHdrVType::INC_IDX_V ? 0x40 : 0x00;
  GRPC_CHTTP2_WRITE_VARINT(len_key, 1, 0x00, &key_buf[1], len_key_len);
  add_header_data(st, grpc_slice_ref_internal(GRPC_MDKEY(elem)));
  const uint32_t len_val_len = GRPC_CHTTP2_VARINT_LENGTH(len_val, 1);
  uint8_t* value_buf = add_tiny_header_data(
      st, len_val_len + (value.insert_null_before_wire_value ? 1 : 0));
  GRPC_CHTTP2_WRITE_VARINT(len_val, 1, value.huffman_prefix, value_buf,
                           len_val_len);
  if (value.insert_null_before_wire_value) {
    value_buf[len_val_len] = 0;
  }
  Add(st, value.data);
}

void HPackCompressor::Framer::AdvertiseTableSizeChange() {
  uint32_t len = GRPC_CHTTP2_VARINT_LENGTH(compressor_->table_.max_size(), 3);
  GRPC_CHTTP2_WRITE_VARINT(compressor_->table_.max_size(), 3, 0x20,
                           AddTiny(len), len);
}

void HPackCompressor::Framer::Log(grpc_mdelem elem) {
  char* k = grpc_slice_to_c_string(GRPC_MDKEY(elem));
  char* v = nullptr;
  if (grpc_is_binary_header_internal(GRPC_MDKEY(elem))) {
    v = grpc_dump_slice(GRPC_MDVALUE(elem), GPR_DUMP_HEX);
  } else {
    v = grpc_slice_to_c_string(GRPC_MDVALUE(elem));
  }
  gpr_log(
      GPR_INFO,
      "Encode: '%s: %s', elem_interned=%d [%d], k_interned=%d, v_interned=%d",
      k, v, GRPC_MDELEM_IS_INTERNED(elem), GRPC_MDELEM_STORAGE(elem),
      grpc_slice_is_interned(GRPC_MDKEY(elem)),
      grpc_slice_is_interned(GRPC_MDVALUE(elem)));
  gpr_free(k);
  gpr_free(v);
}

struct EmitIndexedStatus {
  EmitIndexedStatus() = default;
  EmitIndexedStatus(uint32_t elem_hash, bool emitted, bool can_add)
      : elem_hash(elem_hash), emitted(emitted), can_add(can_add) {}
  const uint32_t elem_hash = 0;
  const bool emitted = false;
  const bool can_add = false;
};

static EmitIndexedStatus maybe_emit_indexed(grpc_chttp2_hpack_compressor* c,
                                            grpc_mdelem elem,
                                            framer_state* st) {
  const uint32_t elem_hash =
      GRPC_MDELEM_STORAGE(elem) == GRPC_MDELEM_STORAGE_INTERNED
          ? reinterpret_cast<grpc_core::InternedMetadata*>(
                GRPC_MDELEM_DATA(elem))
                ->hash()
          : reinterpret_cast<grpc_core::StaticMetadata*>(GRPC_MDELEM_DATA(elem))
                ->hash();
  // Update filter to see if we can perhaps add this elem.
  bool can_add_to_hashtable =
      c->filter_elems->AddElement(HASH_FRAGMENT_1(elem_hash));
  /* is this elem currently in the decoders table? */
  auto indices_key = c->elem_index->Lookup(KeyElem(elem, elem_hash));
  if (indices_key.has_value() &&
      c->table->ConvertableToDynamicIndex(*indices_key)) {
    emit_indexed(c, c->table->DynamicIndex(*indices_key), st);
    return EmitIndexedStatus(elem_hash, true, false);
  }
  /* Didn't hit either cuckoo index, so no emit. */
  return EmitIndexedStatus(elem_hash, false, can_add_to_hashtable);
}

static void emit_maybe_add(grpc_chttp2_hpack_compressor* c, grpc_mdelem elem,
                           framer_state* st, uint32_t indices_key,
                           bool should_add_elem, size_t decoder_space_usage,
                           uint32_t elem_hash, uint32_t key_hash) {
  if (should_add_elem) {
    emit_lithdr<EmitLitHdrType::INC_IDX>(c, c->table->DynamicIndex(indices_key),
                                         elem, st);
    add_elem(c, elem, decoder_space_usage, elem_hash, key_hash);
  } else {
    emit_lithdr<EmitLitHdrType::NO_IDX>(c, c->table->DynamicIndex(indices_key),
                                        elem, st);
  }
}

/* encode an mdelem */
void HPackCompressor::Framer::EncodeDynamic(grpc_mdelem elem) {
  const grpc_slice& elem_key = GRPC_MDKEY(elem);
  // User-provided key len validated in grpc_validate_header_key_is_legal().
  GPR_DEBUG_ASSERT(GRPC_SLICE_LENGTH(elem_key) > 0);
  // Header ordering: all reserved headers (prefixed with ':') must precede
  // regular headers. This can be a debug assert, since:
  // 1) User cannot give us ':' headers (grpc_validate_header_key_is_legal()).
  // 2) grpc filters/core should be checked during debug builds. */
#ifndef NDEBUG
  if (GRPC_SLICE_START_PTR(elem_key)[0] != ':') { /* regular header */
    seen_regular_header_ = 1;
  } else {
    GPR_DEBUG_ASSERT(
        !seen_regular_header_ &&
        "Reserved header (colon-prefixed) happening after regular ones.");
  }
#endif
  if (GRPC_TRACE_FLAG_ENABLED(grpc_http_trace)) {
    Log(elem);
  }
  const bool elem_interned = GRPC_MDELEM_IS_INTERNED(elem);
  const bool key_interned = elem_interned || grpc_slice_is_interned(elem_key);
  // Key is not interned, emit literals.
  if (!key_interned) {
    emit_lithdr_v<EmitLitHdrVType::NO_IDX_V>(c, elem, st);
    return;
  }
  /* Interned metadata => maybe already indexed. */
  const EmitIndexedStatus ret =
      elem_interned ? maybe_emit_indexed(c, elem, st) : EmitIndexedStatus();
  if (ret.emitted) {
    return;
  }
  /* should this elem be in the table? */
  const size_t decoder_space_usage =
      grpc_core::MetadataSizeInHPackTable(elem, st->use_true_binary_metadata);
  const bool decoder_space_available =
      decoder_space_usage < kMaxDecoderSpaceUsage;
  const bool should_add_elem =
      elem_interned && decoder_space_available && ret.can_add;
  const uint32_t elem_hash = ret.elem_hash;
  /* no hits for the elem... maybe there's a key? */
  const uint32_t key_hash = elem_key.refcount->Hash(elem_key);
  auto indices_key =
      c->key_index->Lookup(KeySliceRef(elem_key.refcount, key_hash));
  if (indices_key.has_value() &&
      c->table->ConvertableToDynamicIndex(*indices_key)) {
    emit_maybe_add(c, elem, st, *indices_key, should_add_elem,
                   decoder_space_usage, elem_hash, key_hash);
    return;
  }
  /* no elem, key in the table... fall back to literal emission */
  const bool should_add_key = !elem_interned && decoder_space_available;
  if (should_add_elem || should_add_key) {
    emit_lithdr_v<EmitLitHdrVType::INC_IDX_V>(c, elem, st);
  } else {
    emit_lithdr_v<EmitLitHdrVType::NO_IDX_V>(c, elem, st);
  }
  if (should_add_elem) {
    add_elem(c, elem, decoder_space_usage, elem_hash, key_hash);
  } else if (should_add_key) {
    add_key(c, elem, decoder_space_usage, key_hash);
  }
}

#define STRLEN_LIT(x) (sizeof(x) - 1)
#define TIMEOUT_KEY "grpc-timeout"

void HPackCompressor::Framer::EncodeDeadline(grpc_millis deadline) {
  char timeout_str[GRPC_HTTP2_TIMEOUT_ENCODE_MIN_BUFSIZE];
  grpc_mdelem mdelem;
  grpc_http2_encode_timeout(deadline - grpc_core::ExecCtx::Get()->Now(),
                            timeout_str);
  mdelem = grpc_mdelem_from_slices(
      GRPC_MDSTR_GRPC_TIMEOUT, grpc_core::UnmanagedMemorySlice(timeout_str));
  EncodeDynamic(mdelem);
  GRPC_MDELEM_UNREF(mdelem);
}

void HPackCompressor::SetMaxUsableSize(uint32_t max_table_size) {
  max_usable_size_ = max_table_size;
  SetMaxTableSize(std::min(table_.max_size(), max_table_size));
}

void HPackCompressor::SetMaxTableSize(uint32_t max_table_size) {
  if (table_.SetMaxSize(std::min(max_usable_size_, max_table_size))) {
    advertise_table_size_change_ = true;
    if (GRPC_TRACE_FLAG_ENABLED(grpc_http_trace)) {
      gpr_log(GPR_INFO, "set max table size from encoder to %d",
              max_table_size);
    }
  }
}

HPackCompressor::Framer::Framer(const EncodeHeaderOptions& options,
                                HPackCompressor* compressor,
                                grpc_slice_buffer* output)
    : max_frame_size_(options.max_frame_size),
      use_true_binary_metadata_(options.use_true_binary_metadata),
      is_end_of_stream_(options.is_end_of_stream),
      stream_id_(options.stream_id_),
      output_(output),
      stats_(options.stats),
      prefix_(BeginFrame()) {
  if (absl::exchange(compressor_->advertise_table_size_change_, false)) {
    AdvertiseTableSizeChange();
  }
}

void HPackCompressor::Framer::Encode(grpc_mdelem md) {
  if (GRPC_MDELEM_STORAGE(md) == GRPC_MDELEM_STORAGE_STATIC) {
    const uintptr_t static_index =
        reinterpret_cast<grpc_core::StaticMetadata*>(GRPC_MDELEM_DATA(md))
            ->StaticIndex();
    if (static_index < hpack_constants::kLastStaticEntry) {
      EmitIndexed(static_cast<uint32_t>(static_index + 1));
      return;
    }
  }
  EncodeDynamic(md);
}

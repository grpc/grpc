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
#include "src/core/ext/transport/chttp2/transport/hpack_table.h"
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
constexpr uint8_t kMaxFilterValue = 255;

/* if the probability of this item being seen again is < 1/x then don't add
   it to the table */
#define ONE_ON_ADD_PROBABILITY (GRPC_CHTTP2_HPACKC_NUM_VALUES >> 1)
/* The hpack index we encode over the wire. Meaningful to the hpack encoder and
   parser on the remote end as well as HTTP2. *Not* the same as
   HpackEncoderSlotHash, which is only meaningful to the hpack encoder
   implementation (HpackEncoderSlotHash is used for the hashtable implementation
   when mapping from metadata to HpackEncoderIndex. */
typedef uint32_t HpackEncoderIndex;
/* Internal-table bookkeeping (*not* the hpack index). */
typedef uint32_t HpackEncoderSlotHash;

struct SliceRefComparator {
  typedef grpc_slice_refcount* Type;
  static grpc_slice_refcount* Null() { return nullptr; }
  static bool IsNull(const grpc_slice_refcount* sref) {
    return sref == nullptr;
  }
  static bool Equals(const grpc_slice_refcount* s1,
                     const grpc_slice_refcount* s2) {
    return s1 == s2;
  }
  static void Ref(grpc_slice_refcount* sref) {
    GPR_DEBUG_ASSERT(sref != nullptr);
    sref->Ref();
  }
  static void Unref(grpc_slice_refcount* sref) {
    GPR_DEBUG_ASSERT(sref != nullptr);
    sref->Unref();
  }
};

struct MetadataComparator {
  typedef grpc_mdelem Type;
  static const grpc_mdelem Null() { return {0}; }
  static bool IsNull(const grpc_mdelem md) { return md.payload == 0; }
  static bool Equals(const grpc_mdelem md1, const grpc_mdelem md2) {
    return md1.payload == md2.payload;
  }
  static void Ref(grpc_mdelem md) {
    GPR_DEBUG_ASSERT(md.payload != 0);
    GRPC_MDELEM_REF(md);
  }
  static void Unref(grpc_mdelem md) {
    GPR_DEBUG_ASSERT(md.payload != 0);
    GRPC_MDELEM_UNREF(md);
  }
};

/* Index table management */
template <typename Hashtable>
static HpackEncoderIndex HpackIndex(const Hashtable* hashtable,
                                    HpackEncoderSlotHash hash_index) {
  return hashtable[hash_index].index;
}

template <typename ValueType, typename Hashtable>
static const ValueType& GetEntry(const Hashtable* hashtable,
                                 HpackEncoderSlotHash hash_index) {
  return hashtable[hash_index].value;
}

template <typename Cmp, typename Hashtable>
static bool TableEmptyAt(const Hashtable* hashtable,
                         HpackEncoderSlotHash hash_index) {
  return Cmp::Equals(hashtable[hash_index].value, Cmp::Null());
}

template <typename Cmp, typename Hashtable, typename ValueType>
static bool Matches(const Hashtable* hashtable, const ValueType& value,
                    HpackEncoderSlotHash hash_index) {
  return Cmp::Equals(value, hashtable[hash_index].value);
}

template <typename Hashtable>
static void UpdateIndex(Hashtable* hashtable, HpackEncoderSlotHash hash_index,
                        HpackEncoderIndex hpack_index) {
  hashtable[hash_index].index = hpack_index;
}

template <typename Hashtable, typename ValueType>
static void SetIndex(Hashtable* hashtable, HpackEncoderSlotHash hash_index,
                     const ValueType& value, HpackEncoderIndex hpack_index) {
  hashtable[hash_index].value = value;
  UpdateIndex(hashtable, hash_index, hpack_index);
}

template <typename Cmp, typename Hashtable, typename ValueType>
static bool GetMatchingIndex(Hashtable* hashtable, const ValueType& value,
                             uint32_t value_hash, HpackEncoderIndex* index) {
  const HpackEncoderSlotHash cuckoo_first = HASH_FRAGMENT_2(value_hash);
  if (Matches<Cmp>(hashtable, value, cuckoo_first)) {
    *index = HpackIndex(hashtable, cuckoo_first);
    return true;
  }
#if GRPC_HPACK_ENCODER_USE_CUCKOO_HASH
  const HpackEncoderSlotHash cuckoo_second = HASH_FRAGMENT_3(value_hash);

  if (Matches<Cmp>(hashtable, value, cuckoo_second)) {
    *index = HpackIndex(hashtable, cuckoo_second);
    return true;
  }
#endif
  return false;
}

template <typename Cmp, typename Hashtable, typename ValueType>
static ValueType ReplaceOlderIndex(Hashtable* hashtable, const ValueType& value,
                                   HpackEncoderSlotHash hash_index_a,
                                   HpackEncoderSlotHash hash_index_b,
                                   HpackEncoderIndex new_index) {
  const HpackEncoderIndex hpack_idx_a = hashtable[hash_index_a].index;
  const HpackEncoderIndex hpack_idx_b = hashtable[hash_index_b].index;
  const HpackEncoderSlotHash id =
      hpack_idx_a < hpack_idx_b ? hash_index_a : hash_index_b;
  ValueType old = GetEntry<typename Cmp::Type>(hashtable, id);
  SetIndex(hashtable, id, value, new_index);
  return old;
}

template <typename Cmp, typename Hashtable, typename ValueType>
static void UpdateAddOrEvict(Hashtable hashtable, const ValueType& value,
                             uint32_t value_hash, HpackEncoderIndex new_index) {
  const HpackEncoderSlotHash cuckoo_first = HASH_FRAGMENT_2(value_hash);
  if (Matches<Cmp>(hashtable, value, cuckoo_first)) {
    UpdateIndex(hashtable, cuckoo_first, new_index);
    return;
  }
  if (TableEmptyAt<Cmp>(hashtable, cuckoo_first)) {
    Cmp::Ref(value);
    SetIndex(hashtable, cuckoo_first, value, new_index);
    return;
  }
#if GRPC_HPACK_ENCODER_USE_CUCKOO_HASH
  const HpackEncoderSlotHash cuckoo_second = HASH_FRAGMENT_3(value_hash);
  if (Matches<Cmp>(hashtable, value, cuckoo_second)) {
    UpdateIndex(hashtable, cuckoo_second, new_index);
    return;
  }
  Cmp::Ref(value);
  if (TableEmptyAt<Cmp>(hashtable, cuckoo_second)) {
    SetIndex(hashtable, cuckoo_second, value, new_index);
    return;
  }
  Cmp::Unref(ReplaceOlderIndex<Cmp>(hashtable, value, cuckoo_first,
                                    cuckoo_second, new_index));
#else
  ValueType old = GetEntry<typename Cmp::Type>(hashtable, cuckoo_first);
  SetIndex(hashtable, cuckoo_first, value, new_index);
  Cmp::Unref(old);
#endif
}

/* halve all counts because an element reached max */
static void HalveFilter(uint8_t idx, uint32_t* sum, uint8_t* elems) {
  *sum = 0;
  for (int i = 0; i < GRPC_CHTTP2_HPACKC_NUM_VALUES; i++) {
    elems[i] /= 2;
    (*sum) += elems[i];
  }
}

/* increment a filter count, halve all counts if one element reaches max */
static void IncrementFilter(uint8_t idx, uint32_t* sum, uint8_t* elems) {
  elems[idx]++;
  if (GPR_LIKELY(elems[idx] < kMaxFilterValue)) {
    (*sum)++;
  } else {
    HalveFilter(idx, sum, elems);
  }
}

static uint32_t UpdateHashtablePopularity(
    grpc_chttp2_hpack_compressor* hpack_compressor, uint32_t elem_hash) {
  const uint32_t popularity_hash = HASH_FRAGMENT_1(elem_hash);
  IncrementFilter(popularity_hash, &hpack_compressor->filter_elems_sum,
                  hpack_compressor->filter_elems);
  return popularity_hash;
}

static bool CanAddToHashtable(grpc_chttp2_hpack_compressor* hpack_compressor,
                              uint32_t popularity_hash) {
  const bool can_add =
      hpack_compressor->filter_elems[popularity_hash] >=
      hpack_compressor->filter_elems_sum / ONE_ON_ADD_PROBABILITY;
  return can_add;
}
} /* namespace */

typedef struct {
  int is_first_frame;
  /* number of bytes in 'output' when we started the frame - used to calculate
     frame length */
  size_t output_length_at_start_of_frame;
  /* index (in output) of the header for the current frame */
  size_t header_idx;
#ifndef NDEBUG
  /* have we seen a regular (non-colon-prefixed) header yet? */
  uint8_t seen_regular_header;
#endif
  /* output stream id */
  uint32_t stream_id;
  grpc_slice_buffer* output;
  grpc_transport_one_way_stats* stats;
  /* maximum size of a frame */
  size_t max_frame_size;
  bool use_true_binary_metadata;
} framer_state;

/* fills p (which is expected to be kDataFrameHeaderSize bytes long)
 * with a data frame header */
static void fill_header(uint8_t* p, uint8_t type, uint32_t id, size_t len,
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

static size_t current_frame_size(framer_state* st) {
  const size_t frame_size =
      st->output->length - st->output_length_at_start_of_frame;
  GPR_DEBUG_ASSERT(frame_size <= st->max_frame_size);
  return frame_size;
}

/* finish a frame - fill in the previously reserved header */
static void finish_frame(framer_state* st, int is_header_boundary,
                         int is_last_in_stream) {
  uint8_t type = 0xff;
  type = st->is_first_frame ? GRPC_CHTTP2_FRAME_HEADER
                            : GRPC_CHTTP2_FRAME_CONTINUATION;
  fill_header(
      GRPC_SLICE_START_PTR(st->output->slices[st->header_idx]), type,
      st->stream_id, current_frame_size(st),
      static_cast<uint8_t>(
          (is_last_in_stream ? GRPC_CHTTP2_DATA_FLAG_END_STREAM : 0) |
          (is_header_boundary ? GRPC_CHTTP2_DATA_FLAG_END_HEADERS : 0)));
  st->stats->framing_bytes += kDataFrameHeaderSize;
  st->is_first_frame = 0;
}

/* begin a new frame: reserve off header space, remember how many bytes we'd
   output before beginning */
static void begin_frame(framer_state* st) {
  grpc_slice reserved;
  reserved.refcount = nullptr;
  reserved.data.inlined.length = kDataFrameHeaderSize;
  st->header_idx = grpc_slice_buffer_add_indexed(st->output, reserved);
  st->output_length_at_start_of_frame = st->output->length;
}

/* make sure that the current frame is of the type desired, and has sufficient
   space to add at least about_to_add bytes -- finishes the current frame if
   needed */
static void ensure_space(framer_state* st, size_t need_bytes) {
  if (GPR_LIKELY(current_frame_size(st) + need_bytes <= st->max_frame_size)) {
    return;
  }
  finish_frame(st, 0, 0);
  begin_frame(st);
}

static void add_header_data(framer_state* st, grpc_slice slice) {
  size_t len = GRPC_SLICE_LENGTH(slice);
  size_t remaining;
  if (len == 0) return;
  remaining = st->max_frame_size - current_frame_size(st);
  if (len <= remaining) {
    st->stats->header_bytes += len;
    grpc_slice_buffer_add(st->output, slice);
  } else {
    st->stats->header_bytes += remaining;
    grpc_slice_buffer_add(st->output, grpc_slice_split_head(&slice, remaining));
    finish_frame(st, 0, 0);
    begin_frame(st);
    add_header_data(st, slice);
  }
}

static uint8_t* add_tiny_header_data(framer_state* st, size_t len) {
  ensure_space(st, len);
  st->stats->header_bytes += len;
  return grpc_slice_buffer_tiny_add(st->output, len);
}

static void evict_entry(grpc_chttp2_hpack_compressor* c) {
  c->tail_remote_index++;
  GPR_ASSERT(c->tail_remote_index > 0);
  GPR_ASSERT(c->table_size >=
             c->table_elem_size[c->tail_remote_index % c->cap_table_elems]);
  GPR_ASSERT(c->table_elems > 0);
  c->table_size = static_cast<uint16_t>(
      c->table_size -
      c->table_elem_size[c->tail_remote_index % c->cap_table_elems]);
  c->table_elems--;
}

// Reserve space in table for the new element, evict entries if needed.
// Return the new index of the element. Return 0 to indicate not adding to
// table.
static uint32_t prepare_space_for_new_elem(grpc_chttp2_hpack_compressor* c,
                                           size_t elem_size) {
  uint32_t new_index = c->tail_remote_index + c->table_elems + 1;
  GPR_DEBUG_ASSERT(elem_size < 65536);

  // TODO(arjunroy): Re-examine semantics
  if (elem_size > c->max_table_size) {
    while (c->table_size > 0) {
      evict_entry(c);
    }
    return 0;
  }

  /* Reserve space for this element in the remote table: if this overflows
     the current table, drop elements until it fits, matching the decompressor
     algorithm */
  while (c->table_size + elem_size > c->max_table_size) {
    evict_entry(c);
  }
  GPR_ASSERT(c->table_elems < c->max_table_size);
  c->table_elem_size[new_index % c->cap_table_elems] =
      static_cast<uint16_t>(elem_size);
  c->table_size = static_cast<uint16_t>(c->table_size + elem_size);
  c->table_elems++;

  return new_index;
}

// Add a key to the dynamic table. Both key and value will be added to table at
// the decoder.
static void AddKeyWithIndex(grpc_chttp2_hpack_compressor* c,
                            grpc_slice_refcount* key_ref, uint32_t new_index,
                            uint32_t key_hash) {
  UpdateAddOrEvict<SliceRefComparator>(c->key_table.entries, key_ref, key_hash,
                                       new_index);
}

/* add an element to the decoder table */
static void AddElemWithIndex(grpc_chttp2_hpack_compressor* c, grpc_mdelem elem,
                             uint32_t new_index, uint32_t elem_hash,
                             uint32_t key_hash) {
  GPR_DEBUG_ASSERT(GRPC_MDELEM_IS_INTERNED(elem));
  UpdateAddOrEvict<MetadataComparator>(c->elem_table.entries, elem, elem_hash,
                                       new_index);
  AddKeyWithIndex(c, GRPC_MDKEY(elem).refcount, new_index, key_hash);
}

static void add_elem(grpc_chttp2_hpack_compressor* c, grpc_mdelem elem,
                     size_t elem_size, uint32_t elem_hash, uint32_t key_hash) {
  uint32_t new_index = prepare_space_for_new_elem(c, elem_size);
  if (new_index != 0) {
    AddElemWithIndex(c, elem, new_index, elem_hash, key_hash);
  }
}

static void add_key(grpc_chttp2_hpack_compressor* c, grpc_mdelem elem,
                    size_t elem_size, uint32_t key_hash) {
  uint32_t new_index = prepare_space_for_new_elem(c, elem_size);
  if (new_index != 0) {
    AddKeyWithIndex(c, GRPC_MDKEY(elem).refcount, new_index, key_hash);
  }
}

static void emit_indexed(grpc_chttp2_hpack_compressor* c, uint32_t elem_index,
                         framer_state* st) {
  GRPC_STATS_INC_HPACK_SEND_INDEXED();
  uint32_t len = GRPC_CHTTP2_VARINT_LENGTH(elem_index, 1);
  GRPC_CHTTP2_WRITE_VARINT(elem_index, 1, 0x80, add_tiny_header_data(st, len),
                           len);
}

struct wire_value {
  wire_value(uint8_t huffman_prefix, bool insert_null_before_wire_value,
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
static void emit_lithdr(grpc_chttp2_hpack_compressor* c, uint32_t key_index,
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
  add_header_data(st, value.data);
}

template <EmitLitHdrVType type>
static void emit_lithdr_v(grpc_chttp2_hpack_compressor* c, grpc_mdelem elem,
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
  const uint32_t len_val_len = GRPC_CHTTP2_VARINT_LENGTH(len_val, 1);
  GPR_DEBUG_ASSERT(len_key <= UINT32_MAX);
  GPR_DEBUG_ASSERT(1 + len_key_len < GRPC_SLICE_INLINED_SIZE);
  uint8_t* key_buf = add_tiny_header_data(st, 1 + len_key_len);
  key_buf[0] = type == EmitLitHdrVType::INC_IDX_V ? 0x40 : 0x00;
  GRPC_CHTTP2_WRITE_VARINT(len_key, 1, 0x00, &key_buf[1], len_key_len);
  add_header_data(st, grpc_slice_ref_internal(GRPC_MDKEY(elem)));
  uint8_t* value_buf = add_tiny_header_data(
      st, len_val_len + (value.insert_null_before_wire_value ? 1 : 0));
  GRPC_CHTTP2_WRITE_VARINT(len_val, 1, value.huffman_prefix, value_buf,
                           len_val_len);
  if (value.insert_null_before_wire_value) {
    value_buf[len_val_len] = 0;
  }
  add_header_data(st, value.data);
}

static void emit_advertise_table_size_change(grpc_chttp2_hpack_compressor* c,
                                             framer_state* st) {
  uint32_t len = GRPC_CHTTP2_VARINT_LENGTH(c->max_table_size, 3);
  GRPC_CHTTP2_WRITE_VARINT(c->max_table_size, 3, 0x20,
                           add_tiny_header_data(st, len), len);
  c->advertise_table_size_change = 0;
}

static void GPR_ATTRIBUTE_NOINLINE hpack_enc_log(grpc_mdelem elem) {
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

static uint32_t dynidx(grpc_chttp2_hpack_compressor* c, uint32_t elem_index) {
  return 1 + GRPC_CHTTP2_LAST_STATIC_ENTRY + c->tail_remote_index +
         c->table_elems - elem_index;
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
  /* Update filter to see if we can perhaps add this elem. */
  const uint32_t popularity_hash = UpdateHashtablePopularity(c, elem_hash);
  /* is this elem currently in the decoders table? */
  HpackEncoderIndex indices_key;
  if (GetMatchingIndex<MetadataComparator>(c->elem_table.entries, elem,
                                           elem_hash, &indices_key) &&
      indices_key > c->tail_remote_index) {
    emit_indexed(c, dynidx(c, indices_key), st);
    return EmitIndexedStatus(elem_hash, true, false);
  }
  /* Didn't hit either cuckoo index, so no emit. */
  return EmitIndexedStatus(elem_hash, false,
                           CanAddToHashtable(c, popularity_hash));
}

static void emit_maybe_add(grpc_chttp2_hpack_compressor* c, grpc_mdelem elem,
                           framer_state* st, uint32_t indices_key,
                           bool should_add_elem, size_t decoder_space_usage,
                           uint32_t elem_hash, uint32_t key_hash) {
  if (should_add_elem) {
    emit_lithdr<EmitLitHdrType::INC_IDX>(c, dynidx(c, indices_key), elem, st);
    add_elem(c, elem, decoder_space_usage, elem_hash, key_hash);
  } else {
    emit_lithdr<EmitLitHdrType::NO_IDX>(c, dynidx(c, indices_key), elem, st);
  }
}

/* encode an mdelem */
static void hpack_enc(grpc_chttp2_hpack_compressor* c, grpc_mdelem elem,
                      framer_state* st) {
  const grpc_slice& elem_key = GRPC_MDKEY(elem);
  /* User-provided key len validated in grpc_validate_header_key_is_legal(). */
  GPR_DEBUG_ASSERT(GRPC_SLICE_LENGTH(elem_key) > 0);
  /* Header ordering: all reserved headers (prefixed with ':') must precede
   * regular headers. This can be a debug assert, since:
   * 1) User cannot give us ':' headers (grpc_validate_header_key_is_legal()).
   * 2) grpc filters/core should be checked during debug builds. */
#ifndef NDEBUG
  if (GRPC_SLICE_START_PTR(elem_key)[0] != ':') { /* regular header */
    st->seen_regular_header = 1;
  } else {
    GPR_DEBUG_ASSERT(
        st->seen_regular_header == 0 &&
        "Reserved header (colon-prefixed) happening after regular ones.");
  }
#endif
  if (GRPC_TRACE_FLAG_ENABLED(grpc_http_trace)) {
    hpack_enc_log(elem);
  }
  const bool elem_interned = GRPC_MDELEM_IS_INTERNED(elem);
  const bool key_interned = elem_interned || grpc_slice_is_interned(elem_key);
  /* Key is not interned, emit literals. */
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
      grpc_chttp2_get_size_in_hpack_table(elem, st->use_true_binary_metadata);
  const bool decoder_space_available =
      decoder_space_usage < kMaxDecoderSpaceUsage;
  const bool should_add_elem =
      elem_interned && decoder_space_available && ret.can_add;
  const uint32_t elem_hash = ret.elem_hash;
  /* no hits for the elem... maybe there's a key? */
  const uint32_t key_hash = elem_key.refcount->Hash(elem_key);
  HpackEncoderIndex indices_key;
  if (GetMatchingIndex<SliceRefComparator>(
          c->key_table.entries, elem_key.refcount, key_hash, &indices_key) &&
      indices_key > c->tail_remote_index) {
    emit_maybe_add(c, elem, st, indices_key, should_add_elem,
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

static void deadline_enc(grpc_chttp2_hpack_compressor* c, grpc_millis deadline,
                         framer_state* st) {
  char timeout_str[GRPC_HTTP2_TIMEOUT_ENCODE_MIN_BUFSIZE];
  grpc_mdelem mdelem;
  grpc_http2_encode_timeout(deadline - grpc_core::ExecCtx::Get()->Now(),
                            timeout_str);
  mdelem = grpc_mdelem_from_slices(
      GRPC_MDSTR_GRPC_TIMEOUT, grpc_core::UnmanagedMemorySlice(timeout_str));
  hpack_enc(c, mdelem, st);
  GRPC_MDELEM_UNREF(mdelem);
}

static uint32_t elems_for_bytes(uint32_t bytes) { return (bytes + 31) / 32; }

void grpc_chttp2_hpack_compressor_init(grpc_chttp2_hpack_compressor* c) {
  memset(c, 0, sizeof(*c));
  c->max_table_size = GRPC_CHTTP2_HPACKC_INITIAL_TABLE_SIZE;
  c->cap_table_elems = elems_for_bytes(c->max_table_size);
  c->max_table_elems = c->cap_table_elems;
  c->max_usable_size = GRPC_CHTTP2_HPACKC_INITIAL_TABLE_SIZE;
  const size_t alloc_size = sizeof(*c->table_elem_size) * c->cap_table_elems;
  c->table_elem_size = static_cast<uint16_t*>(gpr_malloc(alloc_size));
  memset(c->table_elem_size, 0, alloc_size);
}

void grpc_chttp2_hpack_compressor_destroy(grpc_chttp2_hpack_compressor* c) {
  for (int i = 0; i < GRPC_CHTTP2_HPACKC_NUM_VALUES; i++) {
    auto* const key = GetEntry<grpc_slice_refcount*>(c->key_table.entries, i);
    if (key != nullptr) {
      key->Unref();
    }
    GRPC_MDELEM_UNREF(GetEntry<grpc_mdelem>(c->elem_table.entries, i));
  }
  gpr_free(c->table_elem_size);
}

void grpc_chttp2_hpack_compressor_set_max_usable_size(
    grpc_chttp2_hpack_compressor* c, uint32_t max_table_size) {
  c->max_usable_size = max_table_size;
  grpc_chttp2_hpack_compressor_set_max_table_size(
      c, GPR_MIN(c->max_table_size, max_table_size));
}

static void rebuild_elems(grpc_chttp2_hpack_compressor* c, uint32_t new_cap) {
  uint16_t* table_elem_size =
      static_cast<uint16_t*>(gpr_malloc(sizeof(*table_elem_size) * new_cap));
  uint32_t i;

  memset(table_elem_size, 0, sizeof(*table_elem_size) * new_cap);
  GPR_ASSERT(c->table_elems <= new_cap);

  for (i = 0; i < c->table_elems; i++) {
    uint32_t ofs = c->tail_remote_index + i + 1;
    table_elem_size[ofs % new_cap] =
        c->table_elem_size[ofs % c->cap_table_elems];
  }

  c->cap_table_elems = new_cap;
  gpr_free(c->table_elem_size);
  c->table_elem_size = table_elem_size;
}

void grpc_chttp2_hpack_compressor_set_max_table_size(
    grpc_chttp2_hpack_compressor* c, uint32_t max_table_size) {
  max_table_size = GPR_MIN(max_table_size, c->max_usable_size);
  if (max_table_size == c->max_table_size) {
    return;
  }
  while (c->table_size > 0 && c->table_size > max_table_size) {
    evict_entry(c);
  }
  c->max_table_size = max_table_size;
  c->max_table_elems = elems_for_bytes(max_table_size);
  if (c->max_table_elems > c->cap_table_elems) {
    rebuild_elems(c, GPR_MAX(c->max_table_elems, 2 * c->cap_table_elems));
  } else if (c->max_table_elems < c->cap_table_elems / 3) {
    uint32_t new_cap = GPR_MAX(c->max_table_elems, 16);
    if (new_cap != c->cap_table_elems) {
      rebuild_elems(c, new_cap);
    }
  }
  c->advertise_table_size_change = 1;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_http_trace)) {
    gpr_log(GPR_INFO, "set max table size from encoder to %d", max_table_size);
  }
}

void grpc_chttp2_encode_header(grpc_chttp2_hpack_compressor* c,
                               grpc_mdelem** extra_headers,
                               size_t extra_headers_size,
                               grpc_metadata_batch* metadata,
                               const grpc_encode_header_options* options,
                               grpc_slice_buffer* outbuf) {
  /* grpc_chttp2_encode_header is called by FlushInitial/TrailingMetadata in
     writing.cc. Specifically, on streams returned by NextStream(), which
     returns streams from the list GRPC_CHTTP2_LIST_WRITABLE. The only way to be
     added to the list is via  grpc_chttp2_list_add_writable_stream(), which
     validates that stream_id is not 0. So, this can be a debug assert. */
  GPR_DEBUG_ASSERT(options->stream_id != 0);
  framer_state st;
#ifndef NDEBUG
  st.seen_regular_header = 0;
#endif
  st.stream_id = options->stream_id;
  st.output = outbuf;
  st.is_first_frame = 1;
  st.stats = options->stats;
  st.max_frame_size = options->max_frame_size;
  st.use_true_binary_metadata = options->use_true_binary_metadata;

  /* Encode a metadata batch; store the returned values, representing
     a metadata element that needs to be unreffed back into the metadata
     slot. THIS MAY NOT BE THE SAME ELEMENT (if a decoder table slot got
     updated). After this loop, we'll do a batch unref of elements. */
  begin_frame(&st);
  if (c->advertise_table_size_change != 0) {
    emit_advertise_table_size_change(c, &st);
  }
  for (size_t i = 0; i < extra_headers_size; ++i) {
    grpc_mdelem md = *extra_headers[i];
    const bool is_static =
        GRPC_MDELEM_STORAGE(md) == GRPC_MDELEM_STORAGE_STATIC;
    uintptr_t static_index;
    if (is_static &&
        (static_index =
             reinterpret_cast<grpc_core::StaticMetadata*>(GRPC_MDELEM_DATA(md))
                 ->StaticIndex()) < GRPC_CHTTP2_LAST_STATIC_ENTRY) {
      emit_indexed(c, static_cast<uint32_t>(static_index + 1), &st);
    } else {
      hpack_enc(c, md, &st);
    }
  }
  grpc_metadata_batch_assert_ok(metadata);
  for (grpc_linked_mdelem* l = metadata->list.head; l; l = l->next) {
    const bool is_static =
        GRPC_MDELEM_STORAGE(l->md) == GRPC_MDELEM_STORAGE_STATIC;
    uintptr_t static_index;
    if (is_static &&
        (static_index = reinterpret_cast<grpc_core::StaticMetadata*>(
                            GRPC_MDELEM_DATA(l->md))
                            ->StaticIndex()) < GRPC_CHTTP2_LAST_STATIC_ENTRY) {
      emit_indexed(c, static_cast<uint32_t>(static_index + 1), &st);
    } else {
      hpack_enc(c, l->md, &st);
    }
  }
  grpc_millis deadline = metadata->deadline;
  if (deadline != GRPC_MILLIS_INF_FUTURE) {
    deadline_enc(c, deadline, &st);
  }

  finish_frame(&st, 1, options->is_eof);
}

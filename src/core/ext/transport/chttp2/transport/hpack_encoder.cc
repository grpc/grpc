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
#include "src/core/lib/transport/metadata.h"
#include "src/core/lib/transport/static_metadata.h"
#include "src/core/lib/transport/timeout_encoding.h"

#define HASH_FRAGMENT_MASK (GRPC_CHTTP2_HPACKC_NUM_VALUES - 1)
#define HASH_FRAGMENT_1(x) ((x)&HASH_FRAGMENT_MASK)
#define HASH_FRAGMENT_2(x) \
  (((x) >> GRPC_CHTTP2_HPACKC_NUM_VALUES_BITS) & HASH_FRAGMENT_MASK)
#define HASH_FRAGMENT_3(x) \
  (((x) >> (GRPC_CHTTP2_HPACKC_NUM_VALUES_BITS * 2)) & HASH_FRAGMENT_MASK)
#define HASH_FRAGMENT_4(x) \
  (((x) >> (GRPC_CHTTP2_HPACKC_NUM_VALUES_BITS * 3)) & HASH_FRAGMENT_MASK)

/* if the probability of this item being seen again is < 1/x then don't add
   it to the table */
#define ONE_ON_ADD_PROBABILITY (GRPC_CHTTP2_HPACKC_NUM_VALUES >> 1)
/* don't consider adding anything bigger than this to the hpack table */
#define MAX_DECODER_SPACE_USAGE 512

static grpc_slice_refcount terminal_slice_refcount = {nullptr, nullptr};
static const grpc_slice terminal_slice = {
    &terminal_slice_refcount, /* refcount */
    {{0, nullptr}}            /* data.refcounted */
};

typedef struct {
  int is_first_frame;
  /* number of bytes in 'output' when we started the frame - used to calculate
     frame length */
  size_t output_length_at_start_of_frame;
  /* index (in output) of the header for the current frame */
  size_t header_idx;
  /* have we seen a regular (non-colon-prefixed) header yet? */
  uint8_t seen_regular_header;
  /* output stream id */
  uint32_t stream_id;
  grpc_slice_buffer* output;
  grpc_transport_one_way_stats* stats;
  /* maximum size of a frame */
  size_t max_frame_size;
  bool use_true_binary_metadata;
} framer_state;

/* fills p (which is expected to be 9 bytes long) with a data frame header */
static void fill_header(uint8_t* p, uint8_t type, uint32_t id, size_t len,
                        uint8_t flags) {
  GPR_ASSERT(len < 16777316);
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

/* finish a frame - fill in the previously reserved header */
static void finish_frame(framer_state* st, int is_header_boundary,
                         int is_last_in_stream) {
  uint8_t type = 0xff;
  type = st->is_first_frame ? GRPC_CHTTP2_FRAME_HEADER
                            : GRPC_CHTTP2_FRAME_CONTINUATION;
  fill_header(
      GRPC_SLICE_START_PTR(st->output->slices[st->header_idx]), type,
      st->stream_id, st->output->length - st->output_length_at_start_of_frame,
      static_cast<uint8_t>(
          (is_last_in_stream ? GRPC_CHTTP2_DATA_FLAG_END_STREAM : 0) |
          (is_header_boundary ? GRPC_CHTTP2_DATA_FLAG_END_HEADERS : 0)));
  st->stats->framing_bytes += 9;
  st->is_first_frame = 0;
}

/* begin a new frame: reserve off header space, remember how many bytes we'd
   output before beginning */
static void begin_frame(framer_state* st) {
  st->header_idx =
      grpc_slice_buffer_add_indexed(st->output, GRPC_SLICE_MALLOC(9));
  st->output_length_at_start_of_frame = st->output->length;
}

/* make sure that the current frame is of the type desired, and has sufficient
   space to add at least about_to_add bytes -- finishes the current frame if
   needed */
static void ensure_space(framer_state* st, size_t need_bytes) {
  if (st->output->length - st->output_length_at_start_of_frame + need_bytes <=
      st->max_frame_size) {
    return;
  }
  finish_frame(st, 0, 0);
  begin_frame(st);
}

/* increment a filter count, halve all counts if one element reaches max */
static void inc_filter(uint8_t idx, uint32_t* sum, uint8_t* elems) {
  elems[idx]++;
  if (elems[idx] < 255) {
    (*sum)++;
  } else {
    int i;
    *sum = 0;
    for (i = 0; i < GRPC_CHTTP2_HPACKC_NUM_VALUES; i++) {
      elems[i] /= 2;
      (*sum) += elems[i];
    }
  }
}

static void add_header_data(framer_state* st, grpc_slice slice) {
  size_t len = GRPC_SLICE_LENGTH(slice);
  size_t remaining;
  if (len == 0) return;
  remaining = st->max_frame_size + st->output_length_at_start_of_frame -
              st->output->length;
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
  GPR_ASSERT(elem_size < 65536);

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
static void add_key_with_index(grpc_chttp2_hpack_compressor* c,
                               grpc_mdelem elem, uint32_t new_index) {
  if (new_index == 0) {
    return;
  }

  uint32_t key_hash = grpc_slice_hash(GRPC_MDKEY(elem));

  /* Store the key into {entries,indices}_keys */
  if (grpc_slice_eq(c->entries_keys[HASH_FRAGMENT_2(key_hash)],
                    GRPC_MDKEY(elem))) {
    c->indices_keys[HASH_FRAGMENT_2(key_hash)] = new_index;
  } else if (grpc_slice_eq(c->entries_keys[HASH_FRAGMENT_3(key_hash)],
                           GRPC_MDKEY(elem))) {
    c->indices_keys[HASH_FRAGMENT_3(key_hash)] = new_index;
  } else if (c->entries_keys[HASH_FRAGMENT_2(key_hash)].refcount ==
             &terminal_slice_refcount) {
    c->entries_keys[HASH_FRAGMENT_2(key_hash)] =
        grpc_slice_ref_internal(GRPC_MDKEY(elem));
    c->indices_keys[HASH_FRAGMENT_2(key_hash)] = new_index;
  } else if (c->entries_keys[HASH_FRAGMENT_3(key_hash)].refcount ==
             &terminal_slice_refcount) {
    c->entries_keys[HASH_FRAGMENT_3(key_hash)] =
        grpc_slice_ref_internal(GRPC_MDKEY(elem));
    c->indices_keys[HASH_FRAGMENT_3(key_hash)] = new_index;
  } else if (c->indices_keys[HASH_FRAGMENT_2(key_hash)] <
             c->indices_keys[HASH_FRAGMENT_3(key_hash)]) {
    grpc_slice_unref_internal(c->entries_keys[HASH_FRAGMENT_2(key_hash)]);
    c->entries_keys[HASH_FRAGMENT_2(key_hash)] =
        grpc_slice_ref_internal(GRPC_MDKEY(elem));
    c->indices_keys[HASH_FRAGMENT_2(key_hash)] = new_index;
  } else {
    grpc_slice_unref_internal(c->entries_keys[HASH_FRAGMENT_3(key_hash)]);
    c->entries_keys[HASH_FRAGMENT_3(key_hash)] =
        grpc_slice_ref_internal(GRPC_MDKEY(elem));
    c->indices_keys[HASH_FRAGMENT_3(key_hash)] = new_index;
  }
}

/* add an element to the decoder table */
static void add_elem_with_index(grpc_chttp2_hpack_compressor* c,
                                grpc_mdelem elem, uint32_t new_index) {
  if (new_index == 0) {
    return;
  }
  GPR_ASSERT(GRPC_MDELEM_IS_INTERNED(elem));

  uint32_t key_hash = grpc_slice_hash(GRPC_MDKEY(elem));
  uint32_t value_hash = grpc_slice_hash(GRPC_MDVALUE(elem));
  uint32_t elem_hash = GRPC_MDSTR_KV_HASH(key_hash, value_hash);

  /* Store this element into {entries,indices}_elem */
  if (grpc_mdelem_eq(c->entries_elems[HASH_FRAGMENT_2(elem_hash)], elem)) {
    /* already there: update with new index */
    c->indices_elems[HASH_FRAGMENT_2(elem_hash)] = new_index;
  } else if (grpc_mdelem_eq(c->entries_elems[HASH_FRAGMENT_3(elem_hash)],
                            elem)) {
    /* already there (cuckoo): update with new index */
    c->indices_elems[HASH_FRAGMENT_3(elem_hash)] = new_index;
  } else if (GRPC_MDISNULL(c->entries_elems[HASH_FRAGMENT_2(elem_hash)])) {
    /* not there, but a free element: add */
    c->entries_elems[HASH_FRAGMENT_2(elem_hash)] = GRPC_MDELEM_REF(elem);
    c->indices_elems[HASH_FRAGMENT_2(elem_hash)] = new_index;
  } else if (GRPC_MDISNULL(c->entries_elems[HASH_FRAGMENT_3(elem_hash)])) {
    /* not there (cuckoo), but a free element: add */
    c->entries_elems[HASH_FRAGMENT_3(elem_hash)] = GRPC_MDELEM_REF(elem);
    c->indices_elems[HASH_FRAGMENT_3(elem_hash)] = new_index;
  } else if (c->indices_elems[HASH_FRAGMENT_2(elem_hash)] <
             c->indices_elems[HASH_FRAGMENT_3(elem_hash)]) {
    /* not there: replace oldest */
    GRPC_MDELEM_UNREF(c->entries_elems[HASH_FRAGMENT_2(elem_hash)]);
    c->entries_elems[HASH_FRAGMENT_2(elem_hash)] = GRPC_MDELEM_REF(elem);
    c->indices_elems[HASH_FRAGMENT_2(elem_hash)] = new_index;
  } else {
    /* not there: replace oldest */
    GRPC_MDELEM_UNREF(c->entries_elems[HASH_FRAGMENT_3(elem_hash)]);
    c->entries_elems[HASH_FRAGMENT_3(elem_hash)] = GRPC_MDELEM_REF(elem);
    c->indices_elems[HASH_FRAGMENT_3(elem_hash)] = new_index;
  }

  add_key_with_index(c, elem, new_index);
}

static void add_elem(grpc_chttp2_hpack_compressor* c, grpc_mdelem elem,
                     size_t elem_size) {
  uint32_t new_index = prepare_space_for_new_elem(c, elem_size);
  add_elem_with_index(c, elem, new_index);
}

static void add_key(grpc_chttp2_hpack_compressor* c, grpc_mdelem elem,
                    size_t elem_size) {
  uint32_t new_index = prepare_space_for_new_elem(c, elem_size);
  add_key_with_index(c, elem, new_index);
}

static void emit_indexed(grpc_chttp2_hpack_compressor* c, uint32_t elem_index,
                         framer_state* st) {
  GRPC_STATS_INC_HPACK_SEND_INDEXED();
  uint32_t len = GRPC_CHTTP2_VARINT_LENGTH(elem_index, 1);
  GRPC_CHTTP2_WRITE_VARINT(elem_index, 1, 0x80, add_tiny_header_data(st, len),
                           len);
}

typedef struct {
  grpc_slice data;
  uint8_t huffman_prefix;
  bool insert_null_before_wire_value;
} wire_value;

static wire_value get_wire_value(grpc_mdelem elem, bool true_binary_enabled) {
  wire_value wire_val;
  if (grpc_is_binary_header(GRPC_MDKEY(elem))) {
    if (true_binary_enabled) {
      GRPC_STATS_INC_HPACK_SEND_BINARY();
      wire_val.huffman_prefix = 0x00;
      wire_val.insert_null_before_wire_value = true;
      wire_val.data = grpc_slice_ref_internal(GRPC_MDVALUE(elem));

    } else {
      GRPC_STATS_INC_HPACK_SEND_BINARY_BASE64();
      wire_val.huffman_prefix = 0x80;
      wire_val.insert_null_before_wire_value = false;
      wire_val.data =
          grpc_chttp2_base64_encode_and_huffman_compress(GRPC_MDVALUE(elem));
    }
  } else {
    /* TODO(ctiller): opportunistically compress non-binary headers */
    GRPC_STATS_INC_HPACK_SEND_UNCOMPRESSED();
    wire_val.huffman_prefix = 0x00;
    wire_val.insert_null_before_wire_value = false;
    wire_val.data = grpc_slice_ref_internal(GRPC_MDVALUE(elem));
  }
  return wire_val;
}

static size_t wire_value_length(wire_value v) {
  return GPR_SLICE_LENGTH(v.data) + v.insert_null_before_wire_value;
}

static void add_wire_value(framer_state* st, wire_value v) {
  if (v.insert_null_before_wire_value) *add_tiny_header_data(st, 1) = 0;
  add_header_data(st, v.data);
}

static void emit_lithdr_incidx(grpc_chttp2_hpack_compressor* c,
                               uint32_t key_index, grpc_mdelem elem,
                               framer_state* st) {
  GRPC_STATS_INC_HPACK_SEND_LITHDR_INCIDX();
  uint32_t len_pfx = GRPC_CHTTP2_VARINT_LENGTH(key_index, 2);
  wire_value value = get_wire_value(elem, st->use_true_binary_metadata);
  size_t len_val = wire_value_length(value);
  uint32_t len_val_len;
  GPR_ASSERT(len_val <= UINT32_MAX);
  len_val_len = GRPC_CHTTP2_VARINT_LENGTH((uint32_t)len_val, 1);
  GRPC_CHTTP2_WRITE_VARINT(key_index, 2, 0x40,
                           add_tiny_header_data(st, len_pfx), len_pfx);
  GRPC_CHTTP2_WRITE_VARINT((uint32_t)len_val, 1, value.huffman_prefix,
                           add_tiny_header_data(st, len_val_len), len_val_len);
  add_wire_value(st, value);
}

static void emit_lithdr_noidx(grpc_chttp2_hpack_compressor* c,
                              uint32_t key_index, grpc_mdelem elem,
                              framer_state* st) {
  GRPC_STATS_INC_HPACK_SEND_LITHDR_NOTIDX();
  uint32_t len_pfx = GRPC_CHTTP2_VARINT_LENGTH(key_index, 4);
  wire_value value = get_wire_value(elem, st->use_true_binary_metadata);
  size_t len_val = wire_value_length(value);
  uint32_t len_val_len;
  GPR_ASSERT(len_val <= UINT32_MAX);
  len_val_len = GRPC_CHTTP2_VARINT_LENGTH((uint32_t)len_val, 1);
  GRPC_CHTTP2_WRITE_VARINT(key_index, 4, 0x00,
                           add_tiny_header_data(st, len_pfx), len_pfx);
  GRPC_CHTTP2_WRITE_VARINT((uint32_t)len_val, 1, value.huffman_prefix,
                           add_tiny_header_data(st, len_val_len), len_val_len);
  add_wire_value(st, value);
}

static void emit_lithdr_incidx_v(grpc_chttp2_hpack_compressor* c,
                                 uint32_t unused_index, grpc_mdelem elem,
                                 framer_state* st) {
  GPR_ASSERT(unused_index == 0);
  GRPC_STATS_INC_HPACK_SEND_LITHDR_INCIDX_V();
  GRPC_STATS_INC_HPACK_SEND_UNCOMPRESSED();
  uint32_t len_key = static_cast<uint32_t> GRPC_SLICE_LENGTH(GRPC_MDKEY(elem));
  wire_value value = get_wire_value(elem, st->use_true_binary_metadata);
  uint32_t len_val = static_cast<uint32_t>(wire_value_length(value));
  uint32_t len_key_len = GRPC_CHTTP2_VARINT_LENGTH(len_key, 1);
  uint32_t len_val_len = GRPC_CHTTP2_VARINT_LENGTH(len_val, 1);
  GPR_ASSERT(len_key <= UINT32_MAX);
  GPR_ASSERT(wire_value_length(value) <= UINT32_MAX);
  *add_tiny_header_data(st, 1) = 0x40;
  GRPC_CHTTP2_WRITE_VARINT(len_key, 1, 0x00,
                           add_tiny_header_data(st, len_key_len), len_key_len);
  add_header_data(st, grpc_slice_ref_internal(GRPC_MDKEY(elem)));
  GRPC_CHTTP2_WRITE_VARINT(len_val, 1, value.huffman_prefix,
                           add_tiny_header_data(st, len_val_len), len_val_len);
  add_wire_value(st, value);
}

static void emit_lithdr_noidx_v(grpc_chttp2_hpack_compressor* c,
                                uint32_t unused_index, grpc_mdelem elem,
                                framer_state* st) {
  GPR_ASSERT(unused_index == 0);
  GRPC_STATS_INC_HPACK_SEND_LITHDR_NOTIDX_V();
  GRPC_STATS_INC_HPACK_SEND_UNCOMPRESSED();
  uint32_t len_key = static_cast<uint32_t> GRPC_SLICE_LENGTH(GRPC_MDKEY(elem));
  wire_value value = get_wire_value(elem, st->use_true_binary_metadata);
  uint32_t len_val = static_cast<uint32_t>(wire_value_length(value));
  uint32_t len_key_len = GRPC_CHTTP2_VARINT_LENGTH(len_key, 1);
  uint32_t len_val_len = GRPC_CHTTP2_VARINT_LENGTH(len_val, 1);
  GPR_ASSERT(len_key <= UINT32_MAX);
  GPR_ASSERT(wire_value_length(value) <= UINT32_MAX);
  *add_tiny_header_data(st, 1) = 0x00;
  GRPC_CHTTP2_WRITE_VARINT(len_key, 1, 0x00,
                           add_tiny_header_data(st, len_key_len), len_key_len);
  add_header_data(st, grpc_slice_ref_internal(GRPC_MDKEY(elem)));
  GRPC_CHTTP2_WRITE_VARINT(len_val, 1, value.huffman_prefix,
                           add_tiny_header_data(st, len_val_len), len_val_len);
  add_wire_value(st, value);
}

static void emit_advertise_table_size_change(grpc_chttp2_hpack_compressor* c,
                                             framer_state* st) {
  uint32_t len = GRPC_CHTTP2_VARINT_LENGTH(c->max_table_size, 3);
  GRPC_CHTTP2_WRITE_VARINT(c->max_table_size, 3, 0x20,
                           add_tiny_header_data(st, len), len);
  c->advertise_table_size_change = 0;
}

static uint32_t dynidx(grpc_chttp2_hpack_compressor* c, uint32_t elem_index) {
  return 1 + GRPC_CHTTP2_LAST_STATIC_ENTRY + c->tail_remote_index +
         c->table_elems - elem_index;
}

/* encode an mdelem */
static void hpack_enc(grpc_chttp2_hpack_compressor* c, grpc_mdelem elem,
                      framer_state* st) {
  GPR_ASSERT(GRPC_SLICE_LENGTH(GRPC_MDKEY(elem)) > 0);
  if (GRPC_SLICE_START_PTR(GRPC_MDKEY(elem))[0] != ':') { /* regular header */
    st->seen_regular_header = 1;
  } else {
    GPR_ASSERT(
        st->seen_regular_header == 0 &&
        "Reserved header (colon-prefixed) happening after regular ones.");
  }

  if (grpc_http_trace.enabled()) {
    char* k = grpc_slice_to_c_string(GRPC_MDKEY(elem));
    char* v = nullptr;
    if (grpc_is_binary_header(GRPC_MDKEY(elem))) {
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

  bool elem_interned = GRPC_MDELEM_IS_INTERNED(elem);
  bool key_interned = elem_interned || grpc_slice_is_interned(GRPC_MDKEY(elem));

  // Key is not interned, emit literals.
  if (!key_interned) {
    emit_lithdr_noidx_v(c, 0, elem, st);
    return;
  }

  uint32_t key_hash = grpc_slice_hash(GRPC_MDKEY(elem));
  uint32_t elem_hash = 0;

  if (elem_interned) {
    uint32_t value_hash = grpc_slice_hash(GRPC_MDVALUE(elem));
    elem_hash = GRPC_MDSTR_KV_HASH(key_hash, value_hash);

    inc_filter(HASH_FRAGMENT_1(elem_hash), &c->filter_elems_sum,
               c->filter_elems);

    /* is this elem currently in the decoders table? */

    if (grpc_mdelem_eq(c->entries_elems[HASH_FRAGMENT_2(elem_hash)], elem) &&
        c->indices_elems[HASH_FRAGMENT_2(elem_hash)] > c->tail_remote_index) {
      /* HIT: complete element (first cuckoo hash) */
      emit_indexed(c, dynidx(c, c->indices_elems[HASH_FRAGMENT_2(elem_hash)]),
                   st);
      return;
    }

    if (grpc_mdelem_eq(c->entries_elems[HASH_FRAGMENT_3(elem_hash)], elem) &&
        c->indices_elems[HASH_FRAGMENT_3(elem_hash)] > c->tail_remote_index) {
      /* HIT: complete element (second cuckoo hash) */
      emit_indexed(c, dynidx(c, c->indices_elems[HASH_FRAGMENT_3(elem_hash)]),
                   st);
      return;
    }
  }

  uint32_t indices_key;

  /* should this elem be in the table? */
  const size_t decoder_space_usage =
      grpc_chttp2_get_size_in_hpack_table(elem, st->use_true_binary_metadata);
  const bool should_add_elem = elem_interned &&
                               decoder_space_usage < MAX_DECODER_SPACE_USAGE &&
                               c->filter_elems[HASH_FRAGMENT_1(elem_hash)] >=
                                   c->filter_elems_sum / ONE_ON_ADD_PROBABILITY;

  auto emit_maybe_add = [&should_add_elem, &elem, &st, &c, &indices_key,
                         &decoder_space_usage] {
    if (should_add_elem) {
      emit_lithdr_incidx(c, dynidx(c, indices_key), elem, st);
      add_elem(c, elem, decoder_space_usage);
    } else {
      emit_lithdr_noidx(c, dynidx(c, indices_key), elem, st);
    }
  };

  /* no hits for the elem... maybe there's a key? */
  indices_key = c->indices_keys[HASH_FRAGMENT_2(key_hash)];
  if (grpc_slice_eq(c->entries_keys[HASH_FRAGMENT_2(key_hash)],
                    GRPC_MDKEY(elem)) &&
      indices_key > c->tail_remote_index) {
    /* HIT: key (first cuckoo hash) */
    emit_maybe_add();
    return;
  }

  indices_key = c->indices_keys[HASH_FRAGMENT_3(key_hash)];
  if (grpc_slice_eq(c->entries_keys[HASH_FRAGMENT_3(key_hash)],
                    GRPC_MDKEY(elem)) &&
      indices_key > c->tail_remote_index) {
    /* HIT: key (first cuckoo hash) */
    emit_maybe_add();
    return;
  }

  /* no elem, key in the table... fall back to literal emission */
  const bool should_add_key =
      !elem_interned && decoder_space_usage < MAX_DECODER_SPACE_USAGE;
  if (should_add_elem || should_add_key) {
    emit_lithdr_incidx_v(c, 0, elem, st);
  } else {
    emit_lithdr_noidx_v(c, 0, elem, st);
  }
  if (should_add_elem) {
    add_elem(c, elem, decoder_space_usage);
  } else if (should_add_key) {
    add_key(c, elem, decoder_space_usage);
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
  mdelem = grpc_mdelem_from_slices(GRPC_MDSTR_GRPC_TIMEOUT,
                                   grpc_slice_from_copied_string(timeout_str));
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
  c->table_elem_size = static_cast<uint16_t*>(
      gpr_malloc(sizeof(*c->table_elem_size) * c->cap_table_elems));
  memset(c->table_elem_size, 0,
         sizeof(*c->table_elem_size) * c->cap_table_elems);
  for (size_t i = 0; i < GPR_ARRAY_SIZE(c->entries_keys); i++) {
    c->entries_keys[i] = terminal_slice;
  }
}

void grpc_chttp2_hpack_compressor_destroy(grpc_chttp2_hpack_compressor* c) {
  int i;
  for (i = 0; i < GRPC_CHTTP2_HPACKC_NUM_VALUES; i++) {
    if (c->entries_keys[i].refcount != &terminal_slice_refcount) {
      grpc_slice_unref_internal(c->entries_keys[i]);
    }
    GRPC_MDELEM_UNREF(c->entries_elems[i]);
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
  if (grpc_http_trace.enabled()) {
    gpr_log(GPR_INFO, "set max table size from encoder to %d", max_table_size);
  }
}

void grpc_chttp2_encode_header(grpc_chttp2_hpack_compressor* c,
                               grpc_mdelem** extra_headers,
                               size_t extra_headers_size,
                               grpc_metadata_batch* metadata,
                               const grpc_encode_header_options* options,
                               grpc_slice_buffer* outbuf) {
  GPR_ASSERT(options->stream_id != 0);

  framer_state st;
  st.seen_regular_header = 0;
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
    uint8_t static_index = grpc_chttp2_get_static_hpack_table_index(md);
    if (static_index) {
      emit_indexed(c, static_index, &st);
    } else {
      hpack_enc(c, md, &st);
    }
  }
  grpc_metadata_batch_assert_ok(metadata);
  for (grpc_linked_mdelem* l = metadata->list.head; l; l = l->next) {
    uint8_t static_index = grpc_chttp2_get_static_hpack_table_index(l->md);
    if (static_index) {
      emit_indexed(c, static_index, &st);
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

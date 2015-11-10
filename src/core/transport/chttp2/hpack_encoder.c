/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "src/core/transport/chttp2/hpack_encoder.h"

#include <assert.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/useful.h>

#include "src/core/transport/chttp2/bin_encoder.h"
#include "src/core/transport/chttp2/hpack_table.h"
#include "src/core/transport/chttp2/timeout_encoding.h"
#include "src/core/transport/chttp2/varint.h"

#define HASH_FRAGMENT_1(x) ((x)&255)
#define HASH_FRAGMENT_2(x) ((x >> 8) & 255)
#define HASH_FRAGMENT_3(x) ((x >> 16) & 255)
#define HASH_FRAGMENT_4(x) ((x >> 24) & 255)

/* if the probability of this item being seen again is < 1/x then don't add
   it to the table */
#define ONE_ON_ADD_PROBABILITY 128
/* don't consider adding anything bigger than this to the hpack table */
#define MAX_DECODER_SPACE_USAGE 512

typedef struct {
  int is_first_frame;
  /* number of bytes in 'output' when we started the frame - used to calculate
     frame length */
  size_t output_length_at_start_of_frame;
  /* index (in output) of the header for the current frame */
  size_t header_idx;
  /* have we seen a regular (non-colon-prefixed) header yet? */
  gpr_uint8 seen_regular_header;
  /* output stream id */
  gpr_uint32 stream_id;
  gpr_slice_buffer *output;
} framer_state;

/* fills p (which is expected to be 9 bytes long) with a data frame header */
static void fill_header(gpr_uint8 *p, gpr_uint8 type, gpr_uint32 id, size_t len,
                        gpr_uint8 flags) {
  GPR_ASSERT(len < 16777316);
  *p++ = (gpr_uint8)(len >> 16);
  *p++ = (gpr_uint8)(len >> 8);
  *p++ = (gpr_uint8)(len);
  *p++ = type;
  *p++ = flags;
  *p++ = (gpr_uint8)(id >> 24);
  *p++ = (gpr_uint8)(id >> 16);
  *p++ = (gpr_uint8)(id >> 8);
  *p++ = (gpr_uint8)(id);
}

/* finish a frame - fill in the previously reserved header */
static void finish_frame(framer_state *st, int is_header_boundary,
                         int is_last_in_stream) {
  gpr_uint8 type = 0xff;
  type = st->is_first_frame ? GRPC_CHTTP2_FRAME_HEADER
                            : GRPC_CHTTP2_FRAME_CONTINUATION;
  fill_header(
      GPR_SLICE_START_PTR(st->output->slices[st->header_idx]), type,
      st->stream_id, st->output->length - st->output_length_at_start_of_frame,
      (gpr_uint8)(
          (is_last_in_stream ? GRPC_CHTTP2_DATA_FLAG_END_STREAM : 0) |
          (is_header_boundary ? GRPC_CHTTP2_DATA_FLAG_END_HEADERS : 0)));
  st->is_first_frame = 0;
}

/* begin a new frame: reserve off header space, remember how many bytes we'd
   output before beginning */
static void begin_frame(framer_state *st) {
  st->header_idx =
      gpr_slice_buffer_add_indexed(st->output, gpr_slice_malloc(9));
  st->output_length_at_start_of_frame = st->output->length;
}

/* make sure that the current frame is of the type desired, and has sufficient
   space to add at least about_to_add bytes -- finishes the current frame if
   needed */
static void ensure_space(framer_state *st, size_t need_bytes) {
  if (st->output->length - st->output_length_at_start_of_frame + need_bytes <=
      GRPC_CHTTP2_MAX_PAYLOAD_LENGTH) {
    return;
  }
  finish_frame(st, 0, 0);
  begin_frame(st);
}

/* increment a filter count, halve all counts if one element reaches max */
static void inc_filter(gpr_uint8 idx, gpr_uint32 *sum, gpr_uint8 *elems) {
  elems[idx]++;
  if (elems[idx] < 255) {
    (*sum)++;
  } else {
    int i;
    *sum = 0;
    for (i = 0; i < GRPC_CHTTP2_HPACKC_NUM_FILTERS; i++) {
      elems[i] /= 2;
      (*sum) += elems[i];
    }
  }
}

static void add_header_data(framer_state *st, gpr_slice slice) {
  size_t len = GPR_SLICE_LENGTH(slice);
  size_t remaining;
  if (len == 0) return;
  remaining = GRPC_CHTTP2_MAX_PAYLOAD_LENGTH +
              st->output_length_at_start_of_frame - st->output->length;
  if (len <= remaining) {
    gpr_slice_buffer_add(st->output, slice);
  } else {
    gpr_slice_buffer_add(st->output, gpr_slice_split_head(&slice, remaining));
    finish_frame(st, 0, 0);
    begin_frame(st);
    add_header_data(st, slice);
  }
}

static gpr_uint8 *add_tiny_header_data(framer_state *st, size_t len) {
  ensure_space(st, len);
  return gpr_slice_buffer_tiny_add(st->output, len);
}

static void evict_entry(grpc_chttp2_hpack_compressor *c) {
  c->tail_remote_index++;
  GPR_ASSERT(c->tail_remote_index > 0);
  GPR_ASSERT(c->table_size >=
             c->table_elem_size[c->tail_remote_index %
                                c->cap_table_elems]);
  GPR_ASSERT(c->table_elems > 0);
  c->table_size =
      (gpr_uint16)(c->table_size -
                   c->table_elem_size[c->tail_remote_index %
                                      c->cap_table_elems]);
  c->table_elems--;
}

/* add an element to the decoder table */
static void add_elem(grpc_chttp2_hpack_compressor *c, grpc_mdelem *elem) {
  gpr_uint32 key_hash = elem->key->hash;
  gpr_uint32 elem_hash = GRPC_MDSTR_KV_HASH(key_hash, elem->value->hash);
  gpr_uint32 new_index = c->tail_remote_index + c->table_elems + 1;
  size_t elem_size = 32 + GPR_SLICE_LENGTH(elem->key->slice) +
                     GPR_SLICE_LENGTH(elem->value->slice);

  GPR_ASSERT(elem_size < 65536);

  if (elem_size > c->max_table_size) {
    while (c->table_size > 0) {
      evict_entry(c);
    }
    return;
  }

  /* Reserve space for this element in the remote table: if this overflows
     the current table, drop elements until it fits, matching the decompressor
     algorithm */
  while (c->table_size + elem_size > c->max_table_size) {
    evict_entry(c);
  }
  GPR_ASSERT(c->table_elems < c->max_table_size);
  c->table_elem_size[new_index % c->cap_table_elems] =
      (gpr_uint16)elem_size;
  c->table_size = (gpr_uint16)(c->table_size + elem_size);
  c->table_elems++;

  /* Store this element into {entries,indices}_elem */
  if (c->entries_elems[HASH_FRAGMENT_2(elem_hash)] == elem) {
    /* already there: update with new index */
    c->indices_elems[HASH_FRAGMENT_2(elem_hash)] = new_index;
  } else if (c->entries_elems[HASH_FRAGMENT_3(elem_hash)] == elem) {
    /* already there (cuckoo): update with new index */
    c->indices_elems[HASH_FRAGMENT_3(elem_hash)] = new_index;
  } else if (c->entries_elems[HASH_FRAGMENT_2(elem_hash)] == NULL) {
    /* not there, but a free element: add */
    c->entries_elems[HASH_FRAGMENT_2(elem_hash)] = GRPC_MDELEM_REF(elem);
    c->indices_elems[HASH_FRAGMENT_2(elem_hash)] = new_index;
  } else if (c->entries_elems[HASH_FRAGMENT_3(elem_hash)] == NULL) {
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

  /* do exactly the same for the key (so we can find by that again too) */

  if (c->entries_keys[HASH_FRAGMENT_2(key_hash)] == elem->key) {
    c->indices_keys[HASH_FRAGMENT_2(key_hash)] = new_index;
  } else if (c->entries_keys[HASH_FRAGMENT_3(key_hash)] == elem->key) {
    c->indices_keys[HASH_FRAGMENT_3(key_hash)] = new_index;
  } else if (c->entries_keys[HASH_FRAGMENT_2(key_hash)] == NULL) {
    c->entries_keys[HASH_FRAGMENT_2(key_hash)] = GRPC_MDSTR_REF(elem->key);
    c->indices_keys[HASH_FRAGMENT_2(key_hash)] = new_index;
  } else if (c->entries_keys[HASH_FRAGMENT_3(key_hash)] == NULL) {
    c->entries_keys[HASH_FRAGMENT_3(key_hash)] = GRPC_MDSTR_REF(elem->key);
    c->indices_keys[HASH_FRAGMENT_3(key_hash)] = new_index;
  } else if (c->indices_keys[HASH_FRAGMENT_2(key_hash)] <
             c->indices_keys[HASH_FRAGMENT_3(key_hash)]) {
    GRPC_MDSTR_UNREF(c->entries_keys[HASH_FRAGMENT_2(key_hash)]);
    c->entries_keys[HASH_FRAGMENT_2(key_hash)] = GRPC_MDSTR_REF(elem->key);
    c->indices_keys[HASH_FRAGMENT_2(key_hash)] = new_index;
  } else {
    GRPC_MDSTR_UNREF(c->entries_keys[HASH_FRAGMENT_3(key_hash)]);
    c->entries_keys[HASH_FRAGMENT_3(key_hash)] = GRPC_MDSTR_REF(elem->key);
    c->indices_keys[HASH_FRAGMENT_3(key_hash)] = new_index;
  }
}

static void emit_indexed(grpc_chttp2_hpack_compressor *c, gpr_uint32 elem_index,
                         framer_state *st) {
  gpr_uint32 len = GRPC_CHTTP2_VARINT_LENGTH(elem_index, 1);
  GRPC_CHTTP2_WRITE_VARINT(elem_index, 1, 0x80, add_tiny_header_data(st, len),
                           len);
}

static gpr_slice get_wire_value(grpc_mdelem *elem, gpr_uint8 *huffman_prefix) {
  if (grpc_is_binary_header((const char *)GPR_SLICE_START_PTR(elem->key->slice),
                            GPR_SLICE_LENGTH(elem->key->slice))) {
    *huffman_prefix = 0x80;
    return grpc_mdstr_as_base64_encoded_and_huffman_compressed(elem->value);
  }
  /* TODO(ctiller): opportunistically compress non-binary headers */
  *huffman_prefix = 0x00;
  return elem->value->slice;
}

static void emit_lithdr_incidx(grpc_chttp2_hpack_compressor *c,
                               gpr_uint32 key_index, grpc_mdelem *elem,
                               framer_state *st) {
  gpr_uint32 len_pfx = GRPC_CHTTP2_VARINT_LENGTH(key_index, 2);
  gpr_uint8 huffman_prefix;
  gpr_slice value_slice = get_wire_value(elem, &huffman_prefix);
  size_t len_val = GPR_SLICE_LENGTH(value_slice);
  gpr_uint32 len_val_len;
  GPR_ASSERT(len_val <= GPR_UINT32_MAX);
  len_val_len = GRPC_CHTTP2_VARINT_LENGTH((gpr_uint32)len_val, 1);
  GRPC_CHTTP2_WRITE_VARINT(key_index, 2, 0x40,
                           add_tiny_header_data(st, len_pfx), len_pfx);
  GRPC_CHTTP2_WRITE_VARINT((gpr_uint32)len_val, 1, 0x00,
                           add_tiny_header_data(st, len_val_len), len_val_len);
  add_header_data(st, gpr_slice_ref(value_slice));
}

static void emit_lithdr_noidx(grpc_chttp2_hpack_compressor *c,
                              gpr_uint32 key_index, grpc_mdelem *elem,
                              framer_state *st) {
  gpr_uint32 len_pfx = GRPC_CHTTP2_VARINT_LENGTH(key_index, 4);
  gpr_uint8 huffman_prefix;
  gpr_slice value_slice = get_wire_value(elem, &huffman_prefix);
  size_t len_val = GPR_SLICE_LENGTH(value_slice);
  gpr_uint32 len_val_len;
  GPR_ASSERT(len_val <= GPR_UINT32_MAX);
  len_val_len = GRPC_CHTTP2_VARINT_LENGTH((gpr_uint32)len_val, 1);
  GRPC_CHTTP2_WRITE_VARINT(key_index, 4, 0x00,
                           add_tiny_header_data(st, len_pfx), len_pfx);
  GRPC_CHTTP2_WRITE_VARINT((gpr_uint32)len_val, 1, 0x00,
                           add_tiny_header_data(st, len_val_len), len_val_len);
  add_header_data(st, gpr_slice_ref(value_slice));
}

static void emit_lithdr_incidx_v(grpc_chttp2_hpack_compressor *c,
                                 grpc_mdelem *elem, framer_state *st) {
  gpr_uint32 len_key = (gpr_uint32)GPR_SLICE_LENGTH(elem->key->slice);
  gpr_uint8 huffman_prefix;
  gpr_slice value_slice = get_wire_value(elem, &huffman_prefix);
  gpr_uint32 len_val = (gpr_uint32)GPR_SLICE_LENGTH(value_slice);
  gpr_uint32 len_key_len = GRPC_CHTTP2_VARINT_LENGTH(len_key, 1);
  gpr_uint32 len_val_len = GRPC_CHTTP2_VARINT_LENGTH(len_val, 1);
  GPR_ASSERT(len_key <= GPR_UINT32_MAX);
  GPR_ASSERT(GPR_SLICE_LENGTH(value_slice) <= GPR_UINT32_MAX);
  *add_tiny_header_data(st, 1) = 0x40;
  GRPC_CHTTP2_WRITE_VARINT(len_key, 1, 0x00,
                           add_tiny_header_data(st, len_key_len), len_key_len);
  add_header_data(st, gpr_slice_ref(elem->key->slice));
  GRPC_CHTTP2_WRITE_VARINT(len_val, 1, huffman_prefix,
                           add_tiny_header_data(st, len_val_len), len_val_len);
  add_header_data(st, gpr_slice_ref(value_slice));
}

static void emit_lithdr_noidx_v(grpc_chttp2_hpack_compressor *c,
                                grpc_mdelem *elem, framer_state *st) {
  gpr_uint32 len_key = (gpr_uint32)GPR_SLICE_LENGTH(elem->key->slice);
  gpr_uint8 huffman_prefix;
  gpr_slice value_slice = get_wire_value(elem, &huffman_prefix);
  gpr_uint32 len_val = (gpr_uint32)GPR_SLICE_LENGTH(value_slice);
  gpr_uint32 len_key_len = GRPC_CHTTP2_VARINT_LENGTH(len_key, 1);
  gpr_uint32 len_val_len = GRPC_CHTTP2_VARINT_LENGTH(len_val, 1);
  GPR_ASSERT(len_key <= GPR_UINT32_MAX);
  GPR_ASSERT(GPR_SLICE_LENGTH(value_slice) <= GPR_UINT32_MAX);
  *add_tiny_header_data(st, 1) = 0x00;
  GRPC_CHTTP2_WRITE_VARINT(len_key, 1, 0x00,
                           add_tiny_header_data(st, len_key_len), len_key_len);
  add_header_data(st, gpr_slice_ref(elem->key->slice));
  GRPC_CHTTP2_WRITE_VARINT(len_val, 1, huffman_prefix,
                           add_tiny_header_data(st, len_val_len), len_val_len);
  add_header_data(st, gpr_slice_ref(value_slice));
}

static void emit_advertise_table_size_change(grpc_chttp2_hpack_compressor *c, framer_state *st) {
  gpr_uint32 len = GRPC_CHTTP2_VARINT_LENGTH(c->max_table_size, 3);
  GRPC_CHTTP2_WRITE_VARINT(c->max_table_size, 3, 0x20, add_tiny_header_data(st, len), len);
  c->advertise_table_size_change = 0;
}

static gpr_uint32 dynidx(grpc_chttp2_hpack_compressor *c,
                         gpr_uint32 elem_index) {
  return 1 + GRPC_CHTTP2_LAST_STATIC_ENTRY + c->tail_remote_index +
         c->table_elems - elem_index;
}

/* encode an mdelem */
static void hpack_enc(grpc_chttp2_hpack_compressor *c, grpc_mdelem *elem,
                      framer_state *st) {
  gpr_uint32 key_hash = elem->key->hash;
  gpr_uint32 elem_hash = GRPC_MDSTR_KV_HASH(key_hash, elem->value->hash);
  size_t decoder_space_usage;
  gpr_uint32 indices_key;
  int should_add_elem;

  GPR_ASSERT(GPR_SLICE_LENGTH(elem->key->slice) > 0);
  if (GPR_SLICE_START_PTR(elem->key->slice)[0] != ':') { /* regular header */
    st->seen_regular_header = 1;
  } else if (st->seen_regular_header != 0) { /* reserved header */
    gpr_log(GPR_ERROR,
            "Reserved header (colon-prefixed) happening after regular ones.");
    abort();
  }

  inc_filter(HASH_FRAGMENT_1(elem_hash), &c->filter_elems_sum, c->filter_elems);

  /* is this elem currently in the decoders table? */

  if (c->entries_elems[HASH_FRAGMENT_2(elem_hash)] == elem &&
      c->indices_elems[HASH_FRAGMENT_2(elem_hash)] > c->tail_remote_index) {
    /* HIT: complete element (first cuckoo hash) */
    emit_indexed(c, dynidx(c, c->indices_elems[HASH_FRAGMENT_2(elem_hash)]),
                 st);
    return;
  }

  if (c->entries_elems[HASH_FRAGMENT_3(elem_hash)] == elem &&
      c->indices_elems[HASH_FRAGMENT_3(elem_hash)] > c->tail_remote_index) {
    /* HIT: complete element (second cuckoo hash) */
    emit_indexed(c, dynidx(c, c->indices_elems[HASH_FRAGMENT_3(elem_hash)]),
                 st);
    return;
  }

  /* should this elem be in the table? */
  decoder_space_usage = 32 + GPR_SLICE_LENGTH(elem->key->slice) +
                        GPR_SLICE_LENGTH(elem->value->slice);
  should_add_elem = decoder_space_usage < MAX_DECODER_SPACE_USAGE &&
                    c->filter_elems[HASH_FRAGMENT_1(elem_hash)] >=
                        c->filter_elems_sum / ONE_ON_ADD_PROBABILITY;

  /* no hits for the elem... maybe there's a key? */

  indices_key = c->indices_keys[HASH_FRAGMENT_2(key_hash)];
  if (c->entries_keys[HASH_FRAGMENT_2(key_hash)] == elem->key &&
      indices_key > c->tail_remote_index) {
    /* HIT: key (first cuckoo hash) */
    if (should_add_elem) {
      emit_lithdr_incidx(c, dynidx(c, indices_key), elem, st);
      add_elem(c, elem);
      return;
    } else {
      emit_lithdr_noidx(c, dynidx(c, indices_key), elem, st);
      return;
    }
    GPR_UNREACHABLE_CODE(return );
  }

  indices_key = c->indices_keys[HASH_FRAGMENT_3(key_hash)];
  if (c->entries_keys[HASH_FRAGMENT_3(key_hash)] == elem->key &&
      indices_key > c->tail_remote_index) {
    /* HIT: key (first cuckoo hash) */
    if (should_add_elem) {
      emit_lithdr_incidx(c, dynidx(c, indices_key), elem, st);
      add_elem(c, elem);
      return;
    } else {
      emit_lithdr_noidx(c, dynidx(c, indices_key), elem, st);
      return;
    }
    GPR_UNREACHABLE_CODE(return );
  }

  /* no elem, key in the table... fall back to literal emission */

  if (should_add_elem) {
    emit_lithdr_incidx_v(c, elem, st);
    add_elem(c, elem);
    return;
  } else {
    emit_lithdr_noidx_v(c, elem, st);
    return;
  }
  GPR_UNREACHABLE_CODE(return );
}

#define STRLEN_LIT(x) (sizeof(x) - 1)
#define TIMEOUT_KEY "grpc-timeout"

static void deadline_enc(grpc_chttp2_hpack_compressor *c, gpr_timespec deadline,
                         framer_state *st) {
  char timeout_str[GRPC_CHTTP2_TIMEOUT_ENCODE_MIN_BUFSIZE];
  grpc_mdelem *mdelem;
  grpc_chttp2_encode_timeout(
      gpr_time_sub(deadline, gpr_now(deadline.clock_type)), timeout_str);
  mdelem = grpc_mdelem_from_metadata_strings(
      c->mdctx, GRPC_MDSTR_REF(c->timeout_key_str),
      grpc_mdstr_from_string(c->mdctx, timeout_str));
  hpack_enc(c, mdelem, st);
  GRPC_MDELEM_UNREF(mdelem);
}

gpr_slice grpc_chttp2_data_frame_create_empty_close(gpr_uint32 id) {
  gpr_slice slice = gpr_slice_malloc(9);
  fill_header(GPR_SLICE_START_PTR(slice), GRPC_CHTTP2_FRAME_DATA, id, 0, 1);
  return slice;
}

static gpr_uint32 elems_for_bytes(gpr_uint32 bytes) {
  return (bytes + 31) / 32;
}

void grpc_chttp2_hpack_compressor_init(grpc_chttp2_hpack_compressor *c,
                                       grpc_mdctx *ctx) {
  memset(c, 0, sizeof(*c));
  c->mdctx = ctx;
  c->timeout_key_str = grpc_mdstr_from_string(ctx, "grpc-timeout");
  c->max_table_size = GRPC_CHTTP2_HPACKC_INITIAL_TABLE_SIZE;
  c->cap_table_elems = c->max_table_elems = elems_for_bytes(c->max_table_size);
  c->max_usable_size = GRPC_CHTTP2_HPACKC_INITIAL_TABLE_SIZE;
  c->table_elem_size = gpr_malloc(sizeof(*c->table_elem_size) * c->cap_table_elems);
  memset(c->table_elem_size, 0, sizeof(*c->table_elem_size) * c->cap_table_elems);
}

void grpc_chttp2_hpack_compressor_destroy(grpc_chttp2_hpack_compressor *c) {
  int i;
  for (i = 0; i < GRPC_CHTTP2_HPACKC_NUM_VALUES; i++) {
    if (c->entries_keys[i]) GRPC_MDSTR_UNREF(c->entries_keys[i]);
    if (c->entries_elems[i]) GRPC_MDELEM_UNREF(c->entries_elems[i]);
  }
  GRPC_MDSTR_UNREF(c->timeout_key_str);
  gpr_free(c->table_elem_size);
}

void grpc_chttp2_hpack_compressor_set_max_usable_size(grpc_chttp2_hpack_compressor *c, gpr_uint32 max_table_size) {
  c->max_usable_size = max_table_size;
  grpc_chttp2_hpack_compressor_set_max_table_size(c, GPR_MIN(c->max_table_size, max_table_size));
}

static void rebuild_elems(grpc_chttp2_hpack_compressor *c, gpr_uint32 new_cap) {

}

void grpc_chttp2_hpack_compressor_set_max_table_size(grpc_chttp2_hpack_compressor *c, gpr_uint32 max_table_size) {
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
    gpr_uint32 new_cap = GPR_MIN(c->max_table_elems, 16);
    if (new_cap != c->cap_table_elems) {
      rebuild_elems(c, new_cap);
    }
  }
  c->advertise_table_size_change = 1;
  gpr_log(GPR_DEBUG, "set max table size from encoder to %d", max_table_size);
}

void grpc_chttp2_encode_header(grpc_chttp2_hpack_compressor *c,
                               gpr_uint32 stream_id,
                               grpc_metadata_batch *metadata, int is_eof,
                               gpr_slice_buffer *outbuf) {
  framer_state st;
  grpc_linked_mdelem *l;
  gpr_timespec deadline;

  GPR_ASSERT(stream_id != 0);

  st.seen_regular_header = 0;
  st.stream_id = stream_id;
  st.output = outbuf;
  st.is_first_frame = 1;

  /* Encode a metadata batch; store the returned values, representing
     a metadata element that needs to be unreffed back into the metadata
     slot. THIS MAY NOT BE THE SAME ELEMENT (if a decoder table slot got
     updated). After this loop, we'll do a batch unref of elements. */
  begin_frame(&st);
  if (c->advertise_table_size_change != 0) {
    emit_advertise_table_size_change(c, &st);
  }
  grpc_metadata_batch_assert_ok(metadata);
  for (l = metadata->list.head; l; l = l->next) {
    hpack_enc(c, l->md, &st);
  }
  deadline = metadata->deadline;
  if (gpr_time_cmp(deadline, gpr_inf_future(deadline.clock_type)) != 0) {
    deadline_enc(c, deadline, &st);
  }

  finish_frame(&st, 1, is_eof);
}

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

#include "src/core/transport/chttp2/stream_encoder.h"

#include <assert.h>
#include <string.h>

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

/* what kind of frame our we encoding? */
typedef enum { HEADER, DATA, NONE } frame_type;

typedef struct {
  frame_type cur_frame_type;
  /* number of bytes in 'output' when we started the frame - used to calculate
     frame length */
  size_t output_length_at_start_of_frame;
  /* index (in output) of the header for the current frame */
  size_t header_idx;
  /* was the last frame emitted a header? (if yes, we'll need a CONTINUATION */
  gpr_uint8 last_was_header;
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
  switch (st->cur_frame_type) {
    case HEADER:
      type = st->last_was_header ? GRPC_CHTTP2_FRAME_CONTINUATION
                                 : GRPC_CHTTP2_FRAME_HEADER;
      st->last_was_header = 1;
      break;
    case DATA:
      type = GRPC_CHTTP2_FRAME_DATA;
      st->last_was_header = 0;
      is_header_boundary = 0;
      break;
    case NONE:
      return;
  }
  fill_header(
      GPR_SLICE_START_PTR(st->output->slices[st->header_idx]), type,
      st->stream_id, st->output->length - st->output_length_at_start_of_frame,
      (gpr_uint8)(
          (is_last_in_stream ? GRPC_CHTTP2_DATA_FLAG_END_STREAM : 0) |
          (is_header_boundary ? GRPC_CHTTP2_DATA_FLAG_END_HEADERS : 0)));
  st->cur_frame_type = NONE;
}

/* begin a new frame: reserve off header space, remember how many bytes we'd
   output before beginning */
static void begin_frame(framer_state *st, frame_type type) {
  GPR_ASSERT(type != NONE);
  GPR_ASSERT(st->cur_frame_type == NONE);
  st->cur_frame_type = type;
  st->header_idx =
      gpr_slice_buffer_add_indexed(st->output, gpr_slice_malloc(9));
  st->output_length_at_start_of_frame = st->output->length;
}

static void begin_new_frame(framer_state *st, frame_type type) {
  finish_frame(st, 1, 0);
  st->last_was_header = 0;
  begin_frame(st, type);
}

/* make sure that the current frame is of the type desired, and has sufficient
   space to add at least about_to_add bytes -- finishes the current frame if
   needed */
static void ensure_frame_type(framer_state *st, frame_type type,
                              size_t need_bytes) {
  if (st->cur_frame_type == type &&
      st->output->length - st->output_length_at_start_of_frame + need_bytes <=
          GRPC_CHTTP2_MAX_PAYLOAD_LENGTH) {
    return;
  }
  finish_frame(st, type != HEADER, 0);
  begin_frame(st, type);
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
  ensure_frame_type(st, HEADER, 1);
  remaining = GRPC_CHTTP2_MAX_PAYLOAD_LENGTH +
              st->output_length_at_start_of_frame - st->output->length;
  if (len <= remaining) {
    gpr_slice_buffer_add(st->output, slice);
  } else {
    gpr_slice_buffer_add(st->output, gpr_slice_split_head(&slice, remaining));
    add_header_data(st, slice);
  }
}

static gpr_uint8 *add_tiny_header_data(framer_state *st, size_t len) {
  ensure_frame_type(st, HEADER, len);
  return gpr_slice_buffer_tiny_add(st->output, len);
}

/* add an element to the decoder table: returns metadata element to unref */
static grpc_mdelem *add_elem(grpc_chttp2_hpack_compressor *c,
                             grpc_mdelem *elem) {
  gpr_uint32 key_hash = elem->key->hash;
  gpr_uint32 elem_hash = GRPC_MDSTR_KV_HASH(key_hash, elem->value->hash);
  gpr_uint32 new_index = c->tail_remote_index + c->table_elems + 1;
  size_t elem_size = 32 + GPR_SLICE_LENGTH(elem->key->slice) +
                     GPR_SLICE_LENGTH(elem->value->slice);
  grpc_mdelem *elem_to_unref;

  GPR_ASSERT(elem_size < 65536);

  /* Reserve space for this element in the remote table: if this overflows
     the current table, drop elements until it fits, matching the decompressor
     algorithm */
  /* TODO(ctiller): constant */
  while (c->table_size + elem_size > 4096) {
    c->tail_remote_index++;
    GPR_ASSERT(c->tail_remote_index > 0);
    GPR_ASSERT(c->table_size >=
               c->table_elem_size[c->tail_remote_index %
                                  GRPC_CHTTP2_HPACKC_MAX_TABLE_ELEMS]);
    GPR_ASSERT(c->table_elems > 0);
    c->table_size =
        (gpr_uint16)(c->table_size -
                     c->table_elem_size[c->tail_remote_index %
                                        GRPC_CHTTP2_HPACKC_MAX_TABLE_ELEMS]);
    c->table_elems--;
  }
  GPR_ASSERT(c->table_elems < GRPC_CHTTP2_HPACKC_MAX_TABLE_ELEMS);
  c->table_elem_size[new_index % GRPC_CHTTP2_HPACKC_MAX_TABLE_ELEMS] =
      (gpr_uint16)elem_size;
  c->table_size = (gpr_uint16)(c->table_size + elem_size);
  c->table_elems++;

  /* Store this element into {entries,indices}_elem */
  if (c->entries_elems[HASH_FRAGMENT_2(elem_hash)] == elem) {
    /* already there: update with new index */
    c->indices_elems[HASH_FRAGMENT_2(elem_hash)] = new_index;
    elem_to_unref = elem;
  } else if (c->entries_elems[HASH_FRAGMENT_3(elem_hash)] == elem) {
    /* already there (cuckoo): update with new index */
    c->indices_elems[HASH_FRAGMENT_3(elem_hash)] = new_index;
    elem_to_unref = elem;
  } else if (c->entries_elems[HASH_FRAGMENT_2(elem_hash)] == NULL) {
    /* not there, but a free element: add */
    c->entries_elems[HASH_FRAGMENT_2(elem_hash)] = elem;
    c->indices_elems[HASH_FRAGMENT_2(elem_hash)] = new_index;
    elem_to_unref = NULL;
  } else if (c->entries_elems[HASH_FRAGMENT_3(elem_hash)] == NULL) {
    /* not there (cuckoo), but a free element: add */
    c->entries_elems[HASH_FRAGMENT_3(elem_hash)] = elem;
    c->indices_elems[HASH_FRAGMENT_3(elem_hash)] = new_index;
    elem_to_unref = NULL;
  } else if (c->indices_elems[HASH_FRAGMENT_2(elem_hash)] <
             c->indices_elems[HASH_FRAGMENT_3(elem_hash)]) {
    /* not there: replace oldest */
    elem_to_unref = c->entries_elems[HASH_FRAGMENT_2(elem_hash)];
    c->entries_elems[HASH_FRAGMENT_2(elem_hash)] = elem;
    c->indices_elems[HASH_FRAGMENT_2(elem_hash)] = new_index;
  } else {
    /* not there: replace oldest */
    elem_to_unref = c->entries_elems[HASH_FRAGMENT_3(elem_hash)];
    c->entries_elems[HASH_FRAGMENT_3(elem_hash)] = elem;
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

  return elem_to_unref;
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

static gpr_uint32 dynidx(grpc_chttp2_hpack_compressor *c,
                         gpr_uint32 elem_index) {
  return 1 + GRPC_CHTTP2_LAST_STATIC_ENTRY + c->tail_remote_index +
         c->table_elems - elem_index;
}

/* encode an mdelem; returns metadata element to unref */
static grpc_mdelem *hpack_enc(grpc_chttp2_hpack_compressor *c,
                              grpc_mdelem *elem, framer_state *st) {
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
    return elem;
  }

  if (c->entries_elems[HASH_FRAGMENT_3(elem_hash)] == elem &&
      c->indices_elems[HASH_FRAGMENT_3(elem_hash)] > c->tail_remote_index) {
    /* HIT: complete element (second cuckoo hash) */
    emit_indexed(c, dynidx(c, c->indices_elems[HASH_FRAGMENT_3(elem_hash)]),
                 st);
    return elem;
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
      return add_elem(c, elem);
    } else {
      emit_lithdr_noidx(c, dynidx(c, indices_key), elem, st);
      return elem;
    }
    abort();
  }

  indices_key = c->indices_keys[HASH_FRAGMENT_3(key_hash)];
  if (c->entries_keys[HASH_FRAGMENT_3(key_hash)] == elem->key &&
      indices_key > c->tail_remote_index) {
    /* HIT: key (first cuckoo hash) */
    if (should_add_elem) {
      emit_lithdr_incidx(c, dynidx(c, indices_key), elem, st);
      return add_elem(c, elem);
    } else {
      emit_lithdr_noidx(c, dynidx(c, indices_key), elem, st);
      return elem;
    }
    abort();
  }

  /* no elem, key in the table... fall back to literal emission */

  if (should_add_elem) {
    emit_lithdr_incidx_v(c, elem, st);
    return add_elem(c, elem);
  } else {
    emit_lithdr_noidx_v(c, elem, st);
    return elem;
  }
  abort();
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
  mdelem = hpack_enc(c, mdelem, st);
  if (mdelem) GRPC_MDELEM_UNREF(mdelem);
}

gpr_slice grpc_chttp2_data_frame_create_empty_close(gpr_uint32 id) {
  gpr_slice slice = gpr_slice_malloc(9);
  fill_header(GPR_SLICE_START_PTR(slice), GRPC_CHTTP2_FRAME_DATA, id, 0, 1);
  return slice;
}

void grpc_chttp2_hpack_compressor_init(grpc_chttp2_hpack_compressor *c,
                                       grpc_mdctx *ctx) {
  memset(c, 0, sizeof(*c));
  c->mdctx = ctx;
  c->timeout_key_str = grpc_mdstr_from_string(ctx, "grpc-timeout");
}

void grpc_chttp2_hpack_compressor_destroy(grpc_chttp2_hpack_compressor *c) {
  int i;
  for (i = 0; i < GRPC_CHTTP2_HPACKC_NUM_VALUES; i++) {
    if (c->entries_keys[i]) GRPC_MDSTR_UNREF(c->entries_keys[i]);
    if (c->entries_elems[i]) GRPC_MDELEM_UNREF(c->entries_elems[i]);
  }
  GRPC_MDSTR_UNREF(c->timeout_key_str);
}

gpr_uint32 grpc_chttp2_preencode(grpc_stream_op *inops, size_t *inops_count,
                                 gpr_uint32 max_flow_controlled_bytes,
                                 grpc_stream_op_buffer *outops) {
  gpr_slice slice;
  grpc_stream_op *op;
  gpr_uint32 max_take_size;
  gpr_uint32 flow_controlled_bytes_taken = 0;
  gpr_uint32 curop = 0;
  gpr_uint8 *p;
  gpr_uint8 compressed_flag_set = 0;

  while (curop < *inops_count) {
    GPR_ASSERT(flow_controlled_bytes_taken <= max_flow_controlled_bytes);
    op = &inops[curop];
    switch (op->type) {
      case GRPC_NO_OP:
        /* skip */
        curop++;
        break;
      case GRPC_OP_METADATA:
        grpc_metadata_batch_assert_ok(&op->data.metadata);
        /* these just get copied as they don't impact the number of flow
           controlled bytes */
        grpc_sopb_append(outops, op, 1);
        curop++;
        break;
      case GRPC_OP_BEGIN_MESSAGE:
        /* begin op: for now we just convert the op to a slice and fall
           through - this lets us reuse the slice framing code below */
        compressed_flag_set =
            (op->data.begin_message.flags & GRPC_WRITE_INTERNAL_COMPRESS) != 0;
        slice = gpr_slice_malloc(5);

        p = GPR_SLICE_START_PTR(slice);
        p[0] = compressed_flag_set;
        p[1] = (gpr_uint8)(op->data.begin_message.length >> 24);
        p[2] = (gpr_uint8)(op->data.begin_message.length >> 16);
        p[3] = (gpr_uint8)(op->data.begin_message.length >> 8);
        p[4] = (gpr_uint8)(op->data.begin_message.length);
        op->type = GRPC_OP_SLICE;
        op->data.slice = slice;
      /* fallthrough */
      case GRPC_OP_SLICE:
        slice = op->data.slice;
        if (!GPR_SLICE_LENGTH(slice)) {
          /* skip zero length slices */
          gpr_slice_unref(slice);
          curop++;
          break;
        }
        max_take_size = max_flow_controlled_bytes - flow_controlled_bytes_taken;
        if (max_take_size == 0) {
          goto exit_loop;
        }
        if (GPR_SLICE_LENGTH(slice) > max_take_size) {
          slice = gpr_slice_split_head(&op->data.slice, max_take_size);
          grpc_sopb_add_slice(outops, slice);
        } else {
          /* consume this op immediately */
          grpc_sopb_append(outops, op, 1);
          curop++;
        }
        flow_controlled_bytes_taken += (gpr_uint32)GPR_SLICE_LENGTH(slice);
        break;
    }
  }
exit_loop:
  *inops_count -= curop;
  memmove(inops, inops + curop, *inops_count * sizeof(grpc_stream_op));

  for (curop = 0; curop < *inops_count; curop++) {
    if (inops[curop].type == GRPC_OP_METADATA) {
      grpc_metadata_batch_assert_ok(&inops[curop].data.metadata);
    }
  }

  return flow_controlled_bytes_taken;
}

void grpc_chttp2_encode(grpc_stream_op *ops, size_t ops_count, int eof,
                        gpr_uint32 stream_id,
                        grpc_chttp2_hpack_compressor *compressor,
                        gpr_slice_buffer *output) {
  framer_state st;
  gpr_slice slice;
  grpc_stream_op *op;
  size_t max_take_size;
  gpr_uint32 curop = 0;
  gpr_uint32 unref_op;
  grpc_linked_mdelem *l;
  int need_unref = 0;
  gpr_timespec deadline;

  GPR_ASSERT(stream_id != 0);

  st.cur_frame_type = NONE;
  st.last_was_header = 0;
  st.seen_regular_header = 0;
  st.stream_id = stream_id;
  st.output = output;

  while (curop < ops_count) {
    op = &ops[curop];
    switch (op->type) {
      case GRPC_NO_OP:
      case GRPC_OP_BEGIN_MESSAGE:
        gpr_log(
            GPR_ERROR,
            "These stream ops should be filtered out by grpc_chttp2_preencode");
        abort();
      case GRPC_OP_METADATA:
        /* Encode a metadata batch; store the returned values, representing
           a metadata element that needs to be unreffed back into the metadata
           slot. THIS MAY NOT BE THE SAME ELEMENT (if a decoder table slot got
           updated). After this loop, we'll do a batch unref of elements. */
        begin_new_frame(&st, HEADER);
        need_unref |= op->data.metadata.garbage.head != NULL;
        grpc_metadata_batch_assert_ok(&op->data.metadata);
        for (l = op->data.metadata.list.head; l; l = l->next) {
          l->md = hpack_enc(compressor, l->md, &st);
          need_unref |= l->md != NULL;
        }
        deadline = op->data.metadata.deadline;
        if (gpr_time_cmp(deadline, gpr_inf_future(deadline.clock_type)) != 0) {
          deadline_enc(compressor, deadline, &st);
        }
        curop++;
        break;
      case GRPC_OP_SLICE:
        slice = op->data.slice;
        if (st.cur_frame_type == DATA &&
            st.output->length - st.output_length_at_start_of_frame ==
                GRPC_CHTTP2_MAX_PAYLOAD_LENGTH) {
          finish_frame(&st, 0, 0);
        }
        ensure_frame_type(&st, DATA, 1);
        max_take_size = GRPC_CHTTP2_MAX_PAYLOAD_LENGTH +
                        st.output_length_at_start_of_frame - st.output->length;
        if (GPR_SLICE_LENGTH(slice) > max_take_size) {
          slice = gpr_slice_split_head(&op->data.slice, max_take_size);
        } else {
          /* consume this op immediately */
          curop++;
        }
        gpr_slice_buffer_add(output, slice);
        break;
    }
  }
  if (eof && st.cur_frame_type == NONE) {
    begin_frame(&st, DATA);
  }
  finish_frame(&st, 1, eof);

  if (need_unref) {
    for (unref_op = 0; unref_op < curop; unref_op++) {
      op = &ops[unref_op];
      if (op->type != GRPC_OP_METADATA) continue;
      for (l = op->data.metadata.list.head; l; l = l->next) {
        if (l->md) GRPC_MDELEM_UNREF(l->md);
      }
      for (l = op->data.metadata.garbage.head; l; l = l->next) {
        GRPC_MDELEM_UNREF(l->md);
      }
    }
  }
}

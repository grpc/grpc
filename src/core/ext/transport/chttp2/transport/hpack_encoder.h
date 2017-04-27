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

#ifndef GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HPACK_ENCODER_H
#define GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HPACK_ENCODER_H

#include <grpc/slice.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/port_platform.h>
#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/lib/transport/metadata.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/transport.h"

#define GRPC_CHTTP2_HPACKC_NUM_FILTERS 256
#define GRPC_CHTTP2_HPACKC_NUM_VALUES 256
/* initial table size, per spec */
#define GRPC_CHTTP2_HPACKC_INITIAL_TABLE_SIZE 4096
/* maximum table size we'll actually use */
#define GRPC_CHTTP2_HPACKC_MAX_TABLE_SIZE (1024 * 1024)

typedef struct {
  uint32_t filter_elems_sum;
  uint32_t max_table_size;
  uint32_t max_table_elems;
  uint32_t cap_table_elems;
  /** if non-zero, advertise to the decoder that we'll start using a table
      of this size */
  uint8_t advertise_table_size_change;
  /** maximum number of bytes we'll use for the decode table (to guard against
      peers ooming us by setting decode table size high) */
  uint32_t max_usable_size;
  /* one before the lowest usable table index */
  uint32_t tail_remote_index;
  uint32_t table_size;
  uint32_t table_elems;

  /* filter tables for elems: this tables provides an approximate
     popularity count for particular hashes, and are used to determine whether
     a new literal should be added to the compression table or not.
     They track a single integer that counts how often a particular value has
     been seen. When that count reaches max (255), all values are halved. */
  uint8_t filter_elems[GRPC_CHTTP2_HPACKC_NUM_FILTERS];

  /* entry tables for keys & elems: these tables track values that have been
     seen and *may* be in the decompressor table */
  grpc_slice entries_keys[GRPC_CHTTP2_HPACKC_NUM_VALUES];
  grpc_mdelem entries_elems[GRPC_CHTTP2_HPACKC_NUM_VALUES];
  uint32_t indices_keys[GRPC_CHTTP2_HPACKC_NUM_VALUES];
  uint32_t indices_elems[GRPC_CHTTP2_HPACKC_NUM_VALUES];

  uint16_t *table_elem_size;
} grpc_chttp2_hpack_compressor;

void grpc_chttp2_hpack_compressor_init(grpc_chttp2_hpack_compressor *c);
void grpc_chttp2_hpack_compressor_destroy(grpc_exec_ctx *exec_ctx,
                                          grpc_chttp2_hpack_compressor *c);
void grpc_chttp2_hpack_compressor_set_max_table_size(
    grpc_chttp2_hpack_compressor *c, uint32_t max_table_size);
void grpc_chttp2_hpack_compressor_set_max_usable_size(
    grpc_chttp2_hpack_compressor *c, uint32_t max_table_size);

typedef struct {
  uint32_t stream_id;
  bool is_eof;
  bool use_true_binary_metadata;
  size_t max_frame_size;
  grpc_transport_one_way_stats *stats;
} grpc_encode_header_options;

void grpc_chttp2_encode_header(grpc_exec_ctx *exec_ctx,
                               grpc_chttp2_hpack_compressor *c,
                               grpc_metadata_batch *metadata,
                               const grpc_encode_header_options *options,
                               grpc_slice_buffer *outbuf);

#endif /* GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HPACK_ENCODER_H */

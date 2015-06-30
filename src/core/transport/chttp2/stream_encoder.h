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

#ifndef GRPC_INTERNAL_CORE_TRANSPORT_CHTTP2_STREAM_ENCODER_H
#define GRPC_INTERNAL_CORE_TRANSPORT_CHTTP2_STREAM_ENCODER_H

#include "src/core/transport/chttp2/frame.h"
#include "src/core/transport/metadata.h"
#include "src/core/transport/stream_op.h"
#include <grpc/support/port_platform.h>
#include <grpc/support/slice.h>
#include <grpc/support/slice_buffer.h>

#define GRPC_CHTTP2_HPACKC_NUM_FILTERS 256
#define GRPC_CHTTP2_HPACKC_NUM_VALUES 256
#define GRPC_CHTTP2_HPACKC_MAX_TABLE_ELEMS (4096 / 32)

typedef struct {
  gpr_uint32 filter_elems_sum;
  /* one before the lowest usable table index */
  gpr_uint32 tail_remote_index;
  gpr_uint16 table_size;
  gpr_uint16 table_elems;

  /* filter tables for elems: this tables provides an approximate
     popularity count for particular hashes, and are used to determine whether
     a new literal should be added to the compression table or not.
     They track a single integer that counts how often a particular value has
     been seen. When that count reaches max (255), all values are halved. */
  gpr_uint8 filter_elems[GRPC_CHTTP2_HPACKC_NUM_FILTERS];

  /* metadata context */
  grpc_mdctx *mdctx;
  /* the string 'grpc-timeout' */
  grpc_mdstr *timeout_key_str;

  /* entry tables for keys & elems: these tables track values that have been
     seen and *may* be in the decompressor table */
  grpc_mdstr *entries_keys[GRPC_CHTTP2_HPACKC_NUM_VALUES];
  grpc_mdelem *entries_elems[GRPC_CHTTP2_HPACKC_NUM_VALUES];
  gpr_uint32 indices_keys[GRPC_CHTTP2_HPACKC_NUM_VALUES];
  gpr_uint32 indices_elems[GRPC_CHTTP2_HPACKC_NUM_VALUES];

  gpr_uint16 table_elem_size[GRPC_CHTTP2_HPACKC_MAX_TABLE_ELEMS];
} grpc_chttp2_hpack_compressor;

void grpc_chttp2_hpack_compressor_init(grpc_chttp2_hpack_compressor *c,
                                       grpc_mdctx *mdctx);
void grpc_chttp2_hpack_compressor_destroy(grpc_chttp2_hpack_compressor *c);

/* select stream ops to be encoded, moving them from inops to outops, and
   moving subsequent ops in inops forward in the queue */
gpr_uint32 grpc_chttp2_preencode(grpc_stream_op *inops, size_t *inops_count,
                                 gpr_uint32 max_flow_controlled_bytes,
                                 grpc_stream_op_buffer *outops);

/* encode stream ops to output */
void grpc_chttp2_encode(grpc_stream_op *ops, size_t ops_count, int eof,
                        gpr_uint32 stream_id,
                        grpc_chttp2_hpack_compressor *compressor,
                        gpr_slice_buffer *output);

#endif /* GRPC_INTERNAL_CORE_TRANSPORT_CHTTP2_STREAM_ENCODER_H */

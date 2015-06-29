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

#ifndef GRPC_INTERNAL_CORE_CHTTP2_INCOMING_METADATA_H
#define GRPC_INTERNAL_CORE_CHTTP2_INCOMING_METADATA_H

#include "src/core/transport/transport.h"

typedef struct {
  grpc_linked_mdelem *elems;
  size_t count;
  size_t capacity;
  gpr_timespec deadline;
} grpc_chttp2_incoming_metadata_buffer;

typedef struct {
  grpc_linked_mdelem *elems;
} grpc_chttp2_incoming_metadata_live_op_buffer;

/** assumes everything initially zeroed */
void grpc_chttp2_incoming_metadata_buffer_init(
    grpc_chttp2_incoming_metadata_buffer *buffer);
void grpc_chttp2_incoming_metadata_buffer_destroy(
    grpc_chttp2_incoming_metadata_buffer *buffer);
void grpc_chttp2_incoming_metadata_buffer_reset(
    grpc_chttp2_incoming_metadata_buffer *buffer);

void grpc_chttp2_incoming_metadata_buffer_add(
    grpc_chttp2_incoming_metadata_buffer *buffer, grpc_mdelem *elem);
void grpc_chttp2_incoming_metadata_buffer_set_deadline(
    grpc_chttp2_incoming_metadata_buffer *buffer, gpr_timespec deadline);

/** extend sopb with a metadata batch; this must be post-processed by
    grpc_chttp2_incoming_metadata_buffer_postprocess_sopb before being handed
    out of the transport */
void grpc_chttp2_incoming_metadata_buffer_place_metadata_batch_into(
    grpc_chttp2_incoming_metadata_buffer *buffer, grpc_stream_op_buffer *sopb);

void grpc_incoming_metadata_buffer_move_to_referencing_sopb(
    grpc_chttp2_incoming_metadata_buffer *src,
    grpc_chttp2_incoming_metadata_buffer *dst, grpc_stream_op_buffer *sopb);

void grpc_chttp2_incoming_metadata_buffer_postprocess_sopb_and_begin_live_op(
    grpc_chttp2_incoming_metadata_buffer *buffer, grpc_stream_op_buffer *sopb,
    grpc_chttp2_incoming_metadata_live_op_buffer *live_op_buffer);

void grpc_chttp2_incoming_metadata_live_op_buffer_end(
    grpc_chttp2_incoming_metadata_live_op_buffer *live_op_buffer);

#endif /* GRPC_INTERNAL_CORE_CHTTP2_INCOMING_METADATA_H */

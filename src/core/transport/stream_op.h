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

#ifndef GRPC_INTERNAL_CORE_TRANSPORT_STREAM_OP_H
#define GRPC_INTERNAL_CORE_TRANSPORT_STREAM_OP_H

#include <grpc/grpc.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/slice.h>
#include <grpc/support/time.h>
#include "src/core/transport/metadata.h"

/* this many stream ops are inlined into a sopb before allocating */
#define GRPC_SOPB_INLINE_ELEMENTS 16

/* Operations that can be performed on a stream.
   Used by grpc_stream_op. */
typedef enum grpc_stream_op_code {
  /* Do nothing code. Useful if rewriting a batch to exclude some operations.
     Must be ignored by receivers */
  GRPC_NO_OP,
  GRPC_OP_METADATA,
  GRPC_OP_DEADLINE,
  GRPC_OP_METADATA_BOUNDARY,
  /* Begin a message/metadata element/status - as defined by
     grpc_message_type. */
  GRPC_OP_BEGIN_MESSAGE,
  /* Add a slice of data to the current message/metadata element/status.
     Must not overflow the forward declared length. */
  GRPC_OP_SLICE,
  /* Call some function once this operation has passed flow control. */
  GRPC_OP_FLOW_CTL_CB
} grpc_stream_op_code;

/* Arguments for GRPC_OP_BEGIN */
typedef struct grpc_begin_message {
  /* How many bytes of data will this message contain */
  gpr_uint32 length;
  /* Write flags for the message: see grpc.h GRPC_WRITE_xxx */
  gpr_uint32 flags;
} grpc_begin_message;

/* Arguments for GRPC_OP_FLOW_CTL_CB */
typedef struct grpc_flow_ctl_cb {
  void (*cb)(void *arg, grpc_op_error error);
  void *arg;
} grpc_flow_ctl_cb;

/* Represents a single operation performed on a stream/transport */
typedef struct grpc_stream_op {
  /* the operation to be applied */
  enum grpc_stream_op_code type;
  /* the arguments to this operation. union fields are named according to the
     associated op-code */
  union {
    grpc_begin_message begin_message;
    grpc_mdelem *metadata;
    gpr_timespec deadline;
    gpr_slice slice;
    grpc_flow_ctl_cb flow_ctl_cb;
  } data;
} grpc_stream_op;

/* A stream op buffer is a wrapper around stream operations that is dynamically
   extendable.
   TODO(ctiller): inline a few elements into the struct, to avoid common case
                  per-call allocations. */
typedef struct grpc_stream_op_buffer {
  grpc_stream_op *ops;
  size_t nops;
  size_t capacity;
  grpc_stream_op inlined_ops[GRPC_SOPB_INLINE_ELEMENTS];
} grpc_stream_op_buffer;

/* Initialize a stream op buffer */
void grpc_sopb_init(grpc_stream_op_buffer *sopb);
/* Destroy a stream op buffer */
void grpc_sopb_destroy(grpc_stream_op_buffer *sopb);
/* Reset a sopb to no elements */
void grpc_sopb_reset(grpc_stream_op_buffer *sopb);
/* Swap two sopbs */
void grpc_sopb_swap(grpc_stream_op_buffer *a, grpc_stream_op_buffer *b);

void grpc_stream_ops_unref_owned_objects(grpc_stream_op *ops, size_t nops);

/* Append a GRPC_NO_OP to a buffer */
void grpc_sopb_add_no_op(grpc_stream_op_buffer *sopb);
/* Append a GRPC_OP_BEGIN to a buffer */
void grpc_sopb_add_begin_message(grpc_stream_op_buffer *sopb, gpr_uint32 length,
                                 gpr_uint32 flags);
void grpc_sopb_add_metadata(grpc_stream_op_buffer *sopb, grpc_mdelem *metadata);
void grpc_sopb_add_deadline(grpc_stream_op_buffer *sopb, gpr_timespec deadline);
void grpc_sopb_add_metadata_boundary(grpc_stream_op_buffer *sopb);
/* Append a GRPC_SLICE to a buffer - does not ref/unref the slice */
void grpc_sopb_add_slice(grpc_stream_op_buffer *sopb, gpr_slice slice);
/* Append a GRPC_OP_FLOW_CTL_CB to a buffer */
void grpc_sopb_add_flow_ctl_cb(grpc_stream_op_buffer *sopb,
                               void (*cb)(void *arg, grpc_op_error error),
                               void *arg);
/* Append a buffer to a buffer - does not ref/unref any internal objects */
void grpc_sopb_append(grpc_stream_op_buffer *sopb, grpc_stream_op *ops,
                      size_t nops);

#endif  /* GRPC_INTERNAL_CORE_TRANSPORT_STREAM_OP_H */

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

#include <string.h>

#include "src/core/channel/compress_filter.h"
#include "src/core/channel/channel_args.h"
#include "src/core/compression/message_compress.h"
#include <grpc/compression.h>
#include <grpc/support/log.h>
#include <grpc/support/slice_buffer.h>

typedef struct call_data {
  gpr_slice_buffer slices;
  int remaining_slice_bytes;
  int no_compress;  /**< whether to skip compression for this specific call */

  grpc_linked_mdelem compression_algorithm;
} call_data;

typedef struct channel_data {
  grpc_compression_algorithm compress_algorithm;
  grpc_mdelem *compress_algorithm_md;
  grpc_mdelem *no_compression_md;
} channel_data;

/** Compress \a slices in place using \a algorithm. Returns 1 if compression did
 * actually happen, 0 otherwise (for example if the compressed output size was
 * larger than the raw input).
 *
 * Returns 1 if the data was actually compress and 0 otherwise. */
static int compress_send_sb(grpc_compression_algorithm algorithm,
                             gpr_slice_buffer *slices) {
  int did_compress;
  gpr_slice_buffer tmp;
  gpr_slice_buffer_init(&tmp);
  did_compress = grpc_msg_compress(algorithm, slices, &tmp);
  gpr_slice_buffer_swap(slices, &tmp);
  gpr_slice_buffer_destroy(&tmp);
  return did_compress;
}

static void process_send_ops(grpc_call_element *elem,
                             grpc_stream_op_buffer *send_ops) {
  call_data *calld = elem->call_data;
  channel_data *channeld = elem->channel_data;
  size_t i, j;
  int begin_message_index = -1;
  int metadata_op_index = -1;
  grpc_mdelem *actual_compression_md;

  /* buffer up slices until we've processed all the expected ones (as given by
   * GRPC_OP_BEGIN_MESSAGE) */
  for (i = 0; i < send_ops->nops; ++i) {
    grpc_stream_op *sop = &send_ops->ops[i];
    switch (sop->type) {
      case GRPC_OP_BEGIN_MESSAGE:
        begin_message_index = i;
        calld->remaining_slice_bytes = sop->data.begin_message.length;
        calld->no_compress =
            !!(sop->data.begin_message.flags & GRPC_WRITE_NO_COMPRESS);
        break;
      case GRPC_OP_SLICE:
        if (calld->no_compress) continue;
        GPR_ASSERT(calld->remaining_slice_bytes > 0);
        /* add to calld->slices */
        gpr_slice_buffer_add(&calld->slices, sop->data.slice);
        calld->remaining_slice_bytes -= GPR_SLICE_LENGTH(sop->data.slice);
        if (calld->remaining_slice_bytes == 0) {
          /* compress */
          if (!compress_send_sb(channeld->compress_algorithm, &calld->slices)) {
            calld->no_compress = 1;  /* GPR_TRUE */
          }
        }
        break;
      case GRPC_OP_METADATA:
        /* Save the index of the first metadata op, to be processed after we
         * know whether compression actually happened */
        if (metadata_op_index < 0) metadata_op_index = i;
        break;
      case GRPC_NO_OP:
        ;  /* fallthrough, ignore */
    }
  }

  if (metadata_op_index < 0 || begin_message_index < 0) { /* bail out */
    return;
  }

  /* update both the metadata and the begin_message's flags */
  if (calld->no_compress) {
    /* either because the user requested the exception or because compressing
     * would have resulted in a larger output */
    channeld->compress_algorithm = GRPC_COMPRESS_NONE;
    actual_compression_md = channeld->no_compression_md;
    /* reset the flag compression bit */
    send_ops->ops[begin_message_index].data.begin_message.flags &=
        ~GRPC_WRITE_INTERNAL_COMPRESS;
  } else {  /* DID compress */
    actual_compression_md = channeld->compress_algorithm_md;
    /* at this point, calld->slices contains the *compressed* slices from
     * send_ops->ops[*]->data.slice. We now replace these input slices with the
     * compressed ones. */
    for (i = 0, j = 0; i < send_ops->nops; ++i) {
      grpc_stream_op *sop = &send_ops->ops[i];
      GPR_ASSERT(j < calld->slices.count);
      switch (sop->type) {
        case GRPC_OP_SLICE:
          gpr_slice_unref(sop->data.slice);
          sop->data.slice = gpr_slice_ref(calld->slices.slices[j++]);
          break;
        case GRPC_OP_BEGIN_MESSAGE:
          sop->data.begin_message.length = calld->slices.length;
          sop->data.begin_message.flags |= GRPC_WRITE_INTERNAL_COMPRESS;
        case GRPC_NO_OP:
        case GRPC_OP_METADATA:
          ;  /* fallthrough, ignore */
      }
    }
  }

  grpc_metadata_batch_add_head(
      &(send_ops->ops[metadata_op_index].data.metadata),
      &calld->compression_algorithm, grpc_mdelem_ref(actual_compression_md));
}

/* Called either:
     - in response to an API call (or similar) from above, to send something
     - a network event (or similar) from below, to receive something
   op contains type and call direction information, in addition to the data
   that is being sent or received. */
static void compress_start_transport_op(grpc_call_element *elem,
                                    grpc_transport_op *op) {
  if (op->send_ops && op->send_ops->nops > 0) {
    process_send_ops(elem, op->send_ops);
  }

  /* pass control down the stack */
  grpc_call_next_op(elem, op);
}

/* Called on special channel events, such as disconnection or new incoming
   calls on the server */
static void channel_op(grpc_channel_element *elem,
                       grpc_channel_element *from_elem, grpc_channel_op *op) {
  switch (op->type) {
    default:
      grpc_channel_next_op(elem, op);
      break;
  }
}

/* Constructor for call_data */
static void init_call_elem(grpc_call_element *elem,
                           const void *server_transport_data,
                           grpc_transport_op *initial_op) {
  /* grab pointers to our data from the call element */
  call_data *calld = elem->call_data;

  /* initialize members */
  gpr_slice_buffer_init(&calld->slices);

  if (initial_op) {
    if (initial_op->send_ops && initial_op->send_ops->nops > 0) {
      process_send_ops(elem, initial_op->send_ops);
    }
  }
}

/* Destructor for call_data */
static void destroy_call_elem(grpc_call_element *elem) {
  /* grab pointers to our data from the call element */
  call_data *calld = elem->call_data;
  gpr_slice_buffer_destroy(&calld->slices);
}

/* Constructor for channel_data */
static void init_channel_elem(grpc_channel_element *elem,
                              const grpc_channel_args *args, grpc_mdctx *mdctx,
                              int is_first, int is_last) {
  channel_data *channeld = elem->channel_data;
  const grpc_compression_level clevel =
      grpc_channel_args_get_compression_level(args);
  const grpc_compression_algorithm none_alg = GRPC_COMPRESS_NONE;

  channeld->compress_algorithm_md = grpc_mdelem_from_string_and_buffer(
      mdctx, "grpc-compression-level", (gpr_uint8*)&clevel, sizeof(clevel));
  channeld->compress_algorithm = grpc_compression_algorithm_for_level(clevel);

  channeld->no_compression_md = grpc_mdelem_from_string_and_buffer(
      mdctx, "grpc-compression-level", (gpr_uint8 *)&none_alg,
      sizeof(none_alg));

  /* The first and the last filters tend to be implemented differently to
     handle the case that there's no 'next' filter to call on the up or down
     path */
  GPR_ASSERT(!is_first);
  GPR_ASSERT(!is_last);
}

/* Destructor for channel data */
static void destroy_channel_elem(grpc_channel_element *elem) {
  channel_data *channeld = elem->channel_data;
  grpc_mdelem_unref(channeld->compress_algorithm_md);
  grpc_mdelem_unref(channeld->no_compression_md);
}

const grpc_channel_filter grpc_compress_filter = {
    compress_start_transport_op, channel_op, sizeof(call_data), init_call_elem,
    destroy_call_elem, sizeof(channel_data), init_channel_elem,
    destroy_channel_elem, "compress"};

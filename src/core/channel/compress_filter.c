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

#include <assert.h>
#include <string.h>

#include <grpc/compression.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/slice_buffer.h>

#include "src/core/channel/compress_filter.h"
#include "src/core/channel/channel_args.h"
#include "src/core/profiling/timers.h"
#include "src/core/compression/message_compress.h"
#include "src/core/support/string.h"

typedef struct call_data {
  gpr_slice_buffer slices; /**< Buffers up input slices to be compressed */
  grpc_linked_mdelem compression_algorithm_storage;
  grpc_linked_mdelem accept_encoding_storage;
  gpr_uint32 remaining_slice_bytes;
  /**< Input data to be read, as per BEGIN_MESSAGE */
  int written_initial_metadata; /**< Already processed initial md? */
  /** Compression algorithm we'll try to use. It may be given by incoming
   * metadata, or by the channel's default compression settings. */
  grpc_compression_algorithm compression_algorithm;
  /** If true, contents of \a compression_algorithm are authoritative */
  int has_compression_algorithm;
} call_data;

typedef struct channel_data {
  /** Metadata key for the incoming (requested) compression algorithm */
  grpc_mdstr *mdstr_request_compression_algorithm_key;
  /** Metadata key for the outgoing (used) compression algorithm */
  grpc_mdstr *mdstr_outgoing_compression_algorithm_key;
  /** Metadata key for the accepted encodings */
  grpc_mdstr *mdstr_compression_capabilities_key;
  /** Precomputed metadata elements for all available compression algorithms */
  grpc_mdelem *mdelem_compression_algorithms[GRPC_COMPRESS_ALGORITHMS_COUNT];
  /** Precomputed metadata elements for the accepted encodings */
  grpc_mdelem *mdelem_accept_encoding;
  /** The default, channel-level, compression algorithm */
  grpc_compression_algorithm default_compression_algorithm;
  /** Compression options for the channel */
  grpc_compression_options compression_options;
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
  if (did_compress) {
    gpr_slice_buffer_swap(slices, &tmp);
  }
  gpr_slice_buffer_destroy(&tmp);
  return did_compress;
}

/** For each \a md element from the incoming metadata, filter out the entry for
 * "grpc-encoding", using its value to populate the call data's
 * compression_algorithm field. */
static grpc_mdelem *compression_md_filter(void *user_data, grpc_mdelem *md) {
  grpc_call_element *elem = user_data;
  call_data *calld = elem->call_data;
  channel_data *channeld = elem->channel_data;

  if (md->key == channeld->mdstr_request_compression_algorithm_key) {
    const char *md_c_str = grpc_mdstr_as_c_string(md->value);
    if (!grpc_compression_algorithm_parse(md_c_str, strlen(md_c_str),
                                          &calld->compression_algorithm)) {
      gpr_log(GPR_ERROR,
              "Invalid compression algorithm: '%s' (unknown). Ignoring.",
              md_c_str);
      calld->compression_algorithm = GRPC_COMPRESS_NONE;
    }
    if (grpc_compression_options_is_algorithm_enabled(
            &channeld->compression_options, calld->compression_algorithm) ==
        0) {
      gpr_log(GPR_ERROR,
              "Invalid compression algorithm: '%s' (previously disabled). "
              "Ignoring.",
              md_c_str);
      calld->compression_algorithm = GRPC_COMPRESS_NONE;
    }
    calld->has_compression_algorithm = 1;
    return NULL;
  }

  return md;
}

static int skip_compression(channel_data *channeld, call_data *calld) {
  if (calld->has_compression_algorithm) {
    if (calld->compression_algorithm == GRPC_COMPRESS_NONE) {
      return 1;
    }
    return 0; /* we have an actual call-specific algorithm */
  }
  /* no per-call compression override */
  return channeld->default_compression_algorithm == GRPC_COMPRESS_NONE;
}

/** Assembles a new grpc_stream_op_buffer with the compressed slices, modifying
 * the associated GRPC_OP_BEGIN_MESSAGE accordingly (new compressed length,
 * flags indicating compression is in effect) and replaces \a send_ops with it.
 * */
static void finish_compressed_sopb(grpc_stream_op_buffer *send_ops,
                                   grpc_call_element *elem) {
  size_t i;
  call_data *calld = elem->call_data;
  int new_slices_added = 0; /* GPR_FALSE */
  grpc_metadata_batch metadata;
  grpc_stream_op_buffer new_send_ops;
  grpc_sopb_init(&new_send_ops);

  for (i = 0; i < send_ops->nops; i++) {
    grpc_stream_op *sop = &send_ops->ops[i];
    switch (sop->type) {
      case GRPC_OP_BEGIN_MESSAGE:
        GPR_ASSERT(calld->slices.length <= GPR_UINT32_MAX);
        grpc_sopb_add_begin_message(
            &new_send_ops, (gpr_uint32)calld->slices.length,
            sop->data.begin_message.flags | GRPC_WRITE_INTERNAL_COMPRESS);
        break;
      case GRPC_OP_SLICE:
        /* Once we reach the slices section of the original buffer, simply add
         * all the new (compressed) slices. We obviously want to do this only
         * once, hence the "new_slices_added" guard. */
        if (!new_slices_added) {
          size_t j;
          for (j = 0; j < calld->slices.count; ++j) {
            grpc_sopb_add_slice(&new_send_ops,
                                gpr_slice_ref(calld->slices.slices[j]));
          }
          new_slices_added = 1; /* GPR_TRUE */
        }
        break;
      case GRPC_OP_METADATA:
        /* move the metadata to the new buffer. */
        grpc_metadata_batch_move(&metadata, &sop->data.metadata);
        grpc_sopb_add_metadata(&new_send_ops, metadata);
        break;
      case GRPC_NO_OP:
        break;
    }
  }
  grpc_sopb_swap(send_ops, &new_send_ops);
  grpc_sopb_destroy(&new_send_ops);
}

/** Filter's "main" function, called for any incoming grpc_transport_stream_op
 * instance that holds a non-zero number of send operations, accesible to this
 * function in \a send_ops.  */
static void process_send_ops(grpc_call_element *elem,
                             grpc_stream_op_buffer *send_ops) {
  call_data *calld = elem->call_data;
  channel_data *channeld = elem->channel_data;
  size_t i;
  int did_compress = 0;

  /* In streaming calls, we need to reset the previously accumulated slices */
  gpr_slice_buffer_reset_and_unref(&calld->slices);
  for (i = 0; i < send_ops->nops; ++i) {
    grpc_stream_op *sop = &send_ops->ops[i];
    switch (sop->type) {
      case GRPC_OP_BEGIN_MESSAGE:
        /* buffer up slices until we've processed all the expected ones (as
         * given by GRPC_OP_BEGIN_MESSAGE) */
        calld->remaining_slice_bytes = sop->data.begin_message.length;
        if (sop->data.begin_message.flags & GRPC_WRITE_NO_COMPRESS) {
          calld->has_compression_algorithm = 1; /* GPR_TRUE */
          calld->compression_algorithm = GRPC_COMPRESS_NONE;
        }
        break;
      case GRPC_OP_METADATA:
        if (!calld->written_initial_metadata) {
          /* Parse incoming request for compression. If any, it'll be available
           * at calld->compression_algorithm */
          grpc_metadata_batch_filter(&(sop->data.metadata),
                                     compression_md_filter, elem);
          if (!calld->has_compression_algorithm) {
            /* If no algorithm was found in the metadata and we aren't
             * exceptionally skipping compression, fall back to the channel
             * default */
            calld->compression_algorithm =
                channeld->default_compression_algorithm;
            calld->has_compression_algorithm = 1; /* GPR_TRUE */
          }
          /* hint compression algorithm */
          grpc_metadata_batch_add_tail(
              &(sop->data.metadata), &calld->compression_algorithm_storage,
              GRPC_MDELEM_REF(channeld->mdelem_compression_algorithms
                                  [calld->compression_algorithm]));

          /* convey supported compression algorithms */
          grpc_metadata_batch_add_tail(
              &(sop->data.metadata), &calld->accept_encoding_storage,
              GRPC_MDELEM_REF(channeld->mdelem_accept_encoding));

          calld->written_initial_metadata = 1; /* GPR_TRUE */
        }
        break;
      case GRPC_OP_SLICE:
        if (skip_compression(channeld, calld)) continue;
        GPR_ASSERT(calld->remaining_slice_bytes > 0);
        /* Increase input ref count, gpr_slice_buffer_add takes ownership.  */
        gpr_slice_buffer_add(&calld->slices, gpr_slice_ref(sop->data.slice));
        GPR_ASSERT(GPR_SLICE_LENGTH(sop->data.slice) >=
                   calld->remaining_slice_bytes);
        calld->remaining_slice_bytes -=
            (gpr_uint32)GPR_SLICE_LENGTH(sop->data.slice);
        if (calld->remaining_slice_bytes == 0) {
          did_compress =
              compress_send_sb(calld->compression_algorithm, &calld->slices);
        }
        break;
      case GRPC_NO_OP:
        break;
    }
  }

  /* Modify the send_ops stream_op_buffer depending on whether compression was
   * carried out */
  if (did_compress) {
    finish_compressed_sopb(send_ops, elem);
  }
}

/* Called either:
     - in response to an API call (or similar) from above, to send something
     - a network event (or similar) from below, to receive something
   op contains type and call direction information, in addition to the data
   that is being sent or received. */
static void compress_start_transport_stream_op(grpc_exec_ctx *exec_ctx,
                                               grpc_call_element *elem,
                                               grpc_transport_stream_op *op) {
  GRPC_TIMER_BEGIN("compress_start_transport_stream_op", 0);

  if (op->send_ops && op->send_ops->nops > 0) {
    process_send_ops(elem, op->send_ops);
  }

  GRPC_TIMER_END("compress_start_transport_stream_op", 0);

  /* pass control down the stack */
  grpc_call_next_op(exec_ctx, elem, op);
}

/* Constructor for call_data */
static void init_call_elem(grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
                           const void *server_transport_data,
                           grpc_transport_stream_op *initial_op) {
  /* grab pointers to our data from the call element */
  call_data *calld = elem->call_data;

  /* initialize members */
  gpr_slice_buffer_init(&calld->slices);
  calld->has_compression_algorithm = 0;
  calld->written_initial_metadata = 0; /* GPR_FALSE */

  if (initial_op) {
    if (initial_op->send_ops && initial_op->send_ops->nops > 0) {
      process_send_ops(elem, initial_op->send_ops);
    }
  }
}

/* Destructor for call_data */
static void destroy_call_elem(grpc_exec_ctx *exec_ctx,
                              grpc_call_element *elem) {
  /* grab pointers to our data from the call element */
  call_data *calld = elem->call_data;
  gpr_slice_buffer_destroy(&calld->slices);
}

/* Constructor for channel_data */
static void init_channel_elem(grpc_exec_ctx *exec_ctx,
                              grpc_channel_element *elem, grpc_channel *master,
                              const grpc_channel_args *args, grpc_mdctx *mdctx,
                              int is_first, int is_last) {
  channel_data *channeld = elem->channel_data;
  grpc_compression_algorithm algo_idx;
  const char *supported_algorithms_names[GRPC_COMPRESS_ALGORITHMS_COUNT - 1];
  size_t supported_algorithms_idx = 0;
  char *accept_encoding_str;
  size_t accept_encoding_str_len;

  grpc_compression_options_init(&channeld->compression_options);
  channeld->compression_options.enabled_algorithms_bitset =
      (gpr_uint32)grpc_channel_args_compression_algorithm_get_states(args);

  channeld->default_compression_algorithm =
      grpc_channel_args_get_compression_algorithm(args);
  /* Make sure the default isn't disabled. */
  GPR_ASSERT(grpc_compression_options_is_algorithm_enabled(
      &channeld->compression_options, channeld->default_compression_algorithm));
  channeld->compression_options.default_compression_algorithm =
      channeld->default_compression_algorithm;

  channeld->mdstr_request_compression_algorithm_key =
      grpc_mdstr_from_string(mdctx, GRPC_COMPRESS_REQUEST_ALGORITHM_KEY);

  channeld->mdstr_outgoing_compression_algorithm_key =
      grpc_mdstr_from_string(mdctx, "grpc-encoding");

  channeld->mdstr_compression_capabilities_key =
      grpc_mdstr_from_string(mdctx, "grpc-accept-encoding");

  for (algo_idx = 0; algo_idx < GRPC_COMPRESS_ALGORITHMS_COUNT; ++algo_idx) {
    char *algorithm_name;
    /* skip disabled algorithms */
    if (grpc_compression_options_is_algorithm_enabled(
            &channeld->compression_options, algo_idx) == 0) {
      continue;
    }
    GPR_ASSERT(grpc_compression_algorithm_name(algo_idx, &algorithm_name) != 0);
    channeld->mdelem_compression_algorithms[algo_idx] =
        grpc_mdelem_from_metadata_strings(
            mdctx,
            GRPC_MDSTR_REF(channeld->mdstr_outgoing_compression_algorithm_key),
            grpc_mdstr_from_string(mdctx, algorithm_name));
    if (algo_idx > 0) {
      supported_algorithms_names[supported_algorithms_idx++] = algorithm_name;
    }
  }

  /* TODO(dgq): gpr_strjoin_sep could be made to work with statically allocated
   * arrays, as to avoid the heap allocs */
  accept_encoding_str =
      gpr_strjoin_sep(supported_algorithms_names, supported_algorithms_idx, ",",
                      &accept_encoding_str_len);

  channeld->mdelem_accept_encoding = grpc_mdelem_from_metadata_strings(
      mdctx, GRPC_MDSTR_REF(channeld->mdstr_compression_capabilities_key),
      grpc_mdstr_from_string(mdctx, accept_encoding_str));
  gpr_free(accept_encoding_str);

  GPR_ASSERT(!is_last);
}

/* Destructor for channel data */
static void destroy_channel_elem(grpc_exec_ctx *exec_ctx,
                                 grpc_channel_element *elem) {
  channel_data *channeld = elem->channel_data;
  grpc_compression_algorithm algo_idx;

  GRPC_MDSTR_UNREF(channeld->mdstr_request_compression_algorithm_key);
  GRPC_MDSTR_UNREF(channeld->mdstr_outgoing_compression_algorithm_key);
  GRPC_MDSTR_UNREF(channeld->mdstr_compression_capabilities_key);
  for (algo_idx = 0; algo_idx < GRPC_COMPRESS_ALGORITHMS_COUNT; ++algo_idx) {
    GRPC_MDELEM_UNREF(channeld->mdelem_compression_algorithms[algo_idx]);
  }
  GRPC_MDELEM_UNREF(channeld->mdelem_accept_encoding);
}

const grpc_channel_filter grpc_compress_filter = {
    compress_start_transport_stream_op, grpc_channel_next_op, sizeof(call_data),
    init_call_elem, destroy_call_elem, sizeof(channel_data), init_channel_elem,
    destroy_channel_elem, grpc_call_next_get_peer, "compress"};

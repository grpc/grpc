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
#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/compress_filter.h"
#include "src/core/lib/compression/algorithm_metadata.h"
#include "src/core/lib/compression/message_compress.h"
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/support/string.h"
#include "src/core/lib/transport/static_metadata.h"

int grpc_compression_trace = 0;

typedef struct call_data {
  grpc_slice_buffer slices; /**< Buffers up input slices to be compressed */
  grpc_linked_mdelem compression_algorithm_storage;
  grpc_linked_mdelem accept_encoding_storage;
  uint32_t remaining_slice_bytes;
  /** Compression algorithm we'll try to use. It may be given by incoming
   * metadata, or by the channel's default compression settings. */
  grpc_compression_algorithm compression_algorithm;
  /** If true, contents of \a compression_algorithm are authoritative */
  int has_compression_algorithm;

  grpc_transport_stream_op_batch *send_op;
  uint32_t send_length;
  uint32_t send_flags;
  grpc_slice incoming_slice;
  grpc_slice_buffer_stream replacement_stream;
  grpc_closure *post_send;
  grpc_closure send_done;
  grpc_closure got_slice;
} call_data;

typedef struct channel_data {
  /** The default, channel-level, compression algorithm */
  grpc_compression_algorithm default_compression_algorithm;
  /** Bitset of enabled algorithms */
  uint32_t enabled_algorithms_bitset;
  /** Supported compression algorithms */
  uint32_t supported_compression_algorithms;
} channel_data;

static int skip_compression(grpc_call_element *elem, uint32_t flags) {
  call_data *calld = elem->call_data;
  channel_data *channeld = elem->channel_data;

  if (flags & (GRPC_WRITE_NO_COMPRESS | GRPC_WRITE_INTERNAL_COMPRESS)) {
    return 1;
  }
  if (calld->has_compression_algorithm) {
    if (calld->compression_algorithm == GRPC_COMPRESS_NONE) {
      return 1;
    }
    return 0; /* we have an actual call-specific algorithm */
  }
  /* no per-call compression override */
  return channeld->default_compression_algorithm == GRPC_COMPRESS_NONE;
}

/** Filter initial metadata */
static grpc_error *process_send_initial_metadata(
    grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
    grpc_metadata_batch *initial_metadata) GRPC_MUST_USE_RESULT;
static grpc_error *process_send_initial_metadata(
    grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
    grpc_metadata_batch *initial_metadata) {
  call_data *calld = elem->call_data;
  channel_data *channeld = elem->channel_data;
  /* Parse incoming request for compression. If any, it'll be available
   * at calld->compression_algorithm */
  if (initial_metadata->idx.named.grpc_internal_encoding_request != NULL) {
    grpc_mdelem md =
        initial_metadata->idx.named.grpc_internal_encoding_request->md;
    if (!grpc_compression_algorithm_parse(GRPC_MDVALUE(md),
                                          &calld->compression_algorithm)) {
      char *val = grpc_slice_to_c_string(GRPC_MDVALUE(md));
      gpr_log(GPR_ERROR,
              "Invalid compression algorithm: '%s' (unknown). Ignoring.", val);
      gpr_free(val);
      calld->compression_algorithm = GRPC_COMPRESS_NONE;
    }
    if (!GPR_BITGET(channeld->enabled_algorithms_bitset,
                    calld->compression_algorithm)) {
      char *val = grpc_slice_to_c_string(GRPC_MDVALUE(md));
      gpr_log(GPR_ERROR,
              "Invalid compression algorithm: '%s' (previously disabled). "
              "Ignoring.",
              val);
      gpr_free(val);
      calld->compression_algorithm = GRPC_COMPRESS_NONE;
    }
    calld->has_compression_algorithm = 1;

    grpc_metadata_batch_remove(
        exec_ctx, initial_metadata,
        initial_metadata->idx.named.grpc_internal_encoding_request);
  } else {
    /* If no algorithm was found in the metadata and we aren't
     * exceptionally skipping compression, fall back to the channel
     * default */
    calld->compression_algorithm = channeld->default_compression_algorithm;
    calld->has_compression_algorithm = 1; /* GPR_TRUE */
  }

  grpc_error *error = GRPC_ERROR_NONE;
  /* hint compression algorithm */
  if (calld->compression_algorithm != GRPC_COMPRESS_NONE) {
    error = grpc_metadata_batch_add_tail(
        exec_ctx, initial_metadata, &calld->compression_algorithm_storage,
        grpc_compression_encoding_mdelem(calld->compression_algorithm));
  }

  if (error != GRPC_ERROR_NONE) return error;

  /* convey supported compression algorithms */
  error = grpc_metadata_batch_add_tail(
      exec_ctx, initial_metadata, &calld->accept_encoding_storage,
      GRPC_MDELEM_ACCEPT_ENCODING_FOR_ALGORITHMS(
          channeld->supported_compression_algorithms));

  return error;
}

static void continue_send_message(grpc_exec_ctx *exec_ctx,
                                  grpc_call_element *elem);

static void send_done(grpc_exec_ctx *exec_ctx, void *elemp, grpc_error *error) {
  grpc_call_element *elem = elemp;
  call_data *calld = elem->call_data;
  grpc_slice_buffer_reset_and_unref_internal(exec_ctx, &calld->slices);
  calld->post_send->cb(exec_ctx, calld->post_send->cb_arg, error);
}

static void finish_send_message(grpc_exec_ctx *exec_ctx,
                                grpc_call_element *elem) {
  call_data *calld = elem->call_data;
  int did_compress;
  grpc_slice_buffer tmp;
  grpc_slice_buffer_init(&tmp);
  did_compress = grpc_msg_compress(exec_ctx, calld->compression_algorithm,
                                   &calld->slices, &tmp);
  if (did_compress) {
    if (grpc_compression_trace) {
      char *algo_name;
      const size_t before_size = calld->slices.length;
      const size_t after_size = tmp.length;
      const float savings_ratio = 1.0f - (float)after_size / (float)before_size;
      GPR_ASSERT(grpc_compression_algorithm_name(calld->compression_algorithm,
                                                 &algo_name));
      gpr_log(GPR_DEBUG, "Compressed[%s] %" PRIuPTR " bytes vs. %" PRIuPTR
                         " bytes (%.2f%% savings)",
              algo_name, before_size, after_size, 100 * savings_ratio);
    }
    grpc_slice_buffer_swap(&calld->slices, &tmp);
    calld->send_flags |= GRPC_WRITE_INTERNAL_COMPRESS;
  } else {
    if (grpc_compression_trace) {
      char *algo_name;
      GPR_ASSERT(grpc_compression_algorithm_name(calld->compression_algorithm,
                                                 &algo_name));
      gpr_log(GPR_DEBUG,
              "Algorithm '%s' enabled but decided not to compress. Input size: "
              "%" PRIuPTR,
              algo_name, calld->slices.length);
    }
  }

  grpc_slice_buffer_destroy_internal(exec_ctx, &tmp);

  grpc_slice_buffer_stream_init(&calld->replacement_stream, &calld->slices,
                                calld->send_flags);
  calld->send_op->payload->send_message.send_message =
      &calld->replacement_stream.base;
  calld->post_send = calld->send_op->on_complete;
  calld->send_op->on_complete = &calld->send_done;

  grpc_call_next_op(exec_ctx, elem, calld->send_op);
}

static void got_slice(grpc_exec_ctx *exec_ctx, void *elemp, grpc_error *error) {
  grpc_call_element *elem = elemp;
  call_data *calld = elem->call_data;
  grpc_slice_buffer_add(&calld->slices, calld->incoming_slice);
  if (calld->send_length == calld->slices.length) {
    finish_send_message(exec_ctx, elem);
  } else {
    continue_send_message(exec_ctx, elem);
  }
}

static void continue_send_message(grpc_exec_ctx *exec_ctx,
                                  grpc_call_element *elem) {
  call_data *calld = elem->call_data;
  while (grpc_byte_stream_next(
      exec_ctx, calld->send_op->payload->send_message.send_message,
      &calld->incoming_slice, ~(size_t)0, &calld->got_slice)) {
    grpc_slice_buffer_add(&calld->slices, calld->incoming_slice);
    if (calld->send_length == calld->slices.length) {
      finish_send_message(exec_ctx, elem);
      break;
    }
  }
}

static void compress_start_transport_stream_op_batch(
    grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
    grpc_transport_stream_op_batch *op) {
  call_data *calld = elem->call_data;

  GPR_TIMER_BEGIN("compress_start_transport_stream_op_batch", 0);

  if (op->send_initial_metadata) {
    grpc_error *error = process_send_initial_metadata(
        exec_ctx, elem,
        op->payload->send_initial_metadata.send_initial_metadata);
    if (error != GRPC_ERROR_NONE) {
      grpc_transport_stream_op_batch_finish_with_failure(exec_ctx, op, error);
      return;
    }
  }
  if (op->send_message &&
      !skip_compression(elem, op->payload->send_message.send_message->flags)) {
    calld->send_op = op;
    calld->send_length = op->payload->send_message.send_message->length;
    calld->send_flags = op->payload->send_message.send_message->flags;
    continue_send_message(exec_ctx, elem);
  } else {
    /* pass control down the stack */
    grpc_call_next_op(exec_ctx, elem, op);
  }

  GPR_TIMER_END("compress_start_transport_stream_op_batch", 0);
}

/* Constructor for call_data */
static grpc_error *init_call_elem(grpc_exec_ctx *exec_ctx,
                                  grpc_call_element *elem,
                                  const grpc_call_element_args *args) {
  /* grab pointers to our data from the call element */
  call_data *calld = elem->call_data;

  /* initialize members */
  grpc_slice_buffer_init(&calld->slices);
  calld->has_compression_algorithm = 0;
  grpc_closure_init(&calld->got_slice, got_slice, elem,
                    grpc_schedule_on_exec_ctx);
  grpc_closure_init(&calld->send_done, send_done, elem,
                    grpc_schedule_on_exec_ctx);

  return GRPC_ERROR_NONE;
}

/* Destructor for call_data */
static void destroy_call_elem(grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
                              const grpc_call_final_info *final_info,
                              grpc_closure *ignored) {
  /* grab pointers to our data from the call element */
  call_data *calld = elem->call_data;
  grpc_slice_buffer_destroy_internal(exec_ctx, &calld->slices);
}

/* Constructor for channel_data */
static grpc_error *init_channel_elem(grpc_exec_ctx *exec_ctx,
                                     grpc_channel_element *elem,
                                     grpc_channel_element_args *args) {
  channel_data *channeld = elem->channel_data;

  channeld->enabled_algorithms_bitset =
      grpc_channel_args_compression_algorithm_get_states(args->channel_args);

  channeld->default_compression_algorithm =
      grpc_channel_args_get_compression_algorithm(args->channel_args);
  /* Make sure the default isn't disabled. */
  if (!GPR_BITGET(channeld->enabled_algorithms_bitset,
                  channeld->default_compression_algorithm)) {
    gpr_log(GPR_DEBUG,
            "compression algorithm %d not enabled: switching to none",
            channeld->default_compression_algorithm);
    channeld->default_compression_algorithm = GRPC_COMPRESS_NONE;
  }

  channeld->supported_compression_algorithms = 1; /* always support identity */
  for (grpc_compression_algorithm algo_idx = 1;
       algo_idx < GRPC_COMPRESS_ALGORITHMS_COUNT; ++algo_idx) {
    /* skip disabled algorithms */
    if (!GPR_BITGET(channeld->enabled_algorithms_bitset, algo_idx)) {
      continue;
    }
    channeld->supported_compression_algorithms |= 1u << algo_idx;
  }

  GPR_ASSERT(!args->is_last);
  return GRPC_ERROR_NONE;
}

/* Destructor for channel data */
static void destroy_channel_elem(grpc_exec_ctx *exec_ctx,
                                 grpc_channel_element *elem) {}

const grpc_channel_filter grpc_compress_filter = {
    compress_start_transport_stream_op_batch,
    grpc_channel_next_op,
    sizeof(call_data),
    init_call_elem,
    grpc_call_stack_ignore_set_pollset_or_pollset_set,
    destroy_call_elem,
    sizeof(channel_data),
    init_channel_elem,
    destroy_channel_elem,
    grpc_call_next_get_peer,
    grpc_channel_next_get_info,
    "compress"};

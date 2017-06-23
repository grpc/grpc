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

#include <assert.h>
#include <string.h>

#include <grpc/compression.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/ext/filters/http/message_compress/message_compress_filter.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/compression/algorithm_metadata.h"
#include "src/core/lib/compression/message_compress.h"
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/support/string.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/transport/static_metadata.h"

typedef enum {
  // Initial metadata not yet seen.
  INITIAL_METADATA_UNSEEN = 0,
  // Initial metadata seen; compression algorithm set.
  HAS_COMPRESSION_ALGORITHM,
  // Initial metadata seen; no compression algorithm set.
  NO_COMPRESSION_ALGORITHM,
} initial_metadata_state;

typedef struct call_data {
  grpc_call_combiner *call_combiner;
  grpc_slice_buffer slices; /**< Buffers up input slices to be compressed */
  grpc_linked_mdelem compression_algorithm_storage;
  grpc_linked_mdelem accept_encoding_storage;
  uint32_t remaining_slice_bytes;
  /** Compression algorithm we'll try to use. It may be given by incoming
   * metadata, or by the channel's default compression settings. */
  grpc_compression_algorithm compression_algorithm;

  initial_metadata_state send_initial_metadata_state;
  grpc_error *cancel_error;

  grpc_transport_stream_op_batch *send_op;
  uint32_t send_length;
  uint32_t send_flags;
  grpc_slice incoming_slice;
  grpc_slice_buffer_stream replacement_stream;
  grpc_closure *post_send;
  grpc_closure send_in_call_combiner;
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

static bool skip_compression(grpc_call_element *elem, uint32_t flags,
                             bool has_compression_algorithm) {
  call_data *calld = elem->call_data;
  channel_data *channeld = elem->channel_data;

  if (flags & (GRPC_WRITE_NO_COMPRESS | GRPC_WRITE_INTERNAL_COMPRESS)) {
    return 1;
  }
  if (has_compression_algorithm) {
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
    grpc_metadata_batch *initial_metadata,
    bool *has_compression_algorithm) GRPC_MUST_USE_RESULT;
static grpc_error *process_send_initial_metadata(
    grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
    grpc_metadata_batch *initial_metadata, bool *has_compression_algorithm) {
  call_data *calld = elem->call_data;
  channel_data *channeld = elem->channel_data;
  *has_compression_algorithm = false;
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
    *has_compression_algorithm = true;

    grpc_metadata_batch_remove(
        exec_ctx, initial_metadata,
        initial_metadata->idx.named.grpc_internal_encoding_request);
  } else {
    /* If no algorithm was found in the metadata and we aren't
     * exceptionally skipping compression, fall back to the channel
     * default */
    calld->compression_algorithm = channeld->default_compression_algorithm;
    *has_compression_algorithm = true;
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

static bool continue_send_message(grpc_exec_ctx *exec_ctx,
                                  grpc_call_element *elem,
                                  bool in_call_combiner);

static void send_done(grpc_exec_ctx *exec_ctx, void *elemp, grpc_error *error) {
  grpc_call_element *elem = elemp;
  call_data *calld = elem->call_data;
  grpc_slice_buffer_reset_and_unref_internal(exec_ctx, &calld->slices);
  calld->post_send->cb(exec_ctx, calld->post_send->cb_arg, error);
}

static void send_in_call_combiner(grpc_exec_ctx *exec_ctx, void *arg,
                                  grpc_error *ignored) {
  grpc_call_element *elem = arg;
  call_data *calld = elem->call_data;
  grpc_call_next_op(exec_ctx, elem, calld->send_op);
}

static void finish_send_message(grpc_exec_ctx *exec_ctx,
                                grpc_call_element *elem,
                                bool in_call_combiner) {
gpr_log(GPR_INFO, "==> %s(): elem=%p, in_call_combiner=%d", __func__, elem, in_call_combiner);
  call_data *calld = elem->call_data;
  int did_compress;
  grpc_slice_buffer tmp;
  grpc_slice_buffer_init(&tmp);
  did_compress = grpc_msg_compress(exec_ctx, calld->compression_algorithm,
                                   &calld->slices, &tmp);
  if (did_compress) {
    if (GRPC_TRACER_ON(grpc_compression_trace)) {
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
    if (GRPC_TRACER_ON(grpc_compression_trace)) {
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

  // If we're not in the call combiner, schedule a closure on the
  // call combiner to send the op down.
  if (!in_call_combiner) {
gpr_log(GPR_INFO, "SCHEDULING send_message CLOSURE ON call_combiner=%p",calld->call_combiner);
    GRPC_CLOSURE_SCHED(exec_ctx, &calld->send_in_call_combiner,
                       GRPC_ERROR_NONE);
  }
}

static void got_slice(grpc_exec_ctx *exec_ctx, void *elemp, grpc_error *error) {
  grpc_call_element *elem = elemp;
gpr_log(GPR_INFO, "==> %s(): elem=%p", __func__, elem);
  call_data *calld = elem->call_data;
  if (GRPC_ERROR_NONE !=
      grpc_byte_stream_pull(exec_ctx,
                            calld->send_op->payload->send_message.send_message,
                            &calld->incoming_slice)) {
    /* Should never reach here */
    abort();
  }
  grpc_slice_buffer_add(&calld->slices, calld->incoming_slice);
  if (calld->send_length == calld->slices.length) {
    finish_send_message(exec_ctx, elem, false /* in_call_combiner */);
  } else {
    continue_send_message(exec_ctx, elem, false /* in_call_combiner */);
  }
}

// Returns true if compression is done.
static bool continue_send_message(grpc_exec_ctx *exec_ctx,
                                  grpc_call_element *elem,
                                  bool in_call_combiner) {
gpr_log(GPR_INFO, "==> %s(): elem=%p, in_call_combiner=%d", __func__, elem, in_call_combiner);
  call_data *calld = elem->call_data;
  while (grpc_byte_stream_next(
      exec_ctx, calld->send_op->payload->send_message.send_message, ~(size_t)0,
      &calld->got_slice)) {
    grpc_byte_stream_pull(exec_ctx,
                          calld->send_op->payload->send_message.send_message,
                          &calld->incoming_slice);
    grpc_slice_buffer_add(&calld->slices, calld->incoming_slice);
    if (calld->send_length == calld->slices.length) {
      finish_send_message(exec_ctx, elem, in_call_combiner);
gpr_log(GPR_INFO, "<== %s(): elem=%p: TRUE", __func__, elem);
      return true;
    }
  }
gpr_log(GPR_INFO, "<=> %s(): elem=%p: FALSE", __func__, elem);
  return false;
}

// Returns true if compression is done.
static bool start_send_message_op(grpc_exec_ctx *exec_ctx,
                                  grpc_call_element *elem,
                                  bool in_call_combiner) {
gpr_log(GPR_INFO, "==> %s(): elem=%p, in_call_combiner=%d", __func__, elem, in_call_combiner);
  call_data *calld = elem->call_data;
  bool done = true;
  if (!skip_compression(
          elem, calld->send_op->payload->send_message.send_message->flags,
          calld->send_initial_metadata_state == HAS_COMPRESSION_ALGORITHM)) {
    calld->send_length =
        calld->send_op->payload->send_message.send_message->length;
    calld->send_flags =
        calld->send_op->payload->send_message.send_message->flags;
    done = continue_send_message(exec_ctx, elem, in_call_combiner);
  }
gpr_log(GPR_INFO, "<== %s(): elem=%p: done=%d", __func__, elem, done);
  return done;
}

static void compress_start_transport_stream_op_batch_inner(
    grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
    grpc_transport_stream_op_batch *op) {
gpr_log(GPR_INFO, "==> %s(): op={send_initial_metadata=%d, send_message=%d, send_trailing_metadata=%d, recv_initial_metadata=%d, recv_message=%d, recv_trailing_metadata=%d, cancel_stream=%d, collect_stats=%d}", __func__, op->send_initial_metadata, op->send_message, op->send_trailing_metadata, op->recv_initial_metadata, op->recv_message, op->recv_trailing_metadata, op->cancel_stream, op->collect_stats);
  call_data *calld = elem->call_data;
  // Handle cancel_stream.
  if (op->cancel_stream) {
    GRPC_ERROR_UNREF(calld->cancel_error);
    calld->cancel_error =
        GRPC_ERROR_REF(op->payload->cancel_stream.cancel_error);
    if (calld->send_op != NULL) {
gpr_log(GPR_INFO, "FAILING send_message BATCH ON call_combiner=%p", calld->call_combiner);
      grpc_transport_stream_op_batch_finish_with_failure(
          exec_ctx, calld->send_op, GRPC_ERROR_REF(calld->cancel_error),
          calld->call_combiner);
    }
// FIXME: is this right?
  } else if (calld->cancel_error != GRPC_ERROR_NONE) {
gpr_log(GPR_INFO, "FAILING current BATCH ON call_combiner=%p", calld->call_combiner);
    grpc_transport_stream_op_batch_finish_with_failure(
        exec_ctx, op, GRPC_ERROR_REF(calld->cancel_error),
        calld->call_combiner);
gpr_log(GPR_INFO, "STOPPING call_combiner=%p", calld->call_combiner);
    grpc_call_combiner_stop(exec_ctx, calld->call_combiner);
    return;
  }
  // Handle send_initial_metadata.
  if (op->send_initial_metadata) {
    GPR_ASSERT(calld->send_initial_metadata_state == INITIAL_METADATA_UNSEEN);
    bool has_compression_algorithm;
    grpc_error *error = process_send_initial_metadata(
        exec_ctx, elem,
        op->payload->send_initial_metadata.send_initial_metadata,
        &has_compression_algorithm);
    if (error != GRPC_ERROR_NONE) {
gpr_log(GPR_INFO, "FAILING BATCH ON call_combiner=%p", calld->call_combiner);
      grpc_transport_stream_op_batch_finish_with_failure(exec_ctx, op, error,
                                                         calld->call_combiner);
gpr_log(GPR_INFO, "STOPPING call_combiner=%p", calld->call_combiner);
      grpc_call_combiner_stop(exec_ctx, calld->call_combiner);
      return;
    }
    calld->send_initial_metadata_state = has_compression_algorithm
                                             ? HAS_COMPRESSION_ALGORITHM
                                             : NO_COMPRESSION_ALGORITHM;
    // If we had previously received a batch containing a send_message op,
    // send it down now.  Note that we need to re-enter the call combiner
    // for this, since we can't send two batches down while holding the
    // call combiner, since the connected_channel filter (at the bottom of
    // the call stack) will release the call combiner for each batch it sees.
    if (calld->send_op != NULL) {
gpr_log(GPR_INFO, "  found send_msg op from before send_initial_metadata");
      if (start_send_message_op(exec_ctx, elem, false /* in_call_combiner */)) {
gpr_log(GPR_INFO, "  compression done, scheduling send_msg op on call combiner");
gpr_log(GPR_INFO, "SCHEDULING send_message BATCH ON call_combiner=%p", calld->call_combiner);
        GRPC_CLOSURE_SCHED(exec_ctx, &calld->send_in_call_combiner,
                           GRPC_ERROR_NONE);
      }
    }
  }
  // Handle send_message.
  if (op->send_message) {
    // There are several cases here:
    // 1. We have not yet received initial metadata, in which case we
    //    give up the call combiner and stop here.  This batch will be
    //    sent down when we receive initial metadata.
    // 2. We have received initial metadata, and we have completed
    //    compression (or no compression was needed), in which case we send
    //    (a possibly modified version of) the batch down.
    // 3. We have received initial metadata, and compression is going to be
    //    completed asynchronously.  In this case, we give up the call
    //    combiner and will re-enter it once the compression is completed
    //    to send the batch down.
    calld->send_op = op;
    if (calld->send_initial_metadata_state == INITIAL_METADATA_UNSEEN ||
        !start_send_message_op(exec_ctx, elem, true /* in_call_combiner */)) {
if (calld->send_initial_metadata_state == INITIAL_METADATA_UNSEEN) {
gpr_log(GPR_INFO, "  got send_msg before send_initial_metadata, releasing call combiner");
} else {
gpr_log(GPR_INFO, "  compression not done, releasing call combiner");
}
      // Cases 1 and 3.
      // Not processing further right now, so give up combiner lock.
gpr_log(GPR_INFO, "STOPPING call_combiner=%p", calld->call_combiner);
      grpc_call_combiner_stop(exec_ctx, calld->call_combiner);
      return;
    }
    // Case 2 (fallthrough).
gpr_log(GPR_INFO, "  compression done, proceding as usual");
  }
  // Pass control down the stack.
  grpc_call_next_op(exec_ctx, elem, op);
}

static void compress_start_transport_stream_op_batch(
    grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
    grpc_transport_stream_op_batch *op) {
  GPR_TIMER_BEGIN("compress_start_transport_stream_op_batch", 0);
  compress_start_transport_stream_op_batch_inner(exec_ctx, elem, op);
  GPR_TIMER_END("compress_start_transport_stream_op_batch", 0);
}

/* Constructor for call_data */
static grpc_error *init_call_elem(grpc_exec_ctx *exec_ctx,
                                  grpc_call_element *elem,
                                  const grpc_call_element_args *args) {
  /* grab pointers to our data from the call element */
  call_data *calld = elem->call_data;

  /* initialize members */
  calld->call_combiner = args->call_combiner;
  calld->cancel_error = GRPC_ERROR_NONE;
  grpc_slice_buffer_init(&calld->slices);
  GRPC_CLOSURE_INIT(&calld->send_in_call_combiner, send_in_call_combiner, elem,
                    &calld->call_combiner->scheduler);
  GRPC_CLOSURE_INIT(&calld->got_slice, got_slice, elem,
                    grpc_schedule_on_exec_ctx);
  GRPC_CLOSURE_INIT(&calld->send_done, send_done, elem,
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
  GRPC_ERROR_UNREF(calld->cancel_error);
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

const grpc_channel_filter grpc_message_compress_filter = {
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
    "message_compress"};

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

#define INITIAL_METADATA_UNSEEN 0
#define HAS_COMPRESSION_ALGORITHM 2
#define NO_COMPRESSION_ALGORITHM 4

#define CANCELLED_BIT ((gpr_atm)1)

typedef struct call_data {
  grpc_slice_buffer slices; /**< Buffers up input slices to be compressed */
  grpc_linked_mdelem compression_algorithm_storage;
  grpc_linked_mdelem accept_encoding_storage;
  uint32_t remaining_slice_bytes;
  /** Compression algorithm we'll try to use. It may be given by incoming
   * metadata, or by the channel's default compression settings. */
  grpc_compression_algorithm compression_algorithm;

  /* Atomic recording the state of initial metadata; allowed values:
     INITIAL_METADATA_UNSEEN - initial metadata op not seen
     HAS_COMPRESSION_ALGORITHM - initial metadata seen; compression algorithm
                                 set
     NO_COMPRESSION_ALGORITHM - initial metadata seen; no compression algorithm
                                set
     pointer - a stalled op containing a send_message that's waiting on initial
               metadata
     pointer | CANCELLED_BIT - request was cancelled with error pointed to */
  gpr_atm send_initial_metadata_state;

  grpc_transport_stream_op_batch *send_message_batch;
  grpc_slice_buffer_stream replacement_stream;
  grpc_closure *original_send_message_on_complete;
  grpc_closure send_message_on_complete;
  grpc_closure on_send_message_next_done;
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

static void send_message_on_complete(grpc_exec_ctx *exec_ctx, void *arg,
                                     grpc_error *error) {
  grpc_call_element *elem = (grpc_call_element *)arg;
  call_data *calld = (call_data *)elem->call_data;
  grpc_slice_buffer_reset_and_unref_internal(exec_ctx, &calld->slices);
  GRPC_CLOSURE_RUN(exec_ctx, calld->original_send_message_on_complete,
                   GRPC_ERROR_REF(error));
}

static void finish_send_message(grpc_exec_ctx *exec_ctx,
                                grpc_call_element *elem) {
  call_data *calld = (call_data *)elem->call_data;
  // Compress the data if appropriate.
  grpc_slice_buffer tmp;
  grpc_slice_buffer_init(&tmp);
  uint32_t send_flags =
      calld->send_message_batch->payload->send_message.send_message->flags;
  const bool did_compress = grpc_msg_compress(
      exec_ctx, calld->compression_algorithm, &calld->slices, &tmp);
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
    send_flags |= GRPC_WRITE_INTERNAL_COMPRESS;
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
  // Swap out the original byte stream with our new one and send the
  // batch down.
  grpc_byte_stream_destroy(
      exec_ctx, calld->send_message_batch->payload->send_message.send_message);
  grpc_slice_buffer_stream_init(&calld->replacement_stream, &calld->slices,
                                send_flags);
  calld->send_message_batch->payload->send_message.send_message =
      &calld->replacement_stream.base;
  calld->original_send_message_on_complete =
      calld->send_message_batch->on_complete;
  calld->send_message_batch->on_complete = &calld->send_message_on_complete;
  grpc_call_next_op(exec_ctx, elem, calld->send_message_batch);
}

// Pulls a slice from the send_message byte stream and adds it to calld->slices.
static grpc_error *pull_slice_from_send_message(grpc_exec_ctx *exec_ctx,
                                                call_data *calld) {
  grpc_slice incoming_slice;
  grpc_error *error = grpc_byte_stream_pull(
      exec_ctx, calld->send_message_batch->payload->send_message.send_message,
      &incoming_slice);
  if (error == GRPC_ERROR_NONE) {
    grpc_slice_buffer_add(&calld->slices, incoming_slice);
  }
  return error;
}

// Reads as many slices as possible from the send_message byte stream.
// If all data has been read, invokes finish_send_message().  Otherwise,
// an async call to grpc_byte_stream_next() has been started, which will
// eventually result in calling on_send_message_next_done().
static grpc_error *continue_reading_send_message(grpc_exec_ctx *exec_ctx,
                                                 grpc_call_element *elem) {
  call_data *calld = (call_data *)elem->call_data;
  while (grpc_byte_stream_next(
      exec_ctx, calld->send_message_batch->payload->send_message.send_message,
      ~(size_t)0, &calld->on_send_message_next_done)) {
    grpc_error *error = pull_slice_from_send_message(exec_ctx, calld);
    if (error != GRPC_ERROR_NONE) return error;
    if (calld->slices.length ==
        calld->send_message_batch->payload->send_message.send_message->length) {
      finish_send_message(exec_ctx, elem);
      break;
    }
  }
  return GRPC_ERROR_NONE;
}

// Async callback for grpc_byte_stream_next().
static void on_send_message_next_done(grpc_exec_ctx *exec_ctx, void *arg,
                                      grpc_error *error) {
  grpc_call_element *elem = (grpc_call_element *)arg;
  call_data *calld = (call_data *)elem->call_data;
  if (error != GRPC_ERROR_NONE) goto fail;
  error = pull_slice_from_send_message(exec_ctx, calld);
  if (error != GRPC_ERROR_NONE) goto fail;
  if (calld->slices.length ==
      calld->send_message_batch->payload->send_message.send_message->length) {
    finish_send_message(exec_ctx, elem);
  } else {
    // This will either finish reading all of the data and invoke
    // finish_send_message(), or else it will make an async call to
    // grpc_byte_stream_next(), which will eventually result in calling
    // this function again.
    error = continue_reading_send_message(exec_ctx, elem);
    if (error != GRPC_ERROR_NONE) goto fail;
  }
  return;
fail:
  grpc_transport_stream_op_batch_finish_with_failure(
      exec_ctx, calld->send_message_batch, error);
}

static void start_send_message_batch(grpc_exec_ctx *exec_ctx,
                                     grpc_call_element *elem,
                                     grpc_transport_stream_op_batch *batch,
                                     bool has_compression_algorithm) {
  call_data *calld = (call_data *)elem->call_data;
  if (!skip_compression(elem, batch->payload->send_message.send_message->flags,
                        has_compression_algorithm)) {
    calld->send_message_batch = batch;
    // This will either finish reading all of the data and invoke
    // finish_send_message(), or else it will make an async call to
    // grpc_byte_stream_next(), which will eventually result in calling
    // on_send_message_next_done().
    grpc_error *error = continue_reading_send_message(exec_ctx, elem);
    if (error != GRPC_ERROR_NONE) {
      grpc_transport_stream_op_batch_finish_with_failure(
          exec_ctx, calld->send_message_batch, error);
    }
  } else {
    /* pass control down the stack */
    grpc_call_next_op(exec_ctx, elem, batch);
  }
}

static void compress_start_transport_stream_op_batch(
    grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
    grpc_transport_stream_op_batch *batch) {
  call_data *calld = elem->call_data;

  GPR_TIMER_BEGIN("compress_start_transport_stream_op_batch", 0);

  if (batch->cancel_stream) {
    // TODO(roth): As part of the upcoming call combiner work, change
    // this to call grpc_byte_stream_shutdown() on the incoming byte
    // stream, to cancel any in-flight calls to grpc_byte_stream_next().
    GRPC_ERROR_REF(batch->payload->cancel_stream.cancel_error);
    gpr_atm cur = gpr_atm_full_xchg(
        &calld->send_initial_metadata_state,
        CANCELLED_BIT | (gpr_atm)batch->payload->cancel_stream.cancel_error);
    switch (cur) {
      case HAS_COMPRESSION_ALGORITHM:
      case NO_COMPRESSION_ALGORITHM:
      case INITIAL_METADATA_UNSEEN:
        break;
      default:
        if ((cur & CANCELLED_BIT) == 0) {
          grpc_transport_stream_op_batch_finish_with_failure(
              exec_ctx, (grpc_transport_stream_op_batch *)cur,
              GRPC_ERROR_REF(batch->payload->cancel_stream.cancel_error));
        } else {
          GRPC_ERROR_UNREF((grpc_error *)(cur & ~CANCELLED_BIT));
        }
        break;
    }
  }

  if (batch->send_initial_metadata) {
    bool has_compression_algorithm;
    grpc_error *error = process_send_initial_metadata(
        exec_ctx, elem,
        batch->payload->send_initial_metadata.send_initial_metadata,
        &has_compression_algorithm);
    if (error != GRPC_ERROR_NONE) {
      grpc_transport_stream_op_batch_finish_with_failure(exec_ctx, batch,
                                                         error);
      return;
    }
    gpr_atm cur;
  retry_send_im:
    cur = gpr_atm_acq_load(&calld->send_initial_metadata_state);
    GPR_ASSERT(cur != HAS_COMPRESSION_ALGORITHM &&
               cur != NO_COMPRESSION_ALGORITHM);
    if ((cur & CANCELLED_BIT) == 0) {
      if (!gpr_atm_rel_cas(&calld->send_initial_metadata_state, cur,
                           has_compression_algorithm
                               ? HAS_COMPRESSION_ALGORITHM
                               : NO_COMPRESSION_ALGORITHM)) {
        goto retry_send_im;
      }
      if (cur != INITIAL_METADATA_UNSEEN) {
        start_send_message_batch(exec_ctx, elem,
                                 (grpc_transport_stream_op_batch *)cur,
                                 has_compression_algorithm);
      }
    }
  }
  if (batch->send_message) {
    gpr_atm cur;
  retry_send:
    cur = gpr_atm_acq_load(&calld->send_initial_metadata_state);
    switch (cur) {
      case INITIAL_METADATA_UNSEEN:
        if (!gpr_atm_rel_cas(&calld->send_initial_metadata_state, cur,
                             (gpr_atm)batch)) {
          goto retry_send;
        }
        break;
      case HAS_COMPRESSION_ALGORITHM:
      case NO_COMPRESSION_ALGORITHM:
        start_send_message_batch(exec_ctx, elem, batch,
                                 cur == HAS_COMPRESSION_ALGORITHM);
        break;
      default:
        if (cur & CANCELLED_BIT) {
          grpc_transport_stream_op_batch_finish_with_failure(
              exec_ctx, batch,
              GRPC_ERROR_REF((grpc_error *)(cur & ~CANCELLED_BIT)));
        } else {
          /* >1 send_message concurrently */
          GPR_UNREACHABLE_CODE(break);
        }
    }
  } else {
    /* pass control down the stack */
    grpc_call_next_op(exec_ctx, elem, batch);
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
  GRPC_CLOSURE_INIT(&calld->on_send_message_next_done,
                    on_send_message_next_done, elem, grpc_schedule_on_exec_ctx);
  GRPC_CLOSURE_INIT(&calld->send_message_on_complete, send_message_on_complete,
                    elem, grpc_schedule_on_exec_ctx);

  return GRPC_ERROR_NONE;
}

/* Destructor for call_data */
static void destroy_call_elem(grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
                              const grpc_call_final_info *final_info,
                              grpc_closure *ignored) {
  /* grab pointers to our data from the call element */
  call_data *calld = elem->call_data;
  grpc_slice_buffer_destroy_internal(exec_ctx, &calld->slices);
  gpr_atm imstate =
      gpr_atm_no_barrier_load(&calld->send_initial_metadata_state);
  if (imstate & CANCELLED_BIT) {
    GRPC_ERROR_UNREF((grpc_error *)(imstate & ~CANCELLED_BIT));
  }
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
    "compress"};

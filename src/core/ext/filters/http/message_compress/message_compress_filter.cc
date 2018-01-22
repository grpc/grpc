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
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/transport/static_metadata.h"

namespace {
enum initial_metadata_state {
  // Initial metadata not yet seen.
  INITIAL_METADATA_UNSEEN = 0,
  // Initial metadata seen; compression algorithm set.
  HAS_COMPRESSION_ALGORITHM,
  // Initial metadata seen; no compression algorithm set.
  NO_COMPRESSION_ALGORITHM,
};

struct call_data {
  grpc_call_combiner* call_combiner;
  grpc_linked_mdelem compression_algorithm_storage;
  grpc_linked_mdelem stream_compression_algorithm_storage;
  grpc_linked_mdelem accept_encoding_storage;
  grpc_linked_mdelem accept_stream_encoding_storage;
  /** Compression algorithm we'll try to use. It may be given by incoming
   * metadata, or by the channel's default compression settings. */
  grpc_compression_algorithm compression_algorithm;
  initial_metadata_state send_initial_metadata_state;
  grpc_error* cancel_error;
  grpc_closure start_send_message_batch_in_call_combiner;
  grpc_transport_stream_op_batch* send_message_batch;
  grpc_slice_buffer slices; /**< Buffers up input slices to be compressed */
  grpc_slice_buffer_stream replacement_stream;
  grpc_closure* original_send_message_on_complete;
  grpc_closure send_message_on_complete;
  grpc_closure on_send_message_next_done;
};

struct channel_data {
  /** The default, channel-level, compression algorithm */
  grpc_compression_algorithm default_compression_algorithm;
  /** Bitset of enabled algorithms */
  uint32_t enabled_algorithms_bitset;
  /** Supported compression algorithms */
  uint32_t supported_compression_algorithms;

  /** The default, channel-level, stream compression algorithm */
  grpc_stream_compression_algorithm default_stream_compression_algorithm;
  /** Bitset of enabled stream compression algorithms */
  uint32_t enabled_stream_compression_algorithms_bitset;
  /** Supported stream compression algorithms */
  uint32_t supported_stream_compression_algorithms;
};
}  // namespace

static bool skip_compression(grpc_call_element* elem, uint32_t flags,
                             bool has_compression_algorithm) {
  call_data* calld = (call_data*)elem->call_data;
  channel_data* channeld = (channel_data*)elem->channel_data;

  if (flags & (GRPC_WRITE_NO_COMPRESS | GRPC_WRITE_INTERNAL_COMPRESS)) {
    return true;
  }
  if (has_compression_algorithm) {
    if (calld->compression_algorithm == GRPC_COMPRESS_NONE) {
      return true;
    }
    return false; /* we have an actual call-specific algorithm */
  }
  /* no per-call compression override */
  return channeld->default_compression_algorithm == GRPC_COMPRESS_NONE;
}

/** Filter initial metadata */
static grpc_error* process_send_initial_metadata(
    grpc_call_element* elem, grpc_metadata_batch* initial_metadata,
    bool* has_compression_algorithm) GRPC_MUST_USE_RESULT;
static grpc_error* process_send_initial_metadata(
    grpc_call_element* elem, grpc_metadata_batch* initial_metadata,
    bool* has_compression_algorithm) {
  call_data* calld = (call_data*)elem->call_data;
  channel_data* channeld = (channel_data*)elem->channel_data;
  *has_compression_algorithm = false;
  grpc_stream_compression_algorithm stream_compression_algorithm =
      GRPC_STREAM_COMPRESS_NONE;
  if (initial_metadata->idx.named.grpc_internal_stream_encoding_request !=
      nullptr) {
    grpc_mdelem md =
        initial_metadata->idx.named.grpc_internal_stream_encoding_request->md;
    if (!grpc_stream_compression_algorithm_parse(
            GRPC_MDVALUE(md), &stream_compression_algorithm)) {
      char* val = grpc_slice_to_c_string(GRPC_MDVALUE(md));
      gpr_log(GPR_ERROR,
              "Invalid stream compression algorithm: '%s' (unknown). Ignoring.",
              val);
      gpr_free(val);
      stream_compression_algorithm = GRPC_STREAM_COMPRESS_NONE;
    }
    if (!GPR_BITGET(channeld->enabled_stream_compression_algorithms_bitset,
                    stream_compression_algorithm)) {
      char* val = grpc_slice_to_c_string(GRPC_MDVALUE(md));
      gpr_log(
          GPR_ERROR,
          "Invalid stream compression algorithm: '%s' (previously disabled). "
          "Ignoring.",
          val);
      gpr_free(val);
      stream_compression_algorithm = GRPC_STREAM_COMPRESS_NONE;
    }
    *has_compression_algorithm = true;
    grpc_metadata_batch_remove(
        initial_metadata,
        initial_metadata->idx.named.grpc_internal_stream_encoding_request);
    /* Disable message-wise compression */
    calld->compression_algorithm = GRPC_COMPRESS_NONE;
    if (initial_metadata->idx.named.grpc_internal_encoding_request != nullptr) {
      grpc_metadata_batch_remove(
          initial_metadata,
          initial_metadata->idx.named.grpc_internal_encoding_request);
    }
  } else if (initial_metadata->idx.named.grpc_internal_encoding_request !=
             nullptr) {
    grpc_mdelem md =
        initial_metadata->idx.named.grpc_internal_encoding_request->md;
    if (!grpc_compression_algorithm_parse(GRPC_MDVALUE(md),
                                          &calld->compression_algorithm)) {
      char* val = grpc_slice_to_c_string(GRPC_MDVALUE(md));
      gpr_log(GPR_ERROR,
              "Invalid compression algorithm: '%s' (unknown). Ignoring.", val);
      gpr_free(val);
      calld->compression_algorithm = GRPC_COMPRESS_NONE;
    }
    *has_compression_algorithm = true;
    grpc_metadata_batch_remove(
        initial_metadata,
        initial_metadata->idx.named.grpc_internal_encoding_request);
  } else {
    /* If no algorithm was found in the metadata and we aren't
     * exceptionally skipping compression, fall back to the channel
     * default */
    if (channeld->default_stream_compression_algorithm !=
        GRPC_STREAM_COMPRESS_NONE) {
      stream_compression_algorithm =
          channeld->default_stream_compression_algorithm;
      calld->compression_algorithm = GRPC_COMPRESS_NONE;
    } else {
      calld->compression_algorithm = channeld->default_compression_algorithm;
    }
    *has_compression_algorithm = true;
  }

  grpc_error* error = GRPC_ERROR_NONE;
  /* hint compression algorithm */
  if (stream_compression_algorithm != GRPC_STREAM_COMPRESS_NONE) {
    error = grpc_metadata_batch_add_tail(
        initial_metadata, &calld->stream_compression_algorithm_storage,
        grpc_stream_compression_encoding_mdelem(stream_compression_algorithm));
  } else if (calld->compression_algorithm != GRPC_COMPRESS_NONE) {
    error = grpc_metadata_batch_add_tail(
        initial_metadata, &calld->compression_algorithm_storage,
        grpc_compression_encoding_mdelem(calld->compression_algorithm));
  }

  if (error != GRPC_ERROR_NONE) return error;

  /* convey supported compression algorithms */
  error = grpc_metadata_batch_add_tail(
      initial_metadata, &calld->accept_encoding_storage,
      GRPC_MDELEM_ACCEPT_ENCODING_FOR_ALGORITHMS(
          channeld->supported_compression_algorithms));

  if (error != GRPC_ERROR_NONE) return error;

  /* Do not overwrite accept-encoding header if it already presents. */
  if (!initial_metadata->idx.named.accept_encoding) {
    error = grpc_metadata_batch_add_tail(
        initial_metadata, &calld->accept_stream_encoding_storage,
        GRPC_MDELEM_ACCEPT_STREAM_ENCODING_FOR_ALGORITHMS(
            channeld->supported_stream_compression_algorithms));
  }

  return error;
}

static void send_message_on_complete(void* arg, grpc_error* error) {
  grpc_call_element* elem = (grpc_call_element*)arg;
  call_data* calld = (call_data*)elem->call_data;
  grpc_slice_buffer_reset_and_unref_internal(&calld->slices);
  GRPC_CLOSURE_RUN(calld->original_send_message_on_complete,
                   GRPC_ERROR_REF(error));
}

static void send_message_batch_continue(grpc_call_element* elem) {
  call_data* calld = (call_data*)elem->call_data;
  // Note: The call to grpc_call_next_op() results in yielding the
  // call combiner, so we need to clear calld->send_message_batch
  // before we do that.
  grpc_transport_stream_op_batch* send_message_batch =
      calld->send_message_batch;
  calld->send_message_batch = nullptr;
  grpc_call_next_op(elem, send_message_batch);
}

static void finish_send_message(grpc_call_element* elem) {
  call_data* calld = (call_data*)elem->call_data;
  // Compress the data if appropriate.
  grpc_slice_buffer tmp;
  grpc_slice_buffer_init(&tmp);
  uint32_t send_flags =
      calld->send_message_batch->payload->send_message.send_message->flags;
  bool did_compress =
      grpc_msg_compress(calld->compression_algorithm, &calld->slices, &tmp);
  if (did_compress) {
    if (grpc_compression_trace.enabled()) {
      const char* algo_name;
      const size_t before_size = calld->slices.length;
      const size_t after_size = tmp.length;
      const float savings_ratio = 1.0f - (float)after_size / (float)before_size;
      GPR_ASSERT(grpc_compression_algorithm_name(calld->compression_algorithm,
                                                 &algo_name));
      gpr_log(GPR_DEBUG,
              "Compressed[%s] %" PRIuPTR " bytes vs. %" PRIuPTR
              " bytes (%.2f%% savings)",
              algo_name, before_size, after_size, 100 * savings_ratio);
    }
    grpc_slice_buffer_swap(&calld->slices, &tmp);
    send_flags |= GRPC_WRITE_INTERNAL_COMPRESS;
  } else {
    if (grpc_compression_trace.enabled()) {
      const char* algo_name;
      GPR_ASSERT(grpc_compression_algorithm_name(calld->compression_algorithm,
                                                 &algo_name));
      gpr_log(GPR_DEBUG,
              "Algorithm '%s' enabled but decided not to compress. Input size: "
              "%" PRIuPTR,
              algo_name, calld->slices.length);
    }
  }
  grpc_slice_buffer_destroy_internal(&tmp);
  // Swap out the original byte stream with our new one and send the
  // batch down.
  grpc_byte_stream_destroy(
      calld->send_message_batch->payload->send_message.send_message);
  grpc_slice_buffer_stream_init(&calld->replacement_stream, &calld->slices,
                                send_flags);
  calld->send_message_batch->payload->send_message.send_message =
      &calld->replacement_stream.base;
  calld->original_send_message_on_complete =
      calld->send_message_batch->on_complete;
  calld->send_message_batch->on_complete = &calld->send_message_on_complete;
  send_message_batch_continue(elem);
}

static void fail_send_message_batch_in_call_combiner(void* arg,
                                                     grpc_error* error) {
  call_data* calld = (call_data*)arg;
  if (calld->send_message_batch != nullptr) {
    grpc_transport_stream_op_batch_finish_with_failure(
        calld->send_message_batch, GRPC_ERROR_REF(error), calld->call_combiner);
    calld->send_message_batch = nullptr;
  }
}

// Pulls a slice from the send_message byte stream and adds it to calld->slices.
static grpc_error* pull_slice_from_send_message(call_data* calld) {
  grpc_slice incoming_slice;
  grpc_error* error = grpc_byte_stream_pull(
      calld->send_message_batch->payload->send_message.send_message,
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
static void continue_reading_send_message(grpc_call_element* elem) {
  call_data* calld = (call_data*)elem->call_data;
  while (grpc_byte_stream_next(
      calld->send_message_batch->payload->send_message.send_message, ~(size_t)0,
      &calld->on_send_message_next_done)) {
    grpc_error* error = pull_slice_from_send_message(calld);
    if (error != GRPC_ERROR_NONE) {
      // Closure callback; does not take ownership of error.
      fail_send_message_batch_in_call_combiner(calld, error);
      GRPC_ERROR_UNREF(error);
      return;
    }
    if (calld->slices.length ==
        calld->send_message_batch->payload->send_message.send_message->length) {
      finish_send_message(elem);
      break;
    }
  }
}

// Async callback for grpc_byte_stream_next().
static void on_send_message_next_done(void* arg, grpc_error* error) {
  grpc_call_element* elem = (grpc_call_element*)arg;
  call_data* calld = (call_data*)elem->call_data;
  if (error != GRPC_ERROR_NONE) {
    // Closure callback; does not take ownership of error.
    fail_send_message_batch_in_call_combiner(calld, error);
    return;
  }
  error = pull_slice_from_send_message(calld);
  if (error != GRPC_ERROR_NONE) {
    // Closure callback; does not take ownership of error.
    fail_send_message_batch_in_call_combiner(calld, error);
    GRPC_ERROR_UNREF(error);
    return;
  }
  if (calld->slices.length ==
      calld->send_message_batch->payload->send_message.send_message->length) {
    finish_send_message(elem);
  } else {
    continue_reading_send_message(elem);
  }
}

static void start_send_message_batch(void* arg, grpc_error* unused) {
  grpc_call_element* elem = (grpc_call_element*)arg;
  call_data* calld = (call_data*)elem->call_data;
  if (skip_compression(
          elem,
          calld->send_message_batch->payload->send_message.send_message->flags,
          calld->send_initial_metadata_state == HAS_COMPRESSION_ALGORITHM)) {
    send_message_batch_continue(elem);
  } else {
    continue_reading_send_message(elem);
  }
}

static void compress_start_transport_stream_op_batch(
    grpc_call_element* elem, grpc_transport_stream_op_batch* batch) {
  call_data* calld = (call_data*)elem->call_data;
  GPR_TIMER_BEGIN("compress_start_transport_stream_op_batch", 0);
  // Handle cancel_stream.
  if (batch->cancel_stream) {
    GRPC_ERROR_UNREF(calld->cancel_error);
    calld->cancel_error =
        GRPC_ERROR_REF(batch->payload->cancel_stream.cancel_error);
    if (calld->send_message_batch != nullptr) {
      if (calld->send_initial_metadata_state == INITIAL_METADATA_UNSEEN) {
        GRPC_CALL_COMBINER_START(
            calld->call_combiner,
            GRPC_CLOSURE_CREATE(fail_send_message_batch_in_call_combiner, calld,
                                grpc_schedule_on_exec_ctx),
            GRPC_ERROR_REF(calld->cancel_error), "failing send_message op");
      } else {
        grpc_byte_stream_shutdown(
            calld->send_message_batch->payload->send_message.send_message,
            GRPC_ERROR_REF(calld->cancel_error));
      }
    }
  } else if (calld->cancel_error != GRPC_ERROR_NONE) {
    grpc_transport_stream_op_batch_finish_with_failure(
        batch, GRPC_ERROR_REF(calld->cancel_error), calld->call_combiner);
    goto done;
  }
  // Handle send_initial_metadata.
  if (batch->send_initial_metadata) {
    GPR_ASSERT(calld->send_initial_metadata_state == INITIAL_METADATA_UNSEEN);
    bool has_compression_algorithm;
    grpc_error* error = process_send_initial_metadata(
        elem, batch->payload->send_initial_metadata.send_initial_metadata,
        &has_compression_algorithm);
    if (error != GRPC_ERROR_NONE) {
      grpc_transport_stream_op_batch_finish_with_failure(batch, error,
                                                         calld->call_combiner);
      goto done;
    }
    calld->send_initial_metadata_state = has_compression_algorithm
                                             ? HAS_COMPRESSION_ALGORITHM
                                             : NO_COMPRESSION_ALGORITHM;
    // If we had previously received a batch containing a send_message op,
    // handle it now.  Note that we need to re-enter the call combiner
    // for this, since we can't send two batches down while holding the
    // call combiner, since the connected_channel filter (at the bottom of
    // the call stack) will release the call combiner for each batch it sees.
    if (calld->send_message_batch != nullptr) {
      GRPC_CALL_COMBINER_START(
          calld->call_combiner,
          &calld->start_send_message_batch_in_call_combiner, GRPC_ERROR_NONE,
          "starting send_message after send_initial_metadata");
    }
  }
  // Handle send_message.
  if (batch->send_message) {
    GPR_ASSERT(calld->send_message_batch == nullptr);
    calld->send_message_batch = batch;
    // If we have not yet seen send_initial_metadata, then we have to
    // wait.  We save the batch in calld and then drop the call
    // combiner, which we'll have to pick up again later when we get
    // send_initial_metadata.
    if (calld->send_initial_metadata_state == INITIAL_METADATA_UNSEEN) {
      GRPC_CALL_COMBINER_STOP(
          calld->call_combiner,
          "send_message batch pending send_initial_metadata");
      goto done;
    }
    start_send_message_batch(elem, GRPC_ERROR_NONE);
  } else {
    // Pass control down the stack.
    grpc_call_next_op(elem, batch);
  }
done:
  GPR_TIMER_END("compress_start_transport_stream_op_batch", 0);
}

/* Constructor for call_data */
static grpc_error* init_call_elem(grpc_call_element* elem,
                                  const grpc_call_element_args* args) {
  call_data* calld = (call_data*)elem->call_data;
  calld->call_combiner = args->call_combiner;
  calld->cancel_error = GRPC_ERROR_NONE;
  grpc_slice_buffer_init(&calld->slices);
  GRPC_CLOSURE_INIT(&calld->start_send_message_batch_in_call_combiner,
                    start_send_message_batch, elem, grpc_schedule_on_exec_ctx);
  GRPC_CLOSURE_INIT(&calld->on_send_message_next_done,
                    on_send_message_next_done, elem, grpc_schedule_on_exec_ctx);
  GRPC_CLOSURE_INIT(&calld->send_message_on_complete, send_message_on_complete,
                    elem, grpc_schedule_on_exec_ctx);
  return GRPC_ERROR_NONE;
}

/* Destructor for call_data */
static void destroy_call_elem(grpc_call_element* elem,
                              const grpc_call_final_info* final_info,
                              grpc_closure* ignored) {
  call_data* calld = (call_data*)elem->call_data;
  grpc_slice_buffer_destroy_internal(&calld->slices);
  GRPC_ERROR_UNREF(calld->cancel_error);
}

/* Constructor for channel_data */
static grpc_error* init_channel_elem(grpc_channel_element* elem,
                                     grpc_channel_element_args* args) {
  channel_data* channeld = (channel_data*)elem->channel_data;

  /* Configuration for message compression */
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

  channeld->supported_compression_algorithms =
      (((1u << GRPC_COMPRESS_ALGORITHMS_COUNT) - 1) &
       channeld->enabled_algorithms_bitset) |
      1u;

  /* Configuration for stream compression */
  channeld->enabled_stream_compression_algorithms_bitset =
      grpc_channel_args_stream_compression_algorithm_get_states(
          args->channel_args);

  channeld->default_stream_compression_algorithm =
      grpc_channel_args_get_stream_compression_algorithm(args->channel_args);

  if (!GPR_BITGET(channeld->enabled_stream_compression_algorithms_bitset,
                  channeld->default_stream_compression_algorithm)) {
    gpr_log(GPR_DEBUG,
            "stream compression algorithm %d not enabled: switching to none",
            channeld->default_stream_compression_algorithm);
    channeld->default_stream_compression_algorithm = GRPC_STREAM_COMPRESS_NONE;
  }

  channeld->supported_stream_compression_algorithms =
      (((1u << GRPC_STREAM_COMPRESS_ALGORITHMS_COUNT) - 1) &
       channeld->enabled_stream_compression_algorithms_bitset) |
      1u;

  GPR_ASSERT(!args->is_last);
  return GRPC_ERROR_NONE;
}

/* Destructor for channel data */
static void destroy_channel_elem(grpc_channel_element* elem) {}

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
    grpc_channel_next_get_info,
    "message_compress"};

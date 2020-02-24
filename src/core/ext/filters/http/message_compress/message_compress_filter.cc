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

#include <grpc/support/port_platform.h>

#include <assert.h>
#include <string.h>

#include <grpc/compression.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/ext/filters/http/message_compress/message_compress_filter.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/compression/algorithm_metadata.h"
#include "src/core/lib/compression/compression_args.h"
#include "src/core/lib/compression/compression_internal.h"
#include "src/core/lib/compression/message_compress.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/manual_constructor.h"
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/transport/static_metadata.h"

static void start_send_message_batch(void* arg, grpc_error* unused);
static void send_message_on_complete(void* arg, grpc_error* error);
static void on_send_message_next_done(void* arg, grpc_error* error);

namespace {

struct channel_data {
  /** The default, channel-level, compression algorithm */
  grpc_compression_algorithm default_compression_algorithm;
  /** Bitset of enabled compression algorithms */
  uint32_t enabled_compression_algorithms_bitset;
  /** Bitset of enabled message compression algorithms */
  uint32_t enabled_message_compression_algorithms_bitset;
  /** Bitset of enabled stream compression algorithms */
  uint32_t enabled_stream_compression_algorithms_bitset;
};

struct call_data {
  call_data(grpc_call_element* elem, const grpc_call_element_args& args)
      : call_combiner(args.call_combiner) {
    channel_data* channeld = static_cast<channel_data*>(elem->channel_data);
    // The call's message compression algorithm is set to channel's default
    // setting. It can be overridden later by initial metadata.
    if (GPR_LIKELY(GPR_BITGET(channeld->enabled_compression_algorithms_bitset,
                              channeld->default_compression_algorithm))) {
      message_compression_algorithm =
          grpc_compression_algorithm_to_message_compression_algorithm(
              channeld->default_compression_algorithm);
    }
    GRPC_CLOSURE_INIT(&start_send_message_batch_in_call_combiner,
                      start_send_message_batch, elem,
                      grpc_schedule_on_exec_ctx);
  }

  ~call_data() {
    if (state_initialized) {
      grpc_slice_buffer_destroy_internal(&slices);
    }
    GRPC_ERROR_UNREF(cancel_error);
  }

  grpc_core::CallCombiner* call_combiner;
  grpc_message_compression_algorithm message_compression_algorithm =
      GRPC_MESSAGE_COMPRESS_NONE;
  grpc_error* cancel_error = GRPC_ERROR_NONE;
  grpc_transport_stream_op_batch* send_message_batch = nullptr;
  bool seen_initial_metadata = false;
  /* Set to true, if the fields below are initialized. */
  bool state_initialized = false;
  grpc_closure start_send_message_batch_in_call_combiner;
  /* The fields below are only initialized when we compress the payload.
   * Keep them at the bottom of the struct, so they don't pollute the
   * cache-lines. */
  grpc_linked_mdelem message_compression_algorithm_storage;
  grpc_linked_mdelem stream_compression_algorithm_storage;
  grpc_linked_mdelem accept_encoding_storage;
  grpc_linked_mdelem accept_stream_encoding_storage;
  grpc_slice_buffer slices; /**< Buffers up input slices to be compressed */
  grpc_core::ManualConstructor<grpc_core::SliceBufferByteStream>
      replacement_stream;
  grpc_closure* original_send_message_on_complete;
  grpc_closure send_message_on_complete;
  grpc_closure on_send_message_next_done;
};

}  // namespace

// Returns true if we should skip message compression for the current message.
static bool skip_message_compression(grpc_call_element* elem) {
  call_data* calld = static_cast<call_data*>(elem->call_data);
  // If the flags of this message indicate that it shouldn't be compressed, we
  // skip message compression.
  uint32_t flags =
      calld->send_message_batch->payload->send_message.send_message->flags();
  if (flags & (GRPC_WRITE_NO_COMPRESS | GRPC_WRITE_INTERNAL_COMPRESS)) {
    return true;
  }
  // If this call doesn't have any message compression algorithm set, skip
  // message compression.
  return calld->message_compression_algorithm == GRPC_MESSAGE_COMPRESS_NONE;
}

// Determines the compression algorithm from the initial metadata and the
// channel's default setting.
static grpc_compression_algorithm find_compression_algorithm(
    grpc_metadata_batch* initial_metadata, channel_data* channeld) {
  if (initial_metadata->idx.named.grpc_internal_encoding_request == nullptr) {
    return channeld->default_compression_algorithm;
  }
  grpc_compression_algorithm compression_algorithm;
  // Parse the compression algorithm from the initial metadata.
  grpc_mdelem md =
      initial_metadata->idx.named.grpc_internal_encoding_request->md;
  GPR_ASSERT(grpc_compression_algorithm_parse(GRPC_MDVALUE(md),
                                              &compression_algorithm));
  // Remove this metadata since it's an internal one (i.e., it won't be
  // transmitted out).
  grpc_metadata_batch_remove(initial_metadata,
                             GRPC_BATCH_GRPC_INTERNAL_ENCODING_REQUEST);
  // Check if that algorithm is enabled. Note that GRPC_COMPRESS_NONE is always
  // enabled.
  // TODO(juanlishen): Maybe use channel default or abort() if the algorithm
  // from the initial metadata is disabled.
  if (GPR_LIKELY(GPR_BITGET(channeld->enabled_compression_algorithms_bitset,
                            compression_algorithm))) {
    return compression_algorithm;
  }
  const char* algorithm_name;
  GPR_ASSERT(
      grpc_compression_algorithm_name(compression_algorithm, &algorithm_name));
  gpr_log(GPR_ERROR,
          "Invalid compression algorithm from initial metadata: '%s' "
          "(previously disabled). "
          "Will not compress.",
          algorithm_name);
  return GRPC_COMPRESS_NONE;
}

static void initialize_state(grpc_call_element* elem, call_data* calld) {
  GPR_DEBUG_ASSERT(!calld->state_initialized);
  calld->state_initialized = true;
  grpc_slice_buffer_init(&calld->slices);
  GRPC_CLOSURE_INIT(&calld->send_message_on_complete,
                    ::send_message_on_complete, elem,
                    grpc_schedule_on_exec_ctx);
  GRPC_CLOSURE_INIT(&calld->on_send_message_next_done,
                    ::on_send_message_next_done, elem,
                    grpc_schedule_on_exec_ctx);
}

static grpc_error* process_send_initial_metadata(
    grpc_call_element* elem,
    grpc_metadata_batch* initial_metadata) GRPC_MUST_USE_RESULT;
static grpc_error* process_send_initial_metadata(
    grpc_call_element* elem, grpc_metadata_batch* initial_metadata) {
  call_data* calld = static_cast<call_data*>(elem->call_data);
  channel_data* channeld = static_cast<channel_data*>(elem->channel_data);
  // Find the compression algorithm.
  grpc_compression_algorithm compression_algorithm =
      find_compression_algorithm(initial_metadata, channeld);
  // Note that at most one of the following algorithms can be set.
  calld->message_compression_algorithm =
      grpc_compression_algorithm_to_message_compression_algorithm(
          compression_algorithm);
  grpc_stream_compression_algorithm stream_compression_algorithm =
      grpc_compression_algorithm_to_stream_compression_algorithm(
          compression_algorithm);
  // Hint compression algorithm.
  grpc_error* error = GRPC_ERROR_NONE;
  if (calld->message_compression_algorithm != GRPC_MESSAGE_COMPRESS_NONE) {
    initialize_state(elem, calld);
    error = grpc_metadata_batch_add_tail(
        initial_metadata, &calld->message_compression_algorithm_storage,
        grpc_message_compression_encoding_mdelem(
            calld->message_compression_algorithm),
        GRPC_BATCH_GRPC_ENCODING);
  } else if (stream_compression_algorithm != GRPC_STREAM_COMPRESS_NONE) {
    initialize_state(elem, calld);
    error = grpc_metadata_batch_add_tail(
        initial_metadata, &calld->stream_compression_algorithm_storage,
        grpc_stream_compression_encoding_mdelem(stream_compression_algorithm),
        GRPC_BATCH_CONTENT_ENCODING);
  }
  if (error != GRPC_ERROR_NONE) return error;
  // Convey supported compression algorithms.
  error = grpc_metadata_batch_add_tail(
      initial_metadata, &calld->accept_encoding_storage,
      GRPC_MDELEM_ACCEPT_ENCODING_FOR_ALGORITHMS(
          channeld->enabled_message_compression_algorithms_bitset),
      GRPC_BATCH_GRPC_ACCEPT_ENCODING);
  if (error != GRPC_ERROR_NONE) return error;
  // Do not overwrite accept-encoding header if it already presents (e.g., added
  // by some proxy).
  if (!initial_metadata->idx.named.accept_encoding) {
    error = grpc_metadata_batch_add_tail(
        initial_metadata, &calld->accept_stream_encoding_storage,
        GRPC_MDELEM_ACCEPT_STREAM_ENCODING_FOR_ALGORITHMS(
            channeld->enabled_stream_compression_algorithms_bitset),
        GRPC_BATCH_ACCEPT_ENCODING);
  }
  return error;
}

static void send_message_on_complete(void* arg, grpc_error* error) {
  grpc_call_element* elem = static_cast<grpc_call_element*>(arg);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  grpc_slice_buffer_reset_and_unref_internal(&calld->slices);
  grpc_core::Closure::Run(DEBUG_LOCATION,
                          calld->original_send_message_on_complete,
                          GRPC_ERROR_REF(error));
}

static void send_message_batch_continue(grpc_call_element* elem) {
  call_data* calld = static_cast<call_data*>(elem->call_data);
  // Note: The call to grpc_call_next_op() results in yielding the
  // call combiner, so we need to clear calld->send_message_batch
  // before we do that.
  grpc_transport_stream_op_batch* send_message_batch =
      calld->send_message_batch;
  calld->send_message_batch = nullptr;
  grpc_call_next_op(elem, send_message_batch);
}

static void finish_send_message(grpc_call_element* elem) {
  call_data* calld = static_cast<call_data*>(elem->call_data);
  GPR_DEBUG_ASSERT(calld->message_compression_algorithm !=
                   GRPC_MESSAGE_COMPRESS_NONE);
  // Compress the data if appropriate.
  grpc_slice_buffer tmp;
  grpc_slice_buffer_init(&tmp);
  uint32_t send_flags =
      calld->send_message_batch->payload->send_message.send_message->flags();
  bool did_compress = grpc_msg_compress(calld->message_compression_algorithm,
                                        &calld->slices, &tmp);
  if (did_compress) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_compression_trace)) {
      const char* algo_name;
      const size_t before_size = calld->slices.length;
      const size_t after_size = tmp.length;
      const float savings_ratio = 1.0f - static_cast<float>(after_size) /
                                             static_cast<float>(before_size);
      GPR_ASSERT(grpc_message_compression_algorithm_name(
          calld->message_compression_algorithm, &algo_name));
      gpr_log(GPR_INFO,
              "Compressed[%s] %" PRIuPTR " bytes vs. %" PRIuPTR
              " bytes (%.2f%% savings)",
              algo_name, before_size, after_size, 100 * savings_ratio);
    }
    grpc_slice_buffer_swap(&calld->slices, &tmp);
    send_flags |= GRPC_WRITE_INTERNAL_COMPRESS;
  } else {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_compression_trace)) {
      const char* algo_name;
      GPR_ASSERT(grpc_message_compression_algorithm_name(
          calld->message_compression_algorithm, &algo_name));
      gpr_log(GPR_INFO,
              "Algorithm '%s' enabled but decided not to compress. Input size: "
              "%" PRIuPTR,
              algo_name, calld->slices.length);
    }
  }
  grpc_slice_buffer_destroy_internal(&tmp);
  // Swap out the original byte stream with our new one and send the
  // batch down.
  calld->replacement_stream.Init(&calld->slices, send_flags);
  calld->send_message_batch->payload->send_message.send_message.reset(
      calld->replacement_stream.get());
  calld->original_send_message_on_complete =
      calld->send_message_batch->on_complete;
  calld->send_message_batch->on_complete = &calld->send_message_on_complete;
  send_message_batch_continue(elem);
}

static void fail_send_message_batch_in_call_combiner(void* arg,
                                                     grpc_error* error) {
  call_data* calld = static_cast<call_data*>(arg);
  if (calld->send_message_batch != nullptr) {
    grpc_transport_stream_op_batch_finish_with_failure(
        calld->send_message_batch, GRPC_ERROR_REF(error), calld->call_combiner);
    calld->send_message_batch = nullptr;
  }
}

// Pulls a slice from the send_message byte stream and adds it to calld->slices.
static grpc_error* pull_slice_from_send_message(call_data* calld) {
  grpc_slice incoming_slice;
  grpc_error* error =
      calld->send_message_batch->payload->send_message.send_message->Pull(
          &incoming_slice);
  if (error == GRPC_ERROR_NONE) {
    grpc_slice_buffer_add(&calld->slices, incoming_slice);
  }
  return error;
}

// Reads as many slices as possible from the send_message byte stream.
// If all data has been read, invokes finish_send_message().  Otherwise,
// an async call to ByteStream::Next() has been started, which will
// eventually result in calling on_send_message_next_done().
static void continue_reading_send_message(grpc_call_element* elem) {
  call_data* calld = static_cast<call_data*>(elem->call_data);
  if (calld->slices.length ==
      calld->send_message_batch->payload->send_message.send_message->length()) {
    finish_send_message(elem);
    return;
  }
  while (calld->send_message_batch->payload->send_message.send_message->Next(
      ~static_cast<size_t>(0), &calld->on_send_message_next_done)) {
    grpc_error* error = pull_slice_from_send_message(calld);
    if (error != GRPC_ERROR_NONE) {
      // Closure callback; does not take ownership of error.
      fail_send_message_batch_in_call_combiner(calld, error);
      GRPC_ERROR_UNREF(error);
      return;
    }
    if (calld->slices.length == calld->send_message_batch->payload->send_message
                                    .send_message->length()) {
      finish_send_message(elem);
      break;
    }
  }
}

// Async callback for ByteStream::Next().
static void on_send_message_next_done(void* arg, grpc_error* error) {
  grpc_call_element* elem = static_cast<grpc_call_element*>(arg);
  call_data* calld = static_cast<call_data*>(elem->call_data);
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
      calld->send_message_batch->payload->send_message.send_message->length()) {
    finish_send_message(elem);
  } else {
    continue_reading_send_message(elem);
  }
}

static void start_send_message_batch(void* arg, grpc_error* /*unused*/) {
  grpc_call_element* elem = static_cast<grpc_call_element*>(arg);
  if (skip_message_compression(elem)) {
    send_message_batch_continue(elem);
  } else {
    continue_reading_send_message(elem);
  }
}

static void compress_start_transport_stream_op_batch(
    grpc_call_element* elem, grpc_transport_stream_op_batch* batch) {
  GPR_TIMER_SCOPE("compress_start_transport_stream_op_batch", 0);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  // Handle cancel_stream.
  if (batch->cancel_stream) {
    GRPC_ERROR_UNREF(calld->cancel_error);
    calld->cancel_error =
        GRPC_ERROR_REF(batch->payload->cancel_stream.cancel_error);
    if (calld->send_message_batch != nullptr) {
      if (!calld->seen_initial_metadata) {
        GRPC_CALL_COMBINER_START(
            calld->call_combiner,
            GRPC_CLOSURE_CREATE(fail_send_message_batch_in_call_combiner, calld,
                                grpc_schedule_on_exec_ctx),
            GRPC_ERROR_REF(calld->cancel_error), "failing send_message op");
      } else {
        calld->send_message_batch->payload->send_message.send_message->Shutdown(
            GRPC_ERROR_REF(calld->cancel_error));
      }
    }
  } else if (calld->cancel_error != GRPC_ERROR_NONE) {
    grpc_transport_stream_op_batch_finish_with_failure(
        batch, GRPC_ERROR_REF(calld->cancel_error), calld->call_combiner);
    return;
  }
  // Handle send_initial_metadata.
  if (batch->send_initial_metadata) {
    GPR_ASSERT(!calld->seen_initial_metadata);
    grpc_error* error = process_send_initial_metadata(
        elem, batch->payload->send_initial_metadata.send_initial_metadata);
    if (error != GRPC_ERROR_NONE) {
      grpc_transport_stream_op_batch_finish_with_failure(batch, error,
                                                         calld->call_combiner);
      return;
    }
    calld->seen_initial_metadata = true;
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
    if (!calld->seen_initial_metadata) {
      GRPC_CALL_COMBINER_STOP(
          calld->call_combiner,
          "send_message batch pending send_initial_metadata");
      return;
    }
    start_send_message_batch(elem, GRPC_ERROR_NONE);
  } else {
    // Pass control down the stack.
    grpc_call_next_op(elem, batch);
  }
}

/* Constructor for call_data */
static grpc_error* compress_init_call_elem(grpc_call_element* elem,
                                           const grpc_call_element_args* args) {
  new (elem->call_data) call_data(elem, *args);
  return GRPC_ERROR_NONE;
}

/* Destructor for call_data */
static void compress_destroy_call_elem(
    grpc_call_element* elem, const grpc_call_final_info* /*final_info*/,
    grpc_closure* /*ignored*/) {
  call_data* calld = static_cast<call_data*>(elem->call_data);
  calld->~call_data();
}

/* Constructor for channel_data */
static grpc_error* compress_init_channel_elem(grpc_channel_element* elem,
                                              grpc_channel_element_args* args) {
  channel_data* channeld = static_cast<channel_data*>(elem->channel_data);
  // Get the enabled and the default algorithms from channel args.
  channeld->enabled_compression_algorithms_bitset =
      grpc_channel_args_compression_algorithm_get_states(args->channel_args);
  channeld->default_compression_algorithm =
      grpc_channel_args_get_channel_default_compression_algorithm(
          args->channel_args);
  // Make sure the default is enabled.
  if (!GPR_BITGET(channeld->enabled_compression_algorithms_bitset,
                  channeld->default_compression_algorithm)) {
    const char* name;
    GPR_ASSERT(grpc_compression_algorithm_name(
                   channeld->default_compression_algorithm, &name) == 1);
    gpr_log(GPR_ERROR,
            "default compression algorithm %s not enabled: switching to none",
            name);
    channeld->default_compression_algorithm = GRPC_COMPRESS_NONE;
  }
  channeld->enabled_message_compression_algorithms_bitset =
      grpc_compression_bitset_to_message_bitset(
          channeld->enabled_compression_algorithms_bitset);
  channeld->enabled_stream_compression_algorithms_bitset =
      grpc_compression_bitset_to_stream_bitset(
          channeld->enabled_compression_algorithms_bitset);
  GPR_ASSERT(!args->is_last);
  return GRPC_ERROR_NONE;
}

/* Destructor for channel data */
static void compress_destroy_channel_elem(grpc_channel_element* /*elem*/) {}

const grpc_channel_filter grpc_message_compress_filter = {
    compress_start_transport_stream_op_batch,
    grpc_channel_next_op,
    sizeof(call_data),
    compress_init_call_elem,
    grpc_call_stack_ignore_set_pollset_or_pollset_set,
    compress_destroy_call_elem,
    sizeof(channel_data),
    compress_init_channel_elem,
    compress_destroy_channel_elem,
    grpc_channel_next_get_info,
    "message_compress"};

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

#include "absl/types/optional.h"

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

namespace {

class ChannelData {
 public:
  explicit ChannelData(grpc_channel_element_args* args) {
    // Get the enabled and the default algorithms from channel args.
    enabled_compression_algorithms_bitset_ =
        grpc_channel_args_compression_algorithm_get_states(args->channel_args);
    default_compression_algorithm_ =
        grpc_channel_args_get_channel_default_compression_algorithm(
            args->channel_args);
    // Make sure the default is enabled.
    if (!GPR_BITGET(enabled_compression_algorithms_bitset_,
                    default_compression_algorithm_)) {
      const char* name;
      GPR_ASSERT(grpc_compression_algorithm_name(default_compression_algorithm_,
                                                 &name) == 1);
      gpr_log(GPR_ERROR,
              "default compression algorithm %s not enabled: switching to none",
              name);
      default_compression_algorithm_ = GRPC_COMPRESS_NONE;
    }
    enabled_message_compression_algorithms_bitset_ =
        grpc_compression_bitset_to_message_bitset(
            enabled_compression_algorithms_bitset_);
    enabled_stream_compression_algorithms_bitset_ =
        grpc_compression_bitset_to_stream_bitset(
            enabled_compression_algorithms_bitset_);
    GPR_ASSERT(!args->is_last);
  }

  grpc_compression_algorithm default_compression_algorithm() const {
    return default_compression_algorithm_;
  }

  uint32_t enabled_compression_algorithms_bitset() const {
    return enabled_compression_algorithms_bitset_;
  }

  uint32_t enabled_message_compression_algorithms_bitset() const {
    return enabled_message_compression_algorithms_bitset_;
  }

  uint32_t enabled_stream_compression_algorithms_bitset() const {
    return enabled_stream_compression_algorithms_bitset_;
  }

 private:
  /** The default, channel-level, compression algorithm */
  grpc_compression_algorithm default_compression_algorithm_;
  /** Bitset of enabled compression algorithms */
  uint32_t enabled_compression_algorithms_bitset_;
  /** Bitset of enabled message compression algorithms */
  uint32_t enabled_message_compression_algorithms_bitset_;
  /** Bitset of enabled stream compression algorithms */
  uint32_t enabled_stream_compression_algorithms_bitset_;
};

class CallData {
 public:
  CallData(grpc_call_element* elem, const grpc_call_element_args& args)
      : call_combiner_(args.call_combiner) {
    ChannelData* channeld = static_cast<ChannelData*>(elem->channel_data);
    // The call's message compression algorithm is set to channel's default
    // setting. It can be overridden later by initial metadata.
    if (GPR_LIKELY(GPR_BITGET(channeld->enabled_compression_algorithms_bitset(),
                              channeld->default_compression_algorithm()))) {
      message_compression_algorithm_ =
          grpc_compression_algorithm_to_message_compression_algorithm(
              channeld->default_compression_algorithm());
    }
    GRPC_CLOSURE_INIT(&start_send_message_batch_in_call_combiner_,
                      StartSendMessageBatch, elem, grpc_schedule_on_exec_ctx);
  }

  ~CallData() {
    if (state_initialized_) {
      grpc_slice_buffer_destroy_internal(&slices_);
    }
    GRPC_ERROR_UNREF(cancel_error_);
  }

  void CompressStartTransportStreamOpBatch(
      grpc_call_element* elem, grpc_transport_stream_op_batch* batch);

 private:
  bool SkipMessageCompression();
  void InitializeState(grpc_call_element* elem);

  grpc_error* ProcessSendInitialMetadata(grpc_call_element* elem,
                                         grpc_metadata_batch* initial_metadata);

  // Methods for processing a send_message batch
  static void StartSendMessageBatch(void* elem_arg, grpc_error* unused);
  static void OnSendMessageNextDone(void* elem_arg, grpc_error* error);
  grpc_error* PullSliceFromSendMessage();
  void ContinueReadingSendMessage(grpc_call_element* elem);
  void FinishSendMessage(grpc_call_element* elem);
  void SendMessageBatchContinue(grpc_call_element* elem);
  static void FailSendMessageBatchInCallCombiner(void* calld_arg,
                                                 grpc_error* error);

  static void SendMessageOnComplete(void* calld_arg, grpc_error* error);

  grpc_core::CallCombiner* call_combiner_;
  grpc_message_compression_algorithm message_compression_algorithm_ =
      GRPC_MESSAGE_COMPRESS_NONE;
  grpc_error* cancel_error_ = GRPC_ERROR_NONE;
  grpc_transport_stream_op_batch* send_message_batch_ = nullptr;
  bool seen_initial_metadata_ = false;
  /* Set to true, if the fields below are initialized. */
  bool state_initialized_ = false;
  grpc_closure start_send_message_batch_in_call_combiner_;
  /* The fields below are only initialized when we compress the payload.
   * Keep them at the bottom of the struct, so they don't pollute the
   * cache-lines. */
  grpc_linked_mdelem message_compression_algorithm_storage_;
  grpc_linked_mdelem stream_compression_algorithm_storage_;
  grpc_linked_mdelem accept_encoding_storage_;
  grpc_linked_mdelem accept_stream_encoding_storage_;
  grpc_slice_buffer slices_; /**< Buffers up input slices to be compressed */
  // Allocate space for the replacement stream
  std::aligned_storage<sizeof(grpc_core::SliceBufferByteStream),
                       alignof(grpc_core::SliceBufferByteStream)>::type
      replacement_stream_;
  grpc_closure* original_send_message_on_complete_ = nullptr;
  grpc_closure send_message_on_complete_;
  grpc_closure on_send_message_next_done_;
};

// Returns true if we should skip message compression for the current message.
bool CallData::SkipMessageCompression() {
  // If the flags of this message indicate that it shouldn't be compressed, we
  // skip message compression.
  uint32_t flags =
      send_message_batch_->payload->send_message.send_message->flags();
  if (flags & (GRPC_WRITE_NO_COMPRESS | GRPC_WRITE_INTERNAL_COMPRESS)) {
    return true;
  }
  // If this call doesn't have any message compression algorithm set, skip
  // message compression.
  return message_compression_algorithm_ == GRPC_MESSAGE_COMPRESS_NONE;
}

// Determines the compression algorithm from the initial metadata and the
// channel's default setting.
grpc_compression_algorithm FindCompressionAlgorithm(
    grpc_metadata_batch* initial_metadata, ChannelData* channeld) {
  if (initial_metadata->idx.named.grpc_internal_encoding_request == nullptr) {
    return channeld->default_compression_algorithm();
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
  if (GPR_LIKELY(GPR_BITGET(channeld->enabled_compression_algorithms_bitset(),
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

void CallData::InitializeState(grpc_call_element* elem) {
  GPR_DEBUG_ASSERT(!state_initialized_);
  state_initialized_ = true;
  grpc_slice_buffer_init(&slices_);
  GRPC_CLOSURE_INIT(&send_message_on_complete_, SendMessageOnComplete, this,
                    grpc_schedule_on_exec_ctx);
  GRPC_CLOSURE_INIT(&on_send_message_next_done_, OnSendMessageNextDone, elem,
                    grpc_schedule_on_exec_ctx);
}

grpc_error* CallData::ProcessSendInitialMetadata(
    grpc_call_element* elem, grpc_metadata_batch* initial_metadata) {
  ChannelData* channeld = static_cast<ChannelData*>(elem->channel_data);
  // Find the compression algorithm.
  grpc_compression_algorithm compression_algorithm =
      FindCompressionAlgorithm(initial_metadata, channeld);
  // Note that at most one of the following algorithms can be set.
  message_compression_algorithm_ =
      grpc_compression_algorithm_to_message_compression_algorithm(
          compression_algorithm);
  grpc_stream_compression_algorithm stream_compression_algorithm =
      grpc_compression_algorithm_to_stream_compression_algorithm(
          compression_algorithm);
  // Hint compression algorithm.
  grpc_error* error = GRPC_ERROR_NONE;
  if (message_compression_algorithm_ != GRPC_MESSAGE_COMPRESS_NONE) {
    InitializeState(elem);
    error = grpc_metadata_batch_add_tail(
        initial_metadata, &message_compression_algorithm_storage_,
        grpc_message_compression_encoding_mdelem(
            message_compression_algorithm_),
        GRPC_BATCH_GRPC_ENCODING);
  } else if (stream_compression_algorithm != GRPC_STREAM_COMPRESS_NONE) {
    InitializeState(elem);
    error = grpc_metadata_batch_add_tail(
        initial_metadata, &stream_compression_algorithm_storage_,
        grpc_stream_compression_encoding_mdelem(stream_compression_algorithm),
        GRPC_BATCH_CONTENT_ENCODING);
  }
  if (error != GRPC_ERROR_NONE) return error;
  // Convey supported compression algorithms.
  error = grpc_metadata_batch_add_tail(
      initial_metadata, &accept_encoding_storage_,
      GRPC_MDELEM_ACCEPT_ENCODING_FOR_ALGORITHMS(
          channeld->enabled_message_compression_algorithms_bitset()),
      GRPC_BATCH_GRPC_ACCEPT_ENCODING);
  if (error != GRPC_ERROR_NONE) return error;
  // Do not overwrite accept-encoding header if it already presents (e.g., added
  // by some proxy).
  if (!initial_metadata->idx.named.accept_encoding) {
    error = grpc_metadata_batch_add_tail(
        initial_metadata, &accept_stream_encoding_storage_,
        GRPC_MDELEM_ACCEPT_STREAM_ENCODING_FOR_ALGORITHMS(
            channeld->enabled_stream_compression_algorithms_bitset()),
        GRPC_BATCH_ACCEPT_ENCODING);
  }
  return error;
}

void CallData::SendMessageOnComplete(void* calld_arg, grpc_error* error) {
  CallData* calld = static_cast<CallData*>(calld_arg);
  grpc_slice_buffer_reset_and_unref_internal(&calld->slices_);
  grpc_core::Closure::Run(DEBUG_LOCATION,
                          calld->original_send_message_on_complete_,
                          GRPC_ERROR_REF(error));
}

void CallData::SendMessageBatchContinue(grpc_call_element* elem) {
  // Note: The call to grpc_call_next_op() results in yielding the
  // call combiner, so we need to clear send_message_batch_ before we do that.
  grpc_transport_stream_op_batch* send_message_batch = send_message_batch_;
  send_message_batch_ = nullptr;
  grpc_call_next_op(elem, send_message_batch);
}

void CallData::FinishSendMessage(grpc_call_element* elem) {
  GPR_DEBUG_ASSERT(message_compression_algorithm_ !=
                   GRPC_MESSAGE_COMPRESS_NONE);
  // Compress the data if appropriate.
  grpc_slice_buffer tmp;
  grpc_slice_buffer_init(&tmp);
  uint32_t send_flags =
      send_message_batch_->payload->send_message.send_message->flags();
  bool did_compress =
      grpc_msg_compress(message_compression_algorithm_, &slices_, &tmp);
  if (did_compress) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_compression_trace)) {
      const char* algo_name;
      const size_t before_size = slices_.length;
      const size_t after_size = tmp.length;
      const float savings_ratio = 1.0f - static_cast<float>(after_size) /
                                             static_cast<float>(before_size);
      GPR_ASSERT(grpc_message_compression_algorithm_name(
          message_compression_algorithm_, &algo_name));
      gpr_log(GPR_INFO,
              "Compressed[%s] %" PRIuPTR " bytes vs. %" PRIuPTR
              " bytes (%.2f%% savings)",
              algo_name, before_size, after_size, 100 * savings_ratio);
    }
    grpc_slice_buffer_swap(&slices_, &tmp);
    send_flags |= GRPC_WRITE_INTERNAL_COMPRESS;
  } else {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_compression_trace)) {
      const char* algo_name;
      GPR_ASSERT(grpc_message_compression_algorithm_name(
          message_compression_algorithm_, &algo_name));
      gpr_log(GPR_INFO,
              "Algorithm '%s' enabled but decided not to compress. Input size: "
              "%" PRIuPTR,
              algo_name, slices_.length);
    }
  }
  grpc_slice_buffer_destroy_internal(&tmp);
  // Swap out the original byte stream with our new one and send the
  // batch down.
  new (&replacement_stream_)
      grpc_core::SliceBufferByteStream(&slices_, send_flags);
  send_message_batch_->payload->send_message.send_message.reset(
      reinterpret_cast<grpc_core::SliceBufferByteStream*>(
          &replacement_stream_));
  original_send_message_on_complete_ = send_message_batch_->on_complete;
  send_message_batch_->on_complete = &send_message_on_complete_;
  SendMessageBatchContinue(elem);
}

void CallData::FailSendMessageBatchInCallCombiner(void* calld_arg,
                                                  grpc_error* error) {
  CallData* calld = static_cast<CallData*>(calld_arg);
  if (calld->send_message_batch_ != nullptr) {
    grpc_transport_stream_op_batch_finish_with_failure(
        calld->send_message_batch_, GRPC_ERROR_REF(error),
        calld->call_combiner_);
    calld->send_message_batch_ = nullptr;
  }
}

// Pulls a slice from the send_message byte stream and adds it to slices_.
grpc_error* CallData::PullSliceFromSendMessage() {
  grpc_slice incoming_slice;
  grpc_error* error =
      send_message_batch_->payload->send_message.send_message->Pull(
          &incoming_slice);
  if (error == GRPC_ERROR_NONE) {
    grpc_slice_buffer_add(&slices_, incoming_slice);
  }
  return error;
}

// Reads as many slices as possible from the send_message byte stream.
// If all data has been read, invokes FinishSendMessage().  Otherwise,
// an async call to ByteStream::Next() has been started, which will
// eventually result in calling OnSendMessageNextDone().
void CallData::ContinueReadingSendMessage(grpc_call_element* elem) {
  if (slices_.length ==
      send_message_batch_->payload->send_message.send_message->length()) {
    FinishSendMessage(elem);
    return;
  }
  while (send_message_batch_->payload->send_message.send_message->Next(
      ~static_cast<size_t>(0), &on_send_message_next_done_)) {
    grpc_error* error = PullSliceFromSendMessage();
    if (error != GRPC_ERROR_NONE) {
      // Closure callback; does not take ownership of error.
      FailSendMessageBatchInCallCombiner(this, error);
      GRPC_ERROR_UNREF(error);
      return;
    }
    if (slices_.length ==
        send_message_batch_->payload->send_message.send_message->length()) {
      FinishSendMessage(elem);
      break;
    }
  }
}

// Async callback for ByteStream::Next().
void CallData::OnSendMessageNextDone(void* elem_arg, grpc_error* error) {
  grpc_call_element* elem = static_cast<grpc_call_element*>(elem_arg);
  CallData* calld = static_cast<CallData*>(elem->call_data);
  if (error != GRPC_ERROR_NONE) {
    // Closure callback; does not take ownership of error.
    FailSendMessageBatchInCallCombiner(calld, error);
    return;
  }
  error = calld->PullSliceFromSendMessage();
  if (error != GRPC_ERROR_NONE) {
    // Closure callback; does not take ownership of error.
    FailSendMessageBatchInCallCombiner(calld, error);
    GRPC_ERROR_UNREF(error);
    return;
  }
  if (calld->slices_.length == calld->send_message_batch_->payload->send_message
                                   .send_message->length()) {
    calld->FinishSendMessage(elem);
  } else {
    calld->ContinueReadingSendMessage(elem);
  }
}

void CallData::StartSendMessageBatch(void* elem_arg, grpc_error* /*unused*/) {
  grpc_call_element* elem = static_cast<grpc_call_element*>(elem_arg);
  CallData* calld = static_cast<CallData*>(elem->call_data);
  if (calld->SkipMessageCompression()) {
    calld->SendMessageBatchContinue(elem);
  } else {
    calld->ContinueReadingSendMessage(elem);
  }
}

void CallData::CompressStartTransportStreamOpBatch(
    grpc_call_element* elem, grpc_transport_stream_op_batch* batch) {
  GPR_TIMER_SCOPE("compress_start_transport_stream_op_batch", 0);
  // Handle cancel_stream.
  if (batch->cancel_stream) {
    GRPC_ERROR_UNREF(cancel_error_);
    cancel_error_ = GRPC_ERROR_REF(batch->payload->cancel_stream.cancel_error);
    if (send_message_batch_ != nullptr) {
      if (!seen_initial_metadata_) {
        GRPC_CALL_COMBINER_START(
            call_combiner_,
            GRPC_CLOSURE_CREATE(FailSendMessageBatchInCallCombiner, this,
                                grpc_schedule_on_exec_ctx),
            GRPC_ERROR_REF(cancel_error_), "failing send_message op");
      } else {
        send_message_batch_->payload->send_message.send_message->Shutdown(
            GRPC_ERROR_REF(cancel_error_));
      }
    }
  } else if (cancel_error_ != GRPC_ERROR_NONE) {
    grpc_transport_stream_op_batch_finish_with_failure(
        batch, GRPC_ERROR_REF(cancel_error_), call_combiner_);
    return;
  }
  // Handle send_initial_metadata.
  if (batch->send_initial_metadata) {
    GPR_ASSERT(!seen_initial_metadata_);
    grpc_error* error = ProcessSendInitialMetadata(
        elem, batch->payload->send_initial_metadata.send_initial_metadata);
    if (error != GRPC_ERROR_NONE) {
      grpc_transport_stream_op_batch_finish_with_failure(batch, error,
                                                         call_combiner_);
      return;
    }
    seen_initial_metadata_ = true;
    // If we had previously received a batch containing a send_message op,
    // handle it now.  Note that we need to re-enter the call combiner
    // for this, since we can't send two batches down while holding the
    // call combiner, since the connected_channel filter (at the bottom of
    // the call stack) will release the call combiner for each batch it sees.
    if (send_message_batch_ != nullptr) {
      GRPC_CALL_COMBINER_START(
          call_combiner_, &start_send_message_batch_in_call_combiner_,
          GRPC_ERROR_NONE, "starting send_message after send_initial_metadata");
    }
  }
  // Handle send_message.
  if (batch->send_message) {
    GPR_ASSERT(send_message_batch_ == nullptr);
    send_message_batch_ = batch;
    // If we have not yet seen send_initial_metadata, then we have to
    // wait.  We save the batch and then drop the call combiner, which we'll
    // have to pick up again later when we get send_initial_metadata.
    if (!seen_initial_metadata_) {
      GRPC_CALL_COMBINER_STOP(
          call_combiner_, "send_message batch pending send_initial_metadata");
      return;
    }
    StartSendMessageBatch(elem, GRPC_ERROR_NONE);
  } else {
    // Pass control down the stack.
    grpc_call_next_op(elem, batch);
  }
}

void CompressStartTransportStreamOpBatch(
    grpc_call_element* elem, grpc_transport_stream_op_batch* batch) {
  CallData* calld = static_cast<CallData*>(elem->call_data);
  calld->CompressStartTransportStreamOpBatch(elem, batch);
}

/* Constructor for call_data */
grpc_error* CompressInitCallElem(grpc_call_element* elem,
                                 const grpc_call_element_args* args) {
  new (elem->call_data) CallData(elem, *args);
  return GRPC_ERROR_NONE;
}

/* Destructor for call_data */
void CompressDestroyCallElem(grpc_call_element* elem,
                             const grpc_call_final_info* /*final_info*/,
                             grpc_closure* /*ignored*/) {
  CallData* calld = static_cast<CallData*>(elem->call_data);
  calld->~CallData();
}

/* Constructor for ChannelData */
grpc_error* CompressInitChannelElem(grpc_channel_element* elem,
                                    grpc_channel_element_args* args) {
  new (elem->channel_data) ChannelData(args);
  return GRPC_ERROR_NONE;
}

/* Destructor for channel data */
void CompressDestroyChannelElem(grpc_channel_element* elem) {
  ChannelData* channeld = static_cast<ChannelData*>(elem->channel_data);
  channeld->~ChannelData();
}

}  // namespace

const grpc_channel_filter grpc_message_compress_filter = {
    CompressStartTransportStreamOpBatch,
    grpc_channel_next_op,
    sizeof(CallData),
    CompressInitCallElem,
    grpc_call_stack_ignore_set_pollset_or_pollset_set,
    CompressDestroyCallElem,
    sizeof(ChannelData),
    CompressInitChannelElem,
    CompressDestroyChannelElem,
    grpc_channel_next_get_info,
    "message_compress"};

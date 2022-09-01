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

#include "src/core/ext/filters/http/message_compress/message_compress_filter.h"

#include <inttypes.h>
#include <stdlib.h>

#include <new>
#include <utility>

#include "absl/meta/type_traits.h"
#include "absl/types/optional.h"

#include <grpc/compression.h>
#include <grpc/impl/codegen/compression_types.h>
#include <grpc/impl/codegen/grpc_types.h>
#include <grpc/support/log.h>

#include "src/core/lib/compression/compression_internal.h"
#include "src/core/lib/compression/message_compress.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/iomgr/call_combiner.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/transport.h"

namespace {

class ChannelData {
 public:
  explicit ChannelData(grpc_channel_element_args* args) {
    // Get the enabled and the default algorithms from channel args.
    enabled_compression_algorithms_ =
        grpc_core::CompressionAlgorithmSet::FromChannelArgs(args->channel_args);
    default_compression_algorithm_ =
        grpc_core::DefaultCompressionAlgorithmFromChannelArgs(
            args->channel_args)
            .value_or(GRPC_COMPRESS_NONE);
    // Make sure the default is enabled.
    if (!enabled_compression_algorithms_.IsSet(
            default_compression_algorithm_)) {
      const char* name;
      if (!grpc_compression_algorithm_name(default_compression_algorithm_,
                                           &name)) {
        name = "<unknown>";
      }
      gpr_log(GPR_ERROR,
              "default compression algorithm %s not enabled: switching to none",
              name);
      default_compression_algorithm_ = GRPC_COMPRESS_NONE;
    }
    GPR_ASSERT(!args->is_last);
  }

  grpc_compression_algorithm default_compression_algorithm() const {
    return default_compression_algorithm_;
  }

  grpc_core::CompressionAlgorithmSet enabled_compression_algorithms() const {
    return enabled_compression_algorithms_;
  }

 private:
  /** The default, channel-level, compression algorithm */
  grpc_compression_algorithm default_compression_algorithm_;
  /** Enabled compression algorithms */
  grpc_core::CompressionAlgorithmSet enabled_compression_algorithms_;
};

class CallData {
 public:
  CallData(grpc_call_element* elem, const grpc_call_element_args& args)
      : call_combiner_(args.call_combiner) {
    ChannelData* channeld = static_cast<ChannelData*>(elem->channel_data);
    // The call's message compression algorithm is set to channel's default
    // setting. It can be overridden later by initial metadata.
    if (GPR_LIKELY(channeld->enabled_compression_algorithms().IsSet(
            channeld->default_compression_algorithm()))) {
      compression_algorithm_ = channeld->default_compression_algorithm();
    }
    GRPC_CLOSURE_INIT(&forward_send_message_batch_in_call_combiner_,
                      ForwardSendMessageBatch, elem, grpc_schedule_on_exec_ctx);
  }

  ~CallData() { GRPC_ERROR_UNREF(cancel_error_); }

  void CompressStartTransportStreamOpBatch(
      grpc_call_element* elem, grpc_transport_stream_op_batch* batch);

 private:
  bool SkipMessageCompression();
  void FinishSendMessage(grpc_call_element* elem);

  void ProcessSendInitialMetadata(grpc_call_element* elem,
                                  grpc_metadata_batch* initial_metadata);

  // Methods for processing a send_message batch
  static void FailSendMessageBatchInCallCombiner(void* calld_arg,
                                                 grpc_error_handle error);
  static void ForwardSendMessageBatch(void* elem_arg, grpc_error_handle unused);

  grpc_core::CallCombiner* call_combiner_;
  grpc_compression_algorithm compression_algorithm_ = GRPC_COMPRESS_NONE;
  grpc_error_handle cancel_error_ = GRPC_ERROR_NONE;
  grpc_transport_stream_op_batch* send_message_batch_ = nullptr;
  bool seen_initial_metadata_ = false;
  grpc_closure forward_send_message_batch_in_call_combiner_;
};

// Returns true if we should skip message compression for the current message.
bool CallData::SkipMessageCompression() {
  // If the flags of this message indicate that it shouldn't be compressed, we
  // skip message compression.
  uint32_t flags = send_message_batch_->payload->send_message.flags;
  if (flags & (GRPC_WRITE_NO_COMPRESS | GRPC_WRITE_INTERNAL_COMPRESS)) {
    return true;
  }
  // If this call doesn't have any message compression algorithm set, skip
  // message compression.
  return compression_algorithm_ == GRPC_COMPRESS_NONE;
}

void CallData::ProcessSendInitialMetadata(
    grpc_call_element* elem, grpc_metadata_batch* initial_metadata) {
  ChannelData* channeld = static_cast<ChannelData*>(elem->channel_data);
  // Find the compression algorithm.
  compression_algorithm_ =
      initial_metadata->Take(grpc_core::GrpcInternalEncodingRequest())
          .value_or(channeld->default_compression_algorithm());
  switch (compression_algorithm_) {
    case GRPC_COMPRESS_NONE:
      break;
    case GRPC_COMPRESS_DEFLATE:
    case GRPC_COMPRESS_GZIP:
      initial_metadata->Set(grpc_core::GrpcEncodingMetadata(),
                            compression_algorithm_);
      break;
    case GRPC_COMPRESS_ALGORITHMS_COUNT:
      abort();
  }
  // Convey supported compression algorithms.
  initial_metadata->Set(grpc_core::GrpcAcceptEncodingMetadata(),
                        channeld->enabled_compression_algorithms());
}

void CallData::FinishSendMessage(grpc_call_element* elem) {
  // Compress the data if appropriate.
  if (!SkipMessageCompression()) {
    grpc_core::SliceBuffer tmp;
    uint32_t& send_flags = send_message_batch_->payload->send_message.flags;
    grpc_core::SliceBuffer* payload =
        send_message_batch_->payload->send_message.send_message;
    bool did_compress =
        grpc_msg_compress(compression_algorithm_, payload->c_slice_buffer(),
                          tmp.c_slice_buffer());
    if (did_compress) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_compression_trace)) {
        const char* algo_name;
        const size_t before_size = payload->Length();
        const size_t after_size = tmp.Length();
        const float savings_ratio = 1.0f - static_cast<float>(after_size) /
                                               static_cast<float>(before_size);
        GPR_ASSERT(grpc_compression_algorithm_name(compression_algorithm_,
                                                   &algo_name));
        gpr_log(GPR_INFO,
                "Compressed[%s] %" PRIuPTR " bytes vs. %" PRIuPTR
                " bytes (%.2f%% savings)",
                algo_name, before_size, after_size, 100 * savings_ratio);
      }
      tmp.Swap(payload);
      send_flags |= GRPC_WRITE_INTERNAL_COMPRESS;
    } else {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_compression_trace)) {
        const char* algo_name;
        GPR_ASSERT(grpc_compression_algorithm_name(compression_algorithm_,
                                                   &algo_name));
        gpr_log(
            GPR_INFO,
            "Algorithm '%s' enabled but decided not to compress. Input size: "
            "%" PRIuPTR,
            algo_name, payload->Length());
      }
    }
  }
  grpc_call_next_op(elem, std::exchange(send_message_batch_, nullptr));
}

void CallData::FailSendMessageBatchInCallCombiner(void* calld_arg,
                                                  grpc_error_handle error) {
  CallData* calld = static_cast<CallData*>(calld_arg);
  if (calld->send_message_batch_ != nullptr) {
    grpc_transport_stream_op_batch_finish_with_failure(
        calld->send_message_batch_, GRPC_ERROR_REF(error),
        calld->call_combiner_);
    calld->send_message_batch_ = nullptr;
  }
}

void CallData::ForwardSendMessageBatch(void* elem_arg,
                                       grpc_error_handle /*unused*/) {
  grpc_call_element* elem = static_cast<grpc_call_element*>(elem_arg);
  CallData* calld = static_cast<CallData*>(elem->call_data);
  calld->FinishSendMessage(elem);
}

void CallData::CompressStartTransportStreamOpBatch(
    grpc_call_element* elem, grpc_transport_stream_op_batch* batch) {
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
      }
    }
  } else if (!GRPC_ERROR_IS_NONE(cancel_error_)) {
    grpc_transport_stream_op_batch_finish_with_failure(
        batch, GRPC_ERROR_REF(cancel_error_), call_combiner_);
    return;
  }
  // Handle send_initial_metadata.
  if (batch->send_initial_metadata) {
    GPR_ASSERT(!seen_initial_metadata_);
    ProcessSendInitialMetadata(
        elem, batch->payload->send_initial_metadata.send_initial_metadata);
    seen_initial_metadata_ = true;
    // If we had previously received a batch containing a send_message op,
    // handle it now.  Note that we need to re-enter the call combiner
    // for this, since we can't send two batches down while holding the
    // call combiner, since the connected_channel filter (at the bottom of
    // the call stack) will release the call combiner for each batch it sees.
    if (send_message_batch_ != nullptr) {
      GRPC_CALL_COMBINER_START(
          call_combiner_, &forward_send_message_batch_in_call_combiner_,
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
    FinishSendMessage(elem);
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
grpc_error_handle CompressInitCallElem(grpc_call_element* elem,
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
grpc_error_handle CompressInitChannelElem(grpc_channel_element* elem,
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
    nullptr,
    grpc_channel_next_op,
    sizeof(CallData),
    CompressInitCallElem,
    grpc_call_stack_ignore_set_pollset_or_pollset_set,
    CompressDestroyCallElem,
    sizeof(ChannelData),
    CompressInitChannelElem,
    grpc_channel_stack_no_post_init,
    CompressDestroyChannelElem,
    grpc_channel_next_get_info,
    "message_compress"};

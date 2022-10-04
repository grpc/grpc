//
//
// Copyright 2020 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/http/message_compress/message_decompress_filter.h"

#include <string.h>

#include <cstdint>
#include <new>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/types/optional.h"

#include <grpc/impl/codegen/compression_types.h>
#include <grpc/status.h>
#include <grpc/support/log.h>

#include "src/core/ext/filters/message_size/message_size_filter.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/compression/message_compress.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/iomgr/call_combiner.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/transport.h"

namespace grpc_core {
namespace {

class ChannelData {
 public:
  explicit ChannelData(const grpc_channel_element_args* args)
      : max_recv_size_(GetMaxRecvSizeFromChannelArgs(
            ChannelArgs::FromC(args->channel_args))),
        message_size_service_config_parser_index_(
            MessageSizeParser::ParserIndex()) {}

  absl::optional<uint32_t> max_recv_size() const { return max_recv_size_; }
  size_t message_size_service_config_parser_index() const {
    return message_size_service_config_parser_index_;
  }

 private:
  absl::optional<uint32_t> max_recv_size_;
  const size_t message_size_service_config_parser_index_;
};

class CallData {
 public:
  CallData(const grpc_call_element_args& args, const ChannelData* chand)
      : call_combiner_(args.call_combiner),
        max_recv_message_length_(chand->max_recv_size()) {
    // Initialize state for recv_initial_metadata_ready callback
    GRPC_CLOSURE_INIT(&on_recv_initial_metadata_ready_,
                      OnRecvInitialMetadataReady, this,
                      grpc_schedule_on_exec_ctx);
    // Initialize state for recv_message_ready callback
    GRPC_CLOSURE_INIT(&on_recv_message_ready_, OnRecvMessageReady, this,
                      grpc_schedule_on_exec_ctx);
    // Initialize state for recv_trailing_metadata_ready callback
    GRPC_CLOSURE_INIT(&on_recv_trailing_metadata_ready_,
                      OnRecvTrailingMetadataReady, this,
                      grpc_schedule_on_exec_ctx);
    const MessageSizeParsedConfig* limits =
        MessageSizeParsedConfig::GetFromCallContext(
            args.context, chand->message_size_service_config_parser_index());
    if (limits != nullptr && limits->max_recv_size().has_value() &&
        (!max_recv_message_length_.has_value() ||
         *limits->max_recv_size() < *max_recv_message_length_)) {
      max_recv_message_length_ = *limits->max_recv_size();
    }
  }

  void DecompressStartTransportStreamOpBatch(
      grpc_call_element* elem, grpc_transport_stream_op_batch* batch);

 private:
  static void OnRecvInitialMetadataReady(void* arg, grpc_error_handle error);

  // Methods for processing a receive message event
  void MaybeResumeOnRecvMessageReady();
  static void OnRecvMessageReady(void* arg, grpc_error_handle error);
  void ContinueRecvMessageReadyCallback(grpc_error_handle error);

  // Methods for processing a recv_trailing_metadata event
  void MaybeResumeOnRecvTrailingMetadataReady();
  static void OnRecvTrailingMetadataReady(void* arg, grpc_error_handle error);

  CallCombiner* call_combiner_;
  // Overall error for the call
  grpc_error_handle error_;
  // Fields for handling recv_initial_metadata_ready callback
  grpc_closure on_recv_initial_metadata_ready_;
  grpc_closure* original_recv_initial_metadata_ready_ = nullptr;
  grpc_metadata_batch* recv_initial_metadata_ = nullptr;
  // Fields for handling recv_message_ready callback
  bool seen_recv_message_ready_ = false;
  absl::optional<uint32_t> max_recv_message_length_;
  grpc_compression_algorithm algorithm_ = GRPC_COMPRESS_NONE;
  absl::optional<SliceBuffer>* recv_message_ = nullptr;
  uint32_t* recv_message_flags_ = nullptr;
  grpc_closure on_recv_message_ready_;
  grpc_closure* original_recv_message_ready_ = nullptr;
  // Fields for handling recv_trailing_metadata_ready callback
  bool seen_recv_trailing_metadata_ready_ = false;
  grpc_closure on_recv_trailing_metadata_ready_;
  grpc_closure* original_recv_trailing_metadata_ready_ = nullptr;
  grpc_error_handle on_recv_trailing_metadata_ready_error_;
};

void CallData::OnRecvInitialMetadataReady(void* arg, grpc_error_handle error) {
  CallData* calld = static_cast<CallData*>(arg);
  if (error.ok()) {
    calld->algorithm_ =
        calld->recv_initial_metadata_->get(GrpcEncodingMetadata())
            .value_or(GRPC_COMPRESS_NONE);
  }
  calld->MaybeResumeOnRecvMessageReady();
  calld->MaybeResumeOnRecvTrailingMetadataReady();
  grpc_closure* closure = calld->original_recv_initial_metadata_ready_;
  calld->original_recv_initial_metadata_ready_ = nullptr;
  Closure::Run(DEBUG_LOCATION, closure, error);
}

void CallData::MaybeResumeOnRecvMessageReady() {
  if (seen_recv_message_ready_) {
    seen_recv_message_ready_ = false;
    GRPC_CALL_COMBINER_START(call_combiner_, &on_recv_message_ready_,
                             absl::OkStatus(),
                             "continue recv_message_ready callback");
  }
}

void CallData::OnRecvMessageReady(void* arg, grpc_error_handle error) {
  CallData* calld = static_cast<CallData*>(arg);
  if (error.ok()) {
    if (calld->original_recv_initial_metadata_ready_ != nullptr) {
      calld->seen_recv_message_ready_ = true;
      GRPC_CALL_COMBINER_STOP(calld->call_combiner_,
                              "Deferring OnRecvMessageReady until after "
                              "OnRecvInitialMetadataReady");
      return;
    }
    if (calld->algorithm_ != GRPC_COMPRESS_NONE) {
      // recv_message can be NULL if trailing metadata is received instead of
      // message, or it's possible that the message was not compressed.
      if (!calld->recv_message_->has_value() ||
          (*calld->recv_message_)->Length() == 0 ||
          ((*calld->recv_message_flags_ & GRPC_WRITE_INTERNAL_COMPRESS) == 0)) {
        return calld->ContinueRecvMessageReadyCallback(absl::OkStatus());
      }
      if (calld->max_recv_message_length_.has_value() &&
          (*calld->recv_message_)->Length() >
              static_cast<uint32_t>(*calld->max_recv_message_length_)) {
        GPR_DEBUG_ASSERT(calld->error_.ok());
        calld->error_ = grpc_error_set_int(
            GRPC_ERROR_CREATE(
                absl::StrFormat("Received message larger than max (%u vs. %d)",
                                (*calld->recv_message_)->Length(),
                                *calld->max_recv_message_length_)),
            StatusIntProperty::kRpcStatus, GRPC_STATUS_RESOURCE_EXHAUSTED);
        return calld->ContinueRecvMessageReadyCallback(calld->error_);
      }
      SliceBuffer decompressed_slices;
      if (grpc_msg_decompress(calld->algorithm_,
                              (*calld->recv_message_)->c_slice_buffer(),
                              decompressed_slices.c_slice_buffer()) == 0) {
        GPR_DEBUG_ASSERT(calld->error_.ok());
        calld->error_ = GRPC_ERROR_CREATE(absl::StrCat(
            "Unexpected error decompressing data for algorithm with "
            "enum value ",
            calld->algorithm_));
      } else {
        *calld->recv_message_flags_ =
            (*calld->recv_message_flags_ & (~GRPC_WRITE_INTERNAL_COMPRESS)) |
            GRPC_WRITE_INTERNAL_TEST_ONLY_WAS_COMPRESSED;
        (*calld->recv_message_)->Swap(&decompressed_slices);
      }
      return calld->ContinueRecvMessageReadyCallback(calld->error_);
    }
  }
  calld->ContinueRecvMessageReadyCallback(error);
}

void CallData::ContinueRecvMessageReadyCallback(grpc_error_handle error) {
  MaybeResumeOnRecvTrailingMetadataReady();
  // The surface will clean up the receiving stream if there is an error.
  grpc_closure* closure = original_recv_message_ready_;
  original_recv_message_ready_ = nullptr;
  Closure::Run(DEBUG_LOCATION, closure, error);
}

void CallData::MaybeResumeOnRecvTrailingMetadataReady() {
  if (seen_recv_trailing_metadata_ready_) {
    seen_recv_trailing_metadata_ready_ = false;
    grpc_error_handle error = on_recv_trailing_metadata_ready_error_;
    on_recv_trailing_metadata_ready_error_ = absl::OkStatus();
    GRPC_CALL_COMBINER_START(call_combiner_, &on_recv_trailing_metadata_ready_,
                             error, "Continuing OnRecvTrailingMetadataReady");
  }
}

void CallData::OnRecvTrailingMetadataReady(void* arg, grpc_error_handle error) {
  CallData* calld = static_cast<CallData*>(arg);
  if (calld->original_recv_initial_metadata_ready_ != nullptr ||
      calld->original_recv_message_ready_ != nullptr) {
    calld->seen_recv_trailing_metadata_ready_ = true;
    calld->on_recv_trailing_metadata_ready_error_ = error;
    GRPC_CALL_COMBINER_STOP(
        calld->call_combiner_,
        "Deferring OnRecvTrailingMetadataReady until after "
        "OnRecvInitialMetadataReady and OnRecvMessageReady");
    return;
  }
  error = grpc_error_add_child(error, calld->error_);
  calld->error_ = absl::OkStatus();
  grpc_closure* closure = calld->original_recv_trailing_metadata_ready_;
  calld->original_recv_trailing_metadata_ready_ = nullptr;
  Closure::Run(DEBUG_LOCATION, closure, error);
}

void CallData::DecompressStartTransportStreamOpBatch(
    grpc_call_element* elem, grpc_transport_stream_op_batch* batch) {
  // Handle recv_initial_metadata.
  if (batch->recv_initial_metadata) {
    recv_initial_metadata_ =
        batch->payload->recv_initial_metadata.recv_initial_metadata;
    original_recv_initial_metadata_ready_ =
        batch->payload->recv_initial_metadata.recv_initial_metadata_ready;
    batch->payload->recv_initial_metadata.recv_initial_metadata_ready =
        &on_recv_initial_metadata_ready_;
  }
  // Handle recv_message
  if (batch->recv_message) {
    recv_message_ = batch->payload->recv_message.recv_message;
    recv_message_flags_ = batch->payload->recv_message.flags;
    original_recv_message_ready_ =
        batch->payload->recv_message.recv_message_ready;
    batch->payload->recv_message.recv_message_ready = &on_recv_message_ready_;
  }
  // Handle recv_trailing_metadata
  if (batch->recv_trailing_metadata) {
    original_recv_trailing_metadata_ready_ =
        batch->payload->recv_trailing_metadata.recv_trailing_metadata_ready;
    batch->payload->recv_trailing_metadata.recv_trailing_metadata_ready =
        &on_recv_trailing_metadata_ready_;
  }
  // Pass control down the stack.
  grpc_call_next_op(elem, batch);
}

void DecompressStartTransportStreamOpBatch(
    grpc_call_element* elem, grpc_transport_stream_op_batch* batch) {
  CallData* calld = static_cast<CallData*>(elem->call_data);
  calld->DecompressStartTransportStreamOpBatch(elem, batch);
}

grpc_error_handle DecompressInitCallElem(grpc_call_element* elem,
                                         const grpc_call_element_args* args) {
  ChannelData* chand = static_cast<ChannelData*>(elem->channel_data);
  new (elem->call_data) CallData(*args, chand);
  return absl::OkStatus();
}

void DecompressDestroyCallElem(grpc_call_element* elem,
                               const grpc_call_final_info* /*final_info*/,
                               grpc_closure* /*ignored*/) {
  CallData* calld = static_cast<CallData*>(elem->call_data);
  calld->~CallData();
}

grpc_error_handle DecompressInitChannelElem(grpc_channel_element* elem,
                                            grpc_channel_element_args* args) {
  ChannelData* chand = static_cast<ChannelData*>(elem->channel_data);
  new (chand) ChannelData(args);
  return absl::OkStatus();
}

void DecompressDestroyChannelElem(grpc_channel_element* elem) {
  ChannelData* chand = static_cast<ChannelData*>(elem->channel_data);
  chand->~ChannelData();
}

}  // namespace

const grpc_channel_filter MessageDecompressFilter = {
    DecompressStartTransportStreamOpBatch,
    nullptr,
    grpc_channel_next_op,
    sizeof(CallData),
    DecompressInitCallElem,
    grpc_call_stack_ignore_set_pollset_or_pollset_set,
    DecompressDestroyCallElem,
    sizeof(ChannelData),
    DecompressInitChannelElem,
    grpc_channel_stack_no_post_init,
    DecompressDestroyChannelElem,
    grpc_channel_next_get_info,
    "message_decompress"};
}  // namespace grpc_core

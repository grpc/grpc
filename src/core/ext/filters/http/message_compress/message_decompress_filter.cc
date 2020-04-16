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

#include <assert.h>
#include <string.h>

#include <grpc/compression.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/filters/http/message_compress/message_decompress_filter.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/compression/algorithm_metadata.h"
#include "src/core/lib/compression/compression_args.h"
#include "src/core/lib/compression/compression_internal.h"
#include "src/core/lib/compression/message_compress.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"

namespace {

class ChannelData {};

class CallData {
 public:
  explicit CallData(const grpc_call_element_args& args)
      : call_combiner_(args.call_combiner) {
    // Initialize state for recv_initial_metadata_ready callback
    GRPC_CLOSURE_INIT(&on_recv_initial_metadata_ready_,
                      OnRecvInitialMetadataReady, this,
                      grpc_schedule_on_exec_ctx);
    // Initialize state for recv_message_ready callback
    grpc_slice_buffer_init(&recv_slices_);
    GRPC_CLOSURE_INIT(&on_recv_message_next_done_, OnRecvMessageNextDone, this,
                      grpc_schedule_on_exec_ctx);
    GRPC_CLOSURE_INIT(&on_recv_message_ready_, OnRecvMessageReady, this,
                      grpc_schedule_on_exec_ctx);
    // Initialize state for recv_trailing_metadata_ready callback
    GRPC_CLOSURE_INIT(&on_recv_trailing_metadata_ready_,
                      OnRecvTrailingMetadataReady, this,
                      grpc_schedule_on_exec_ctx);
  }

  ~CallData() { grpc_slice_buffer_destroy_internal(&recv_slices_); }

  void DecompressStartTransportStreamOpBatch(
      grpc_call_element* elem, grpc_transport_stream_op_batch* batch);

 private:
  static void OnRecvInitialMetadataReady(void* arg, grpc_error* error);

  // Methods for processing a receive message event
  void MaybeResumeOnRecvMessageReady();
  static void OnRecvMessageReady(void* arg, grpc_error* error);
  static void OnRecvMessageNextDone(void* arg, grpc_error* error);
  grpc_error* PullSliceFromRecvMessage();
  void ContinueReadingRecvMessage();
  void FinishRecvMessage();
  void ContinueRecvMessageReadyCallback(grpc_error* error);

  // Methods for processing a recv_trailing_metadata event
  void MaybeResumeOnRecvTrailingMetadataReady();
  static void OnRecvTrailingMetadataReady(void* arg, grpc_error* error);

  grpc_core::CallCombiner* call_combiner_;
  // Overall error for the call
  grpc_error* error_ = GRPC_ERROR_NONE;
  // Fields for handling recv_initial_metadata_ready callback
  grpc_closure on_recv_initial_metadata_ready_;
  grpc_closure* original_recv_initial_metadata_ready_ = nullptr;
  grpc_metadata_batch* recv_initial_metadata_ = nullptr;
  // Fields for handling recv_message_ready callback
  bool seen_recv_message_ready_ = false;
  grpc_message_compression_algorithm algorithm_ = GRPC_MESSAGE_COMPRESS_NONE;
  grpc_closure on_recv_message_ready_;
  grpc_closure* original_recv_message_ready_ = nullptr;
  grpc_closure on_recv_message_next_done_;
  grpc_core::OrphanablePtr<grpc_core::ByteStream>* recv_message_ = nullptr;
  // recv_slices_ holds the slices read from the original recv_message stream.
  // It is initialized during construction and reset when a new stream is
  // created using it.
  grpc_slice_buffer recv_slices_;
  std::aligned_storage<sizeof(grpc_core::SliceBufferByteStream),
                       alignof(grpc_core::SliceBufferByteStream)>::type
      recv_replacement_stream_;
  // Fields for handling recv_trailing_metadata_ready callback
  bool seen_recv_trailing_metadata_ready_ = false;
  grpc_closure on_recv_trailing_metadata_ready_;
  grpc_closure* original_recv_trailing_metadata_ready_ = nullptr;
  grpc_error* on_recv_trailing_metadata_ready_error_ = GRPC_ERROR_NONE;
};

grpc_message_compression_algorithm DecodeMessageCompressionAlgorithm(
    grpc_mdelem md) {
  grpc_message_compression_algorithm algorithm =
      grpc_message_compression_algorithm_from_slice(GRPC_MDVALUE(md));
  if (algorithm == GRPC_MESSAGE_COMPRESS_ALGORITHMS_COUNT) {
    char* md_c_str = grpc_slice_to_c_string(GRPC_MDVALUE(md));
    gpr_log(GPR_ERROR,
            "Invalid incoming message compression algorithm: '%s'. "
            "Interpreting incoming data as uncompressed.",
            md_c_str);
    gpr_free(md_c_str);
    return GRPC_MESSAGE_COMPRESS_NONE;
  }
  return algorithm;
}

void CallData::OnRecvInitialMetadataReady(void* arg, grpc_error* error) {
  CallData* calld = static_cast<CallData*>(arg);
  if (error == GRPC_ERROR_NONE) {
    grpc_linked_mdelem* grpc_encoding =
        calld->recv_initial_metadata_->idx.named.grpc_encoding;
    if (grpc_encoding != nullptr) {
      calld->algorithm_ = DecodeMessageCompressionAlgorithm(grpc_encoding->md);
    }
  }
  calld->MaybeResumeOnRecvMessageReady();
  calld->MaybeResumeOnRecvTrailingMetadataReady();
  grpc_closure* closure = calld->original_recv_initial_metadata_ready_;
  calld->original_recv_initial_metadata_ready_ = nullptr;
  grpc_core::Closure::Run(DEBUG_LOCATION, closure, GRPC_ERROR_REF(error));
}

void CallData::MaybeResumeOnRecvMessageReady() {
  if (seen_recv_message_ready_) {
    seen_recv_message_ready_ = false;
    GRPC_CALL_COMBINER_START(call_combiner_, &on_recv_message_ready_,
                             GRPC_ERROR_NONE,
                             "continue recv_message_ready callback");
  }
}

void CallData::OnRecvMessageReady(void* arg, grpc_error* error) {
  CallData* calld = static_cast<CallData*>(arg);
  if (error == GRPC_ERROR_NONE) {
    if (calld->original_recv_initial_metadata_ready_ != nullptr) {
      calld->seen_recv_message_ready_ = true;
      GRPC_CALL_COMBINER_STOP(calld->call_combiner_,
                              "Deferring OnRecvMessageReady until after "
                              "OnRecvInitialMetadataReady");
      return;
    }
    if (calld->algorithm_ != GRPC_MESSAGE_COMPRESS_NONE) {
      // recv_message can be NULL if trailing metadata is received instead of
      // message, or it's possible that the message was not compressed.
      if (*calld->recv_message_ == nullptr ||
          (*calld->recv_message_)->length() == 0 ||
          ((*calld->recv_message_)->flags() & GRPC_WRITE_INTERNAL_COMPRESS) ==
              0) {
        return calld->ContinueRecvMessageReadyCallback(GRPC_ERROR_NONE);
      }
      grpc_slice_buffer_destroy_internal(&calld->recv_slices_);
      grpc_slice_buffer_init(&calld->recv_slices_);
      return calld->ContinueReadingRecvMessage();
    }
  }
  calld->ContinueRecvMessageReadyCallback(GRPC_ERROR_REF(error));
}

void CallData::ContinueReadingRecvMessage() {
  while ((*recv_message_)
             ->Next((*recv_message_)->length() - recv_slices_.length,
                    &on_recv_message_next_done_)) {
    grpc_error* error = PullSliceFromRecvMessage();
    if (error != GRPC_ERROR_NONE) {
      return ContinueRecvMessageReadyCallback(error);
    }
    // We have read the entire message.
    if (recv_slices_.length == (*recv_message_)->length()) {
      return FinishRecvMessage();
    }
  }
}

grpc_error* CallData::PullSliceFromRecvMessage() {
  grpc_slice incoming_slice;
  grpc_error* error = (*recv_message_)->Pull(&incoming_slice);
  if (error == GRPC_ERROR_NONE) {
    grpc_slice_buffer_add(&recv_slices_, incoming_slice);
  }
  return error;
}

void CallData::OnRecvMessageNextDone(void* arg, grpc_error* error) {
  CallData* calld = static_cast<CallData*>(arg);
  if (error != GRPC_ERROR_NONE) {
    return calld->ContinueRecvMessageReadyCallback(GRPC_ERROR_REF(error));
  }
  error = calld->PullSliceFromRecvMessage();
  if (error != GRPC_ERROR_NONE) {
    return calld->ContinueRecvMessageReadyCallback(error);
  }
  if (calld->recv_slices_.length == (*calld->recv_message_)->length()) {
    calld->FinishRecvMessage();
  } else {
    calld->ContinueReadingRecvMessage();
  }
}

void CallData::FinishRecvMessage() {
  grpc_slice_buffer decompressed_slices;
  grpc_slice_buffer_init(&decompressed_slices);
  if (grpc_msg_decompress(algorithm_, &recv_slices_, &decompressed_slices) ==
      0) {
    char* msg;
    gpr_asprintf(
        &msg,
        "Unexpected error decompressing data for algorithm with enum value %d",
        algorithm_);
    GPR_DEBUG_ASSERT(error_ == GRPC_ERROR_NONE);
    error_ = GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg);
    gpr_free(msg);
    grpc_slice_buffer_destroy_internal(&decompressed_slices);
  } else {
    uint32_t recv_flags =
        ((*recv_message_)->flags() & (~GRPC_WRITE_INTERNAL_COMPRESS)) |
        GRPC_WRITE_INTERNAL_TEST_ONLY_WAS_COMPRESSED;
    // Swap out the original receive byte stream with our new one and send the
    // batch down.
    // Initializing recv_replacement_stream_ with decompressed_slices removes
    // all the slices from decompressed_slices leaving it empty.
    new (&recv_replacement_stream_)
        grpc_core::SliceBufferByteStream(&decompressed_slices, recv_flags);
    recv_message_->reset(reinterpret_cast<grpc_core::SliceBufferByteStream*>(
        &recv_replacement_stream_));
    recv_message_ = nullptr;
  }
  ContinueRecvMessageReadyCallback(GRPC_ERROR_REF(error_));
}

void CallData::ContinueRecvMessageReadyCallback(grpc_error* error) {
  MaybeResumeOnRecvTrailingMetadataReady();
  // The surface will clean up the receiving stream if there is an error.
  grpc_closure* closure = original_recv_message_ready_;
  original_recv_message_ready_ = nullptr;
  grpc_core::Closure::Run(DEBUG_LOCATION, closure, error);
}

void CallData::MaybeResumeOnRecvTrailingMetadataReady() {
  if (seen_recv_trailing_metadata_ready_) {
    seen_recv_trailing_metadata_ready_ = false;
    grpc_error* error = on_recv_trailing_metadata_ready_error_;
    on_recv_trailing_metadata_ready_error_ = GRPC_ERROR_NONE;
    GRPC_CALL_COMBINER_START(call_combiner_, &on_recv_trailing_metadata_ready_,
                             error, "Continuing OnRecvTrailingMetadataReady");
  }
}

void CallData::OnRecvTrailingMetadataReady(void* arg, grpc_error* error) {
  CallData* calld = static_cast<CallData*>(arg);
  if (calld->original_recv_initial_metadata_ready_ != nullptr ||
      calld->original_recv_message_ready_ != nullptr) {
    calld->seen_recv_trailing_metadata_ready_ = true;
    calld->on_recv_trailing_metadata_ready_error_ = GRPC_ERROR_REF(error);
    GRPC_CALL_COMBINER_STOP(
        calld->call_combiner_,
        "Deferring OnRecvTrailingMetadataReady until after "
        "OnRecvInitialMetadataReady and OnRecvMessageReady");
    return;
  }
  error = grpc_error_add_child(GRPC_ERROR_REF(error), calld->error_);
  calld->error_ = GRPC_ERROR_NONE;
  grpc_closure* closure = calld->original_recv_trailing_metadata_ready_;
  calld->original_recv_trailing_metadata_ready_ = nullptr;
  grpc_core::Closure::Run(DEBUG_LOCATION, closure, error);
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
  GPR_TIMER_SCOPE("decompress_start_transport_stream_op_batch", 0);
  CallData* calld = static_cast<CallData*>(elem->call_data);
  calld->DecompressStartTransportStreamOpBatch(elem, batch);
}

static grpc_error* DecompressInitCallElem(grpc_call_element* elem,
                                          const grpc_call_element_args* args) {
  new (elem->call_data) CallData(*args);
  return GRPC_ERROR_NONE;
}

static void DecompressDestroyCallElem(
    grpc_call_element* elem, const grpc_call_final_info* /*final_info*/,
    grpc_closure* /*ignored*/) {
  CallData* calld = static_cast<CallData*>(elem->call_data);
  calld->~CallData();
}

static grpc_error* DecompressInitChannelElem(
    grpc_channel_element* /*elem*/, grpc_channel_element_args* /*args*/) {
  return GRPC_ERROR_NONE;
}

void DecompressDestroyChannelElem(grpc_channel_element* /*elem*/) {}

}  // namespace

const grpc_channel_filter grpc_message_decompress_filter = {
    DecompressStartTransportStreamOpBatch,
    grpc_channel_next_op,
    sizeof(CallData),
    DecompressInitCallElem,
    grpc_call_stack_ignore_set_pollset_or_pollset_set,
    DecompressDestroyCallElem,
    0,  // sizeof(ChannelData)
    DecompressInitChannelElem,
    DecompressDestroyChannelElem,
    grpc_channel_next_get_info,
    "message_decompress"};

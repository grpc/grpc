/*
 *
 * Copyright 2018 gRPC authors.
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

#ifndef GRPC_INTERNAL_CPP_EXT_FILTERS_CENSUS_CLIENT_FILTER_H
#define GRPC_INTERNAL_CPP_EXT_FILTERS_CENSUS_CLIENT_FILTER_H

#include <grpc/support/port_platform.h>

#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "src/cpp/ext/filters/census/channel_filter.h"
#include "src/cpp/ext/filters/census/context.h"

namespace grpc {

// A CallData class will be created for every grpc call within a channel. It is
// used to store data and methods specific to that call. CensusClientCallData is
// thread-compatible, however typically only 1 thread should be interacting with
// a call at a time.
class CensusClientCallData : public CallData {
 public:
  // Maximum size of trace context is sent on the wire.
  static constexpr uint32_t kMaxTraceContextLen = 64;
  // Maximum size of tags that are sent on the wire.
  static constexpr uint32_t kMaxTagsLen = 2048;

  CensusClientCallData()
      : recv_trailing_metadata_(nullptr),
        initial_on_done_recv_trailing_metadata_(nullptr),
        initial_on_done_recv_message_(nullptr),
        elapsed_time_(0),
        recv_message_(nullptr),
        recv_message_count_(0),
        sent_message_count_(0) {
    memset(&stats_bin_, 0, sizeof(grpc_linked_mdelem));
    memset(&tracing_bin_, 0, sizeof(grpc_linked_mdelem));
    memset(&path_, 0, sizeof(grpc_slice));
    memset(&on_done_recv_trailing_metadata_, 0, sizeof(grpc_closure));
    memset(&on_done_recv_message_, 0, sizeof(grpc_closure));
  }

  grpc_error_handle Init(grpc_call_element* elem,
                         const grpc_call_element_args* args) override;

  void Destroy(grpc_call_element* elem, const grpc_call_final_info* final_info,
               grpc_closure* then_call_closure) override;

  void StartTransportStreamOpBatch(grpc_call_element* elem,
                                   TransportStreamOpBatch* op) override;

  static void OnDoneRecvTrailingMetadataCb(void* user_data,
                                           grpc_error_handle error);

  static void OnDoneSendInitialMetadataCb(void* user_data,
                                          grpc_error_handle error);

  static void OnDoneRecvMessageCb(void* user_data, grpc_error_handle error);

 private:
  CensusContext context_;
  // Metadata elements for tracing and census stats data.
  grpc_linked_mdelem stats_bin_;
  grpc_linked_mdelem tracing_bin_;
  // Client method.
  absl::string_view method_;
  std::string qualified_method_;
  grpc_slice path_;
  // The recv trailing metadata callbacks.
  grpc_metadata_batch* recv_trailing_metadata_;
  grpc_closure* initial_on_done_recv_trailing_metadata_;
  grpc_closure on_done_recv_trailing_metadata_;
  // recv message
  grpc_closure* initial_on_done_recv_message_;
  grpc_closure on_done_recv_message_;
  // Start time (for measuring latency).
  absl::Time start_time_;
  // Server elapsed time in nanoseconds.
  uint64_t elapsed_time_;
  // The received message--may be null.
  grpc_core::OrphanablePtr<grpc_core::ByteStream>* recv_message_;
  // Number of messages in this RPC.
  uint64_t recv_message_count_;
  uint64_t sent_message_count_;
  // Buffer needed for grpc_slice to reference when adding trace context
  // metatdata to outgoing message.
  char tracing_buf_[kMaxTraceContextLen];
};

}  // namespace grpc

#endif /* GRPC_INTERNAL_CPP_EXT_FILTERS_CENSUS_CLIENT_FILTER_H */

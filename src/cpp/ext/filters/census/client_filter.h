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
      : elapsed_time_(0),
        recv_message_count_(0),
        sent_message_count_(0) {
    memset(&stats_bin_, 0, sizeof(grpc_linked_mdelem));
    memset(&tracing_bin_, 0, sizeof(grpc_linked_mdelem));
    memset(&path_, 0, sizeof(grpc_slice));
  }

  grpc_error* Init(grpc_call_element* elem,
                   const grpc_call_element_args* args) override;

  void Destroy(grpc_call_element* elem, const grpc_call_final_info* final_info,
               grpc_closure* then_call_closure) override;

  void StartTransportStreamOpBatch(grpc_call_element* elem,
                                   TransportStreamOpBatch* op) override;

  void StartTransportStreamRecvOpBatch(
      grpc_call_element* elem, grpc_transport_stream_recv_op_batch* batch,
      grpc_error* error) override;

  static void OnDoneSendInitialMetadataCb(void* user_data, grpc_error* error);

 private:
  CensusContext context_;
  // Metadata elements for tracing and census stats data.
  grpc_linked_mdelem stats_bin_;
  grpc_linked_mdelem tracing_bin_;
  // Client method.
  absl::string_view method_;
  std::string qualified_method_;
  grpc_slice path_;
  // Start time (for measuring latency).
  absl::Time start_time_;
  // Server elapsed time in nanoseconds.
  uint64_t elapsed_time_;
  // Number of messages in this RPC.
  uint64_t recv_message_count_;
  uint64_t sent_message_count_;
  // Buffer needed for grpc_slice to reference when adding trace context
  // metatdata to outgoing message.
  char tracing_buf_[kMaxTraceContextLen];
};

}  // namespace grpc

#endif /* GRPC_INTERNAL_CPP_EXT_FILTERS_CENSUS_CLIENT_FILTER_H */

//
//
// Copyright 2021 gRPC authors.
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

#ifndef GRPC_INTERNAL_CPP_EXT_FILTERS_OPEN_CENSUS_CALL_TRACER_H
#define GRPC_INTERNAL_CPP_EXT_FILTERS_OPEN_CENSUS_CALL_TRACER_H

#include <grpc/support/port_platform.h>

#include "src/core/lib/channel/call_tracer.h"
#include "src/cpp/ext/filters/census/context.h"

namespace grpc {

class OpenCensusCallTracer : public grpc_core::CallTracer {
 public:
  class OpenCensusCallAttemptTracer : public CallAttemptTracer {
   public:
    void RecordSendInitialMetadata(
        grpc_metadata_batch* /* send_initial_metadata */,
        uint32_t /* flags */) override {}
    void RecordOnDoneSendInitialMetadata(gpr_atm* /* peer_string */) override {}
    void RecordSendTrailingMetadata(
        grpc_metadata_batch* /* send_trailing_metadata */) override {}
    void RecordSendMessage(
        const grpc_core::ByteStream& /* send_message */) override {}
    void RecordReceivedInitialMetadata(
        grpc_metadata_batch* /* recv_initial_metadata */,
        uint32_t /* flags */) override {}
    void RecordReceivedMessage(
        const grpc_core::ByteStream& /* recv_message */) override {}
    void RecordReceivedTrailingMetadata(
        absl::Status /* status */,
        grpc_metadata_batch* /* recv_trailing_metadata */,
        const grpc_transport_stream_stats& /* transport_stream_stats */)
        override {}
    void RecordCancel(grpc_error_handle /* cancel_error */) override {}
    void RecordEnd(const gpr_timespec& /* latency */) override {}

    CensusContext* context() { return &context_; }

   private:
    CensusContext context_;
  };

  OpenCensusCallAttemptTracer* StartNewAttempt(
      bool /* is_transparent_retry */) override {
    return nullptr;
  }

  CensusContext* context() { return &context_; }

 private:
  CensusContext context_;
};

};  // namespace grpc

#endif  // GRPC_INTERNAL_CPP_EXT_FILTERS_OPEN_CENSUS_CALL_TRACER_H

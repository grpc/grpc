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
    OpenCensusCallAttemptTracer(OpenCensusCallTracer* parent,
                                uint64_t attempt_num, bool is_transparent_retry,
                                bool arena_allocated);
    void RecordSendInitialMetadata(
        grpc_metadata_batch* /* send_initial_metadata */,
        uint32_t /* flags */) override;
    void RecordOnDoneSendInitialMetadata(gpr_atm* /* peer_string */) override {}
    void RecordSendTrailingMetadata(
        grpc_metadata_batch* /* send_trailing_metadata */) override {}
    void RecordSendMessage(
        const grpc_core::ByteStream& /* send_message */) override;
    void RecordReceivedInitialMetadata(
        grpc_metadata_batch* /* recv_initial_metadata */,
        uint32_t /* flags */) override {}
    void RecordReceivedMessage(
        const grpc_core::ByteStream& /* recv_message */) override;
    void RecordReceivedTrailingMetadata(
        absl::Status /* status */, grpc_metadata_batch* recv_trailing_metadata,
        const grpc_transport_stream_stats& /* transport_stream_stats */)
        override;
    void RecordCancel(grpc_error_handle cancel_error) override;
    void RecordEnd(const gpr_timespec& /* latency */) override;

    CensusContext* context() { return &context_; }

   private:
    // Maximum size of trace context is sent on the wire.
    static constexpr uint32_t kMaxTraceContextLen = 64;
    // Maximum size of tags that are sent on the wire.
    static constexpr uint32_t kMaxTagsLen = 2048;
    OpenCensusCallTracer* parent_;
    const bool arena_allocated_;
    CensusContext context_;
    // Start time (for measuring latency).
    absl::Time start_time_;
    // Server elapsed time in nanoseconds.
    uint64_t elapsed_time_ = 0;
    // Number of messages in this RPC.
    uint64_t recv_message_count_ = 0;
    uint64_t sent_message_count_ = 0;
    // End status code
    absl::StatusCode status_code_;
  };

  explicit OpenCensusCallTracer(const grpc_call_element_args* args);
  ~OpenCensusCallTracer() override;

  void GenerateContext();
  OpenCensusCallAttemptTracer* StartNewAttempt(
      bool is_transparent_retry) override;

 private:
  const grpc_call_context_element* call_context_;
  // Client method.
  grpc_core::Slice path_;
  absl::string_view method_;
  CensusContext context_;
  grpc_core::Arena* arena_;
  grpc_core::Mutex mu_;
  // Non-transparent attempts per call
  uint64_t retries_ ABSL_GUARDED_BY(&mu_) = 0;
  // Transparent retries per call
  uint64_t transparent_retries_ ABSL_GUARDED_BY(&mu_) = 0;
  // Retry delay
  absl::Duration retry_delay_ ABSL_GUARDED_BY(&mu_);
  absl::Time time_at_last_attempt_end_ ABSL_GUARDED_BY(&mu_);
  uint64_t num_active_rpcs_ ABSL_GUARDED_BY(&mu_) = 0;
};

};  // namespace grpc

#endif  // GRPC_INTERNAL_CPP_EXT_FILTERS_OPEN_CENSUS_CALL_TRACER_H

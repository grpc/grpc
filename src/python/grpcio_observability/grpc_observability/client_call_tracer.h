// Copyright 2023 gRPC authors.
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

#include <stdint.h>
#include <queue>
#include <condition_variable>
#include <mutex>
#include <grpc/grpc.h>
#include <chrono>
#include <mutex>
#include <map>


#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "absl/synchronization/mutex.h"
#include "absl/strings/strip.h"
#include "absl/strings/escaping.h"
#include "absl/status/statusor.h"

#include "src/core/lib/channel/call_tracer.h"
#include "src/core/lib/channel/context.h"
#include "src/cpp/ext/gcp/observability_config.h"

#include "src/python/grpcio_observability/grpc_observability/python_census_context.h"
#include "src/python/grpcio_observability/grpc_observability/observability_main.h"
#include "src/python/grpcio_observability/grpc_observability/constants.h"

namespace grpc_observability {

class PythonOpenCensusCallTracer : public grpc_core::ClientCallTracer {
 public:
  class PythonOpenCensusCallAttemptTracer : public CallAttemptTracer {
   public:
    PythonOpenCensusCallAttemptTracer(PythonOpenCensusCallTracer* parent,
                                uint64_t attempt_num, bool is_transparent_retry);
    std::string TraceId() override {
      return absl::BytesToHexString(absl::string_view(context_.SpanContext().TraceId()));
    }

    std::string SpanId() override {
      return absl::BytesToHexString(absl::string_view(context_.SpanContext().SpanId()));
    }

    bool IsSampled() override { return context_.SpanContext().IsSampled(); }

    void RecordSendInitialMetadata(
        grpc_metadata_batch* send_initial_metadata) override;
    void RecordSendTrailingMetadata(
        grpc_metadata_batch* /*send_trailing_metadata*/) override {}
    void RecordSendMessage(
        const grpc_core::SliceBuffer& /*send_message*/) override;
    void RecordSendCompressedMessage(
        const grpc_core::SliceBuffer& /*send_compressed_message*/) override {}
    void RecordReceivedInitialMetadata(
        grpc_metadata_batch* /*recv_initial_metadata*/) override {}
    void RecordReceivedMessage(
        const grpc_core::SliceBuffer& /*recv_message*/) override;
    void RecordReceivedDecompressedMessage(
        const grpc_core::SliceBuffer& /*recv_decompressed_message*/) override {}
    void RecordReceivedTrailingMetadata(
        absl::Status status, grpc_metadata_batch* recv_trailing_metadata,
        const grpc_transport_stream_stats* transport_stream_stats) override;
    void RecordCancel(grpc_error_handle cancel_error) override;
    void RecordEnd(const gpr_timespec& /*latency*/) override;
    void RecordAnnotation(absl::string_view annotation) override;

   private:
    // Maximum size of trace context is sent on the wire.
    static constexpr uint32_t kMaxTraceContextLen = 64;
    // Maximum size of tags that are sent on the wire.
    static constexpr uint32_t kMaxTagsLen = 2048;
    PythonOpenCensusCallTracer* parent_;
    PythonCensusContext context_;
    // Start time (for measuring latency).
    absl::Time start_time_;
    // Number of messages in this RPC.
    uint64_t recv_message_count_ = 0;
    uint64_t sent_message_count_ = 0;
    // End status code
    absl::StatusCode status_code_;
  };

  explicit PythonOpenCensusCallTracer(
                                char* method, char* trace_id, char* parent_span_id,
                                bool tracing_enabled);
  ~PythonOpenCensusCallTracer() override;

  std::string TraceId() override {
    return absl::BytesToHexString(absl::string_view(context_.SpanContext().TraceId()));
  }

  std::string SpanId() override {
    return absl::BytesToHexString(absl::string_view(context_.SpanContext().SpanId()));
  }

  bool IsSampled() override { return context_.SpanContext().IsSampled(); }

  void GenerateContext();
  PythonOpenCensusCallAttemptTracer* StartNewAttempt(
      bool is_transparent_retry) override;

  void RecordAnnotation(absl::string_view annotation) override;

 private:
  PythonCensusContext CreateCensusContextForCallAttempt();

  // Client method.
  absl::string_view method_;
  PythonCensusContext context_;
  bool tracing_enabled_;
  mutable absl::Mutex mu_;
  // Non-transparent attempts per call
  uint64_t retries_ ABSL_GUARDED_BY(&mu_) = 0;
  // Transparent retries per call
  uint64_t transparent_retries_ ABSL_GUARDED_BY(&mu_) = 0;
  // Retry delay
  absl::Duration retry_delay_ ABSL_GUARDED_BY(&mu_);
  absl::Time time_at_last_attempt_end_ ABSL_GUARDED_BY(&mu_);
  uint64_t num_active_rpcs_ ABSL_GUARDED_BY(&mu_) = 0;
};

}  // namespace grpc_observability

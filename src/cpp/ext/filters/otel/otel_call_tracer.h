//
//
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
//
//

#ifndef GRPC_SRC_CPP_EXT_FILTERS_OTEL_OTEL_CALL_TRACER_H
#define GRPC_SRC_CPP_EXT_FILTERS_OTEL_OTEL_CALL_TRACER_H

#include <grpc/support/port_platform.h>

#include <stdint.h>

#include <string>

#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"

#include <grpc/support/time.h>

#include "src/core/lib/channel/call_tracer.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/transport.h"

namespace grpc {
namespace internal {

class OpenTelemetryCallTracer : public grpc_core::ClientCallTracer {
 public:
  class OpenTelemetryCallAttemptTracer : public CallAttemptTracer {
   public:
    OpenTelemetryCallAttemptTracer(OpenTelemetryCallTracer* parent,
                                   bool arena_allocated);

    std::string TraceId() override {
      // Not implemented
      return "";
    }

    std::string SpanId() override {
      // Not implemented
      return "";
    }

    bool IsSampled() override {
      // Not implemented
      return false;
    }

    void RecordSendInitialMetadata(
        grpc_metadata_batch* send_initial_metadata) override;
    void RecordSendTrailingMetadata(
        grpc_metadata_batch* /*send_trailing_metadata*/) override {}
    void RecordSendMessage(const grpc_core::SliceBuffer& send_message) override;
    void RecordSendCompressedMessage(
        const grpc_core::SliceBuffer& send_compressed_message) override;
    void RecordReceivedInitialMetadata(
        grpc_metadata_batch* /*recv_initial_metadata*/) override {}
    void RecordReceivedMessage(
        const grpc_core::SliceBuffer& recv_message) override;
    void RecordReceivedDecompressedMessage(
        const grpc_core::SliceBuffer& recv_decompressed_message) override;
    void RecordReceivedTrailingMetadata(
        absl::Status status, grpc_metadata_batch* recv_trailing_metadata,
        const grpc_transport_stream_stats* transport_stream_stats) override;
    void RecordCancel(grpc_error_handle cancel_error) override;
    void RecordEnd(const gpr_timespec& /*latency*/) override;
    void RecordAnnotation(absl::string_view annotation) override;

   private:
    const OpenTelemetryCallTracer* parent_;
    const bool arena_allocated_;
    // Start time (for measuring latency).
    absl::Time start_time_;
  };

  explicit OpenTelemetryCallTracer(grpc_core::Slice path,
                                   grpc_core::Arena* arena);
  ~OpenTelemetryCallTracer() override;

  std::string TraceId() override {
    // Not implemented
    return "";
  }

  std::string SpanId() override {
    // Not implemented
    return "";
  }

  bool IsSampled() override {
    // Not implemented
    return false;
  }

  OpenTelemetryCallAttemptTracer* StartNewAttempt(
      bool is_transparent_retry) override;
  void RecordAnnotation(absl::string_view annotation) override;

 private:
  // Client method.
  grpc_core::Slice path_;
  absl::string_view method_;
  grpc_core::Arena* arena_;
  grpc_core::Mutex mu_;
  // Non-transparent attempts per call
  uint64_t retries_ ABSL_GUARDED_BY(&mu_) = 0;
  // Transparent retries per call
  uint64_t transparent_retries_ ABSL_GUARDED_BY(&mu_) = 0;
};

}  // namespace internal
}  // namespace grpc

#endif  // GRPC_SRC_CPP_EXT_FILTERS_OTEL_OTEL_CALL_TRACER_H

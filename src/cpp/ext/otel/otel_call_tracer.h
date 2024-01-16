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

#ifndef GRPC_SRC_CPP_EXT_OTEL_OTEL_CALL_TRACER_H
#define GRPC_SRC_CPP_EXT_OTEL_OTEL_CALL_TRACER_H

#include <grpc/support/port_platform.h>

#include <stdint.h>

#include <memory>
#include <string>

#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"

#include <grpc/support/time.h>

#include "src/core/lib/channel/call_tracer.h"
#include "src/core/lib/channel/tcp_tracer.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/transport.h"
#include "src/cpp/ext/otel/otel_client_filter.h"
#include "src/cpp/ext/otel/otel_plugin.h"

namespace grpc {
namespace internal {

class OpenTelemetryCallTracer : public grpc_core::ClientCallTracer {
 public:
  class OpenTelemetryCallAttemptTracer : public CallAttemptTracer {
   public:
    OpenTelemetryCallAttemptTracer(const OpenTelemetryCallTracer* parent,
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
        grpc_metadata_batch* recv_initial_metadata) override;
    void RecordReceivedMessage(
        const grpc_core::SliceBuffer& recv_message) override;
    void RecordReceivedDecompressedMessage(
        const grpc_core::SliceBuffer& recv_decompressed_message) override;
    void RecordReceivedTrailingMetadata(
        absl::Status status, grpc_metadata_batch* /*recv_trailing_metadata*/,
        const grpc_transport_stream_stats* transport_stream_stats) override;
    void RecordCancel(grpc_error_handle cancel_error) override;
    void RecordEnd(const gpr_timespec& /*latency*/) override;
    void RecordAnnotation(absl::string_view /*annotation*/) override;
    void RecordAnnotation(const Annotation& /*annotation*/) override;
    std::shared_ptr<grpc_core::TcpTracerInterface> StartNewTcpTrace() override;
    void AddOptionalLabels(OptionalLabelComponent component,
                           std::shared_ptr<std::map<std::string, std::string>>
                               optional_labels) override;

   private:
    const OpenTelemetryCallTracer* parent_;
    const bool arena_allocated_;
    // Start time (for measuring latency).
    absl::Time start_time_;
    std::unique_ptr<LabelsIterable> injected_labels_;
    // The indices of the array correspond to the OptionalLabelComponent enum.
    std::array<std::shared_ptr<std::map<std::string, std::string>>,
               static_cast<size_t>(OptionalLabelComponent::kSize)>
        optional_labels_array_;
    std::vector<std::unique_ptr<LabelsIterable>>
        injected_labels_from_plugin_options_;
  };

  explicit OpenTelemetryCallTracer(OpenTelemetryClientFilter* parent,
                                   grpc_core::Slice path,
                                   grpc_core::Arena* arena,
                                   bool registered_method);
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
  void RecordAnnotation(absl::string_view /*annotation*/) override;
  void RecordAnnotation(const Annotation& /*annotation*/) override;

 private:
  absl::string_view MethodForStats() const;

  const OpenTelemetryClientFilter* parent_;
  // Client method.
  grpc_core::Slice path_;
  grpc_core::Arena* arena_;
  const bool registered_method_;
  grpc_core::Mutex mu_;
  // Non-transparent attempts per call
  uint64_t retries_ ABSL_GUARDED_BY(&mu_) = 0;
  // Transparent retries per call
  uint64_t transparent_retries_ ABSL_GUARDED_BY(&mu_) = 0;
};

}  // namespace internal
}  // namespace grpc

#endif  // GRPC_SRC_CPP_EXT_OTEL_OTEL_CALL_TRACER_H

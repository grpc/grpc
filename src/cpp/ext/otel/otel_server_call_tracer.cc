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

#include <grpc/support/port_platform.h>

#include "src/cpp/ext/otel/otel_server_call_tracer.h"

#include <algorithm>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "opentelemetry/context/context.h"
#include "opentelemetry/metrics/sync_instruments.h"

#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/transport.h"
#include "src/cpp/ext/otel/otel_plugin.h"

namespace grpc {
namespace internal {

namespace {

// OpenTelemetryServerCallTracer implementation

class OpenTelemetryServerCallTracer : public grpc_core::ServerCallTracer {
 public:
  OpenTelemetryServerCallTracer() : start_time_(absl::Now()) {
    // We don't have the peer labels at this point.
    if (OTelPluginState().labels_injector != nullptr) {
      labels_ = OTelPluginState().labels_injector->GetLocalLabels();
    }
  }

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

  // Please refer to `grpc_transport_stream_op_batch_payload` for details on
  // arguments.
  void RecordSendInitialMetadata(
      grpc_metadata_batch* send_initial_metadata) override {
    if (OTelPluginState().labels_injector != nullptr) {
      OTelPluginState().labels_injector->AddLabels(send_initial_metadata);
    }
  }

  void RecordSendTrailingMetadata(
      grpc_metadata_batch* /*send_trailing_metadata*/) override;

  void RecordSendMessage(const grpc_core::SliceBuffer& send_message) override {
    RecordAnnotation(
        absl::StrFormat("Send message: %ld bytes", send_message.Length()));
  }
  void RecordSendCompressedMessage(
      const grpc_core::SliceBuffer& send_compressed_message) override {
    RecordAnnotation(absl::StrFormat("Send compressed message: %ld bytes",
                                     send_compressed_message.Length()));
  }

  void RecordReceivedInitialMetadata(
      grpc_metadata_batch* recv_initial_metadata) override;

  void RecordReceivedMessage(
      const grpc_core::SliceBuffer& recv_message) override {
    RecordAnnotation(
        absl::StrFormat("Received message: %ld bytes", recv_message.Length()));
  }
  void RecordReceivedDecompressedMessage(
      const grpc_core::SliceBuffer& recv_decompressed_message) override {
    RecordAnnotation(absl::StrFormat("Received decompressed message: %ld bytes",
                                     recv_decompressed_message.Length()));
  }

  void RecordReceivedTrailingMetadata(
      grpc_metadata_batch* /*recv_trailing_metadata*/) override {}

  void RecordCancel(grpc_error_handle /*cancel_error*/) override {
    elapsed_time_ = absl::Now() - start_time_;
  }

  void RecordEnd(const grpc_call_final_info* final_info) override;

  void RecordAnnotation(absl::string_view /*annotation*/) override {
    // Not implemented
  }

  void RecordAnnotation(const Annotation& /*annotation*/) override {
    // Not implemented
  }

 private:
  absl::Time start_time_;
  absl::Duration elapsed_time_;
  std::vector<std::pair<std::string, std::string>> labels_;
};

void OpenTelemetryServerCallTracer::RecordReceivedInitialMetadata(
    grpc_metadata_batch* recv_initial_metadata) {
  const auto* path =
      recv_initial_metadata->get_pointer(grpc_core::HttpPathMetadata());
  auto method = absl::StripPrefix(path->as_string_view(), "/");
  const auto* authority =
      recv_initial_metadata->get_pointer(grpc_core::HttpAuthorityMetadata());
  // Override with host metadata if authority is absent.
  if (authority == nullptr) {
    authority = recv_initial_metadata->get_pointer(grpc_core::HostMetadata());
  }
  if (OTelPluginState().labels_injector != nullptr) {
    auto peer_labels =
        OTelPluginState().labels_injector->GetPeerLabels(recv_initial_metadata);
    labels_.insert(labels_.end(), std::make_move_iterator(peer_labels.begin()),
                   std::make_move_iterator(peer_labels.end()));
  }
  labels_.emplace_back(OTelMethodKey(), method);
  labels_.emplace_back(OTelAuthorityKey(), authority != nullptr
                                               ? authority->as_string_view()
                                               : "unknown");
  if (OTelPluginState().server.call.started != nullptr) {
    OTelPluginState().server.call.started->Add(1, labels_);
  }
}

void OpenTelemetryServerCallTracer::RecordSendTrailingMetadata(
    grpc_metadata_batch* /*send_trailing_metadata*/) {
  // We need to record the time when the trailing metadata was sent to
  // mark the completeness of the request.
  elapsed_time_ = absl::Now() - start_time_;
}

void OpenTelemetryServerCallTracer::RecordEnd(
    const grpc_call_final_info* final_info) {
  labels_.emplace_back(OTelStatusKey(),
                       absl::StatusCodeToString(static_cast<absl::StatusCode>(
                           final_info->final_status)));
  if (OTelPluginState().server.call.duration != nullptr) {
    OTelPluginState().server.call.duration->Record(
        absl::ToDoubleSeconds(elapsed_time_), labels_,
        opentelemetry::context::Context{});
  }
  if (OTelPluginState().server.call.sent_total_compressed_message_size !=
      nullptr) {
    OTelPluginState().server.call.sent_total_compressed_message_size->Record(
        final_info->stats.transport_stream_stats.outgoing.data_bytes, labels_,
        opentelemetry::context::Context{});
  }
  if (OTelPluginState().server.call.rcvd_total_compressed_message_size !=
      nullptr) {
    OTelPluginState().server.call.rcvd_total_compressed_message_size->Record(
        final_info->stats.transport_stream_stats.incoming.data_bytes, labels_,
        opentelemetry::context::Context{});
  }
}

}  // namespace

//
// OpenTelemetryServerCallTracerFactory
//

grpc_core::ServerCallTracer*
OpenTelemetryServerCallTracerFactory::CreateNewServerCallTracer(
    grpc_core::Arena* arena) {
  return arena->ManagedNew<OpenTelemetryServerCallTracer>();
}

}  // namespace internal
}  // namespace grpc

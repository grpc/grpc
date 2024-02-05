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

#include <array>
#include <memory>
#include <string>
#include <utility>

#include "absl/functional/any_invocable.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/optional.h"
#include "absl/types/span.h"
#include "opentelemetry/context/context.h"
#include "opentelemetry/metrics/sync_instruments.h"

#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/status_util.h"
#include "src/core/lib/channel/tcp_tracer.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/transport.h"
#include "src/cpp/ext/otel/key_value_iterable.h"
#include "src/cpp/ext/otel/otel_plugin.h"

namespace grpc {
namespace internal {

namespace {

// OpenTelemetryServerCallTracer implementation

class OpenTelemetryServerCallTracer : public grpc_core::ServerCallTracer {
 public:
  explicit OpenTelemetryServerCallTracer(const grpc_core::ChannelArgs& args)
      : start_time_(absl::Now()),
        active_plugin_options_view_(
            ActivePluginOptionsView::MakeForServer(args)),
        injected_labels_from_plugin_options_(
            OpenTelemetryPluginState().plugin_options.size()) {}

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
    active_plugin_options_view_.ForEach(
        [&](const InternalOpenTelemetryPluginOption& plugin_option,
            size_t index) {
          auto* labels_injector = plugin_option.labels_injector();
          if (labels_injector != nullptr) {
            labels_injector->AddLabels(
                send_initial_metadata,
                injected_labels_from_plugin_options_[index].get());
          }
          return true;
        });
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
  std::shared_ptr<grpc_core::TcpTracerInterface> StartNewTcpTrace() override {
    // No TCP trace.
    return nullptr;
  }

 private:
  absl::string_view MethodForStats() const {
    absl::string_view method = absl::StripPrefix(path_.as_string_view(), "/");
    if (registered_method_ ||
        (OpenTelemetryPluginState().generic_method_attribute_filter !=
             nullptr &&
         OpenTelemetryPluginState().generic_method_attribute_filter(method))) {
      return method;
    }
    return "other";
  }

  absl::Time start_time_;
  absl::Duration elapsed_time_;
  grpc_core::Slice path_;
  bool registered_method_;
  ActivePluginOptionsView active_plugin_options_view_;
  // TODO(yashykt): It's wasteful to do this per call. When we re-haul the stats
  // infrastructure, this should move to be done per server.
  std::vector<std::unique_ptr<LabelsIterable>>
      injected_labels_from_plugin_options_;
};

void OpenTelemetryServerCallTracer::RecordReceivedInitialMetadata(
    grpc_metadata_batch* recv_initial_metadata) {
  path_ =
      recv_initial_metadata->get_pointer(grpc_core::HttpPathMetadata())->Ref();
  active_plugin_options_view_.ForEach(
      [&](const InternalOpenTelemetryPluginOption& plugin_option,
          size_t index) {
        auto* labels_injector = plugin_option.labels_injector();
        if (labels_injector != nullptr) {
          injected_labels_from_plugin_options_[index] =
              labels_injector->GetLabels(recv_initial_metadata);
        }
        return true;
      });
  registered_method_ =
      recv_initial_metadata->get(grpc_core::GrpcRegisteredMethod())
          .value_or(nullptr) != nullptr;
  std::array<std::pair<absl::string_view, absl::string_view>, 1>
      additional_labels = {{{OpenTelemetryMethodKey(), MethodForStats()}}};
  if (OpenTelemetryPluginState().server.call.started != nullptr) {
    // We might not have all the injected labels that we want at this point, so
    // avoid recording a subset of injected labels here.
    OpenTelemetryPluginState().server.call.started->Add(
        1, KeyValueIterable(/*injected_labels_from_plugin_options=*/{},
                            additional_labels,
                            /*active_plugin_options_view=*/nullptr, {},
                            /*is_client=*/false));
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
  std::array<std::pair<absl::string_view, absl::string_view>, 2>
      additional_labels = {
          {{OpenTelemetryMethodKey(), MethodForStats()},
           {OpenTelemetryStatusKey(),
            grpc_status_code_to_string(final_info->final_status)}}};
  // Currently we do not have any optional labels on the server side.
  KeyValueIterable labels(
      injected_labels_from_plugin_options_, additional_labels,
      /*active_plugin_options_view=*/nullptr, /*optional_labels_span=*/{},
      /*is_client=*/false);
  if (OpenTelemetryPluginState().server.call.duration != nullptr) {
    OpenTelemetryPluginState().server.call.duration->Record(
        absl::ToDoubleSeconds(elapsed_time_), labels,
        opentelemetry::context::Context{});
  }
  if (OpenTelemetryPluginState()
          .server.call.sent_total_compressed_message_size != nullptr) {
    OpenTelemetryPluginState()
        .server.call.sent_total_compressed_message_size->Record(
            final_info->stats.transport_stream_stats.outgoing.data_bytes,
            labels, opentelemetry::context::Context{});
  }
  if (OpenTelemetryPluginState()
          .server.call.rcvd_total_compressed_message_size != nullptr) {
    OpenTelemetryPluginState()
        .server.call.rcvd_total_compressed_message_size->Record(
            final_info->stats.transport_stream_stats.incoming.data_bytes,
            labels, opentelemetry::context::Context{});
  }
}

}  // namespace

//
// OpenTelemetryServerCallTracerFactory
//

grpc_core::ServerCallTracer*
OpenTelemetryServerCallTracerFactory::CreateNewServerCallTracer(
    grpc_core::Arena* arena, const grpc_core::ChannelArgs& args) {
  return arena->ManagedNew<OpenTelemetryServerCallTracer>(args);
}

bool OpenTelemetryServerCallTracerFactory::IsServerTraced(
    const grpc_core::ChannelArgs& args) {
  // Return true only if there is no server selector registered or if the server
  // selector returns true.
  return OpenTelemetryPluginState().server_selector == nullptr ||
         OpenTelemetryPluginState().server_selector(args);
}

}  // namespace internal
}  // namespace grpc

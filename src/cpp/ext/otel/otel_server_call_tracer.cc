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

#include "src/cpp/ext/otel/otel_server_call_tracer.h"

#include <array>
#include <memory>
#include <string>
#include <utility>

#include "absl/functional/any_invocable.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/optional.h"
#include "absl/types/span.h"
#include "opentelemetry/context/context.h"
#include "opentelemetry/metrics/sync_instruments.h"

#include <grpc/support/port_platform.h>

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

void OpenTelemetryPlugin::ServerCallTracer::RecordReceivedInitialMetadata(
    grpc_metadata_batch* recv_initial_metadata) {
  path_ =
      recv_initial_metadata->get_pointer(grpc_core::HttpPathMetadata())->Ref();
  scope_config_->active_plugin_options_view().ForEach(
      [&](const InternalOpenTelemetryPluginOption& plugin_option,
          size_t index) {
        auto* labels_injector = plugin_option.labels_injector();
        if (labels_injector != nullptr) {
          injected_labels_from_plugin_options_[index] =
              labels_injector->GetLabels(recv_initial_metadata);
        }
        return true;
      },
      otel_plugin_);
  registered_method_ =
      recv_initial_metadata->get(grpc_core::GrpcRegisteredMethod())
          .value_or(nullptr) != nullptr;
  std::array<std::pair<absl::string_view, absl::string_view>, 1>
      additional_labels = {{{OpenTelemetryMethodKey(), MethodForStats()}}};
  if (otel_plugin_->server_.call.started != nullptr) {
    // We might not have all the injected labels that we want at this point, so
    // avoid recording a subset of injected labels here.
    otel_plugin_->server_.call.started->Add(
        1, KeyValueIterable(/*injected_labels_from_plugin_options=*/{},
                            additional_labels,
                            /*active_plugin_options_view=*/nullptr, {},
                            /*is_client=*/false, otel_plugin_));
  }
}

void OpenTelemetryPlugin::ServerCallTracer::RecordSendInitialMetadata(
    grpc_metadata_batch* send_initial_metadata) {
  scope_config_->active_plugin_options_view().ForEach(
      [&](const InternalOpenTelemetryPluginOption& plugin_option,
          size_t index) {
        auto* labels_injector = plugin_option.labels_injector();
        if (labels_injector != nullptr) {
          labels_injector->AddLabels(
              send_initial_metadata,
              injected_labels_from_plugin_options_[index].get());
        }
        return true;
      },
      otel_plugin_);
}

void OpenTelemetryPlugin::ServerCallTracer::RecordSendTrailingMetadata(
    grpc_metadata_batch* /*send_trailing_metadata*/) {
  // We need to record the time when the trailing metadata was sent to
  // mark the completeness of the request.
  elapsed_time_ = absl::Now() - start_time_;
}

void OpenTelemetryPlugin::ServerCallTracer::RecordEnd(
    const grpc_call_final_info* final_info) {
  std::array<std::pair<absl::string_view, absl::string_view>, 2>
      additional_labels = {
          {{OpenTelemetryMethodKey(), MethodForStats()},
           {OpenTelemetryStatusKey(),
            grpc_status_code_to_string(final_info->final_status)}}};
  // Currently we do not have any optional labels on the server side.
  KeyValueIterable labels(
      injected_labels_from_plugin_options_, additional_labels,
      /*active_plugin_options_view=*/nullptr, /*optional_labels=*/{},
      /*is_client=*/false, otel_plugin_);
  if (otel_plugin_->server_.call.duration != nullptr) {
    otel_plugin_->server_.call.duration->Record(
        absl::ToDoubleSeconds(elapsed_time_), labels,
        opentelemetry::context::Context{});
  }
  if (otel_plugin_->server_.call.sent_total_compressed_message_size !=
      nullptr) {
    otel_plugin_->server_.call.sent_total_compressed_message_size->Record(
        final_info->stats.transport_stream_stats.outgoing.data_bytes, labels,
        opentelemetry::context::Context{});
  }
  if (otel_plugin_->server_.call.rcvd_total_compressed_message_size !=
      nullptr) {
    otel_plugin_->server_.call.rcvd_total_compressed_message_size->Record(
        final_info->stats.transport_stream_stats.incoming.data_bytes, labels,
        opentelemetry::context::Context{});
  }
}

}  // namespace internal
}  // namespace grpc

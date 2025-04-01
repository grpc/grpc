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

#include <grpc/support/port_platform.h>

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/functional/any_invocable.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "opentelemetry/context/context.h"
#include "opentelemetry/metrics/sync_instruments.h"
#include "src/core/call/metadata_batch.h"
#include "src/core/call/status_util.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/telemetry/tcp_tracer.h"
#include "src/cpp/ext/otel/key_value_iterable.h"
#include "src/cpp/ext/otel/otel_plugin.h"

namespace grpc {
namespace internal {

void OpenTelemetryPluginImpl::ServerCallTracer::RecordReceivedInitialMetadata(
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
  if (otel_plugin_->tracer_ != nullptr) {
    GrpcTextMapCarrier carrier(recv_initial_metadata);
    opentelemetry::context::Context context;
    context = otel_plugin_->text_map_propagator_->Extract(carrier, context);
    opentelemetry::trace::StartSpanOptions options;
    options.parent = context;
    span_ = otel_plugin_->tracer_->StartSpan(
        absl::StrCat("Recv.", GetMethodFromPath(path_)), options);
    // We are intentionally reusing census_context to save opentelemetry's Span
    // on the context to avoid introducing a new type for opentelemetry inside
    // gRPC Core. There's no risk of collisions since we do not allow multiple
    // tracing systems active for the same call.
    grpc_core::SetContext<census_context>(
        reinterpret_cast<census_context*>(span_.get()));
  }
}

void OpenTelemetryPluginImpl::ServerCallTracer::RecordReceivedMessage(
    const grpc_core::Message& recv_message) {
  if (span_ != nullptr) {
    std::array<std::pair<opentelemetry::nostd::string_view,
                         opentelemetry::common::AttributeValue>,
               2>
        attributes = {
            std::pair("sequence-number",
                      opentelemetry::common::AttributeValue(recv_seq_num_++)),
            std::pair(recv_message.flags() & GRPC_WRITE_INTERNAL_COMPRESS
                          ? "message-size-compressed"
                          : "message-size",
                      opentelemetry::common::AttributeValue(
                          recv_message.payload()->Length()))};
    span_->AddEvent(recv_message.flags() & GRPC_WRITE_INTERNAL_COMPRESS
                        ? "Inbound compressed message"
                        : "Inbound message",
                    attributes);
  }
}

void OpenTelemetryPluginImpl::ServerCallTracer::
    RecordReceivedDecompressedMessage(
        const grpc_core::Message& recv_decompressed_message) {
  if (span_ != nullptr) {
    std::array<std::pair<opentelemetry::nostd::string_view,
                         opentelemetry::common::AttributeValue>,
               2>
        attributes = {
            std::pair("sequence-number",
                      opentelemetry::common::AttributeValue(recv_seq_num_ - 1)),
            std::pair("message-size",
                      opentelemetry::common::AttributeValue(
                          recv_decompressed_message.payload()->Length()))};
    span_->AddEvent("Inbound message", attributes);
  }
}

void OpenTelemetryPluginImpl::ServerCallTracer::RecordSendInitialMetadata(
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

void OpenTelemetryPluginImpl::ServerCallTracer::RecordSendMessage(
    const grpc_core::Message& send_message) {
  if (span_ != nullptr) {
    std::array<std::pair<opentelemetry::nostd::string_view,
                         opentelemetry::common::AttributeValue>,
               2>
        attributes = {
            std::pair("sequence-number", send_seq_num_++),
            std::pair("message-size", send_message.payload()->Length())};
    span_->AddEvent("Outbound message", attributes);
  }
}
void OpenTelemetryPluginImpl::ServerCallTracer::RecordSendCompressedMessage(
    const grpc_core::Message& send_compressed_message) {
  if (span_ != nullptr) {
    std::array<std::pair<opentelemetry::nostd::string_view,
                         opentelemetry::common::AttributeValue>,
               2>
        attributes = {
            std::pair("sequence-number",
                      opentelemetry::common::AttributeValue(send_seq_num_ - 1)),
            std::pair("message-size-compressed",
                      opentelemetry::common::AttributeValue(
                          send_compressed_message.payload()->Length()))};
    span_->AddEvent("Outbound message compressed", attributes);
  }
}

void OpenTelemetryPluginImpl::ServerCallTracer::RecordSendTrailingMetadata(
    grpc_metadata_batch* /*send_trailing_metadata*/) {
  // We need to record the time when the trailing metadata was sent to
  // mark the completeness of the request.
  elapsed_time_ = absl::Now() - start_time_;
}

void OpenTelemetryPluginImpl::ServerCallTracer::RecordEnd(
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
        grpc_core::IsCallTracerInTransportEnabled()
            ? outgoing_bytes_.load()
            : final_info->stats.transport_stream_stats.outgoing.data_bytes,
        labels, opentelemetry::context::Context{});
  }
  if (otel_plugin_->server_.call.rcvd_total_compressed_message_size !=
      nullptr) {
    otel_plugin_->server_.call.rcvd_total_compressed_message_size->Record(
        grpc_core::IsCallTracerInTransportEnabled()
            ? incoming_bytes_.load()
            : final_info->stats.transport_stream_stats.incoming.data_bytes,
        labels, opentelemetry::context::Context{});
  }
  if (span_ != nullptr) {
    if (final_info->final_status == GRPC_STATUS_OK) {
      span_->SetStatus(opentelemetry::trace::StatusCode::kOk);
    } else {
      span_->SetStatus(
          opentelemetry::trace::StatusCode::kError,
          absl::Status(static_cast<absl::StatusCode>(final_info->final_status),
                       final_info->error_string)
              .ToString());
    }
    span_->End();
  }
}

void OpenTelemetryPluginImpl::ServerCallTracer::RecordIncomingBytes(
    const TransportByteSize& transport_byte_size) {
  incoming_bytes_.fetch_add(transport_byte_size.data_bytes);
}

void OpenTelemetryPluginImpl::ServerCallTracer::RecordOutgoingBytes(
    const TransportByteSize& transport_byte_size) {
  outgoing_bytes_.fetch_add(transport_byte_size.data_bytes);
}

void OpenTelemetryPluginImpl::ServerCallTracer::RecordAnnotation(
    absl::string_view annotation) {
  if (span_ != nullptr) {
    span_->AddEvent(AbslStringViewToNoStdStringView(annotation));
  }
}

}  // namespace internal
}  // namespace grpc

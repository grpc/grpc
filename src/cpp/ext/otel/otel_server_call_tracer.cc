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

#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "opentelemetry/context/context.h"
#include "opentelemetry/metrics/sync_instruments.h"
#include "src/core/call/metadata_batch.h"
#include "src/core/call/status_util.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/event_engine/utils.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/telemetry/tcp_tracer.h"
#include "src/core/util/grpc_check.h"
#include "src/cpp/ext/otel/key_value_iterable.h"
#include "src/cpp/ext/otel/otel_plugin.h"
#include "absl/functional/any_invocable.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/span.h"

namespace grpc {
namespace internal {

class OpenTelemetryPluginImpl::ServerCallTracerInterface::TcpCallTracer
    : public grpc_core::TcpCallTracer {
 public:
  explicit TcpCallTracer(grpc_core::RefCountedPtr<
                         OpenTelemetryPluginImpl::ServerCallTracerInterface>
                             server_call_tracer)
      : server_call_tracer_(server_call_tracer) {
    // Take a ref on the call if tracing is enabled, since TCP traces might
    // arrive after all the other refs on the call are gone.
    server_call_tracer_->arena_->GetContext<grpc_core::Call>()->InternalRef(
        "OpenTelemetryPluginImpl::ServerCallTracerInterface::TcpCallTracer");
  }

  ~TcpCallTracer() override {
    grpc_core::ExecCtx exec_ctx;
    auto* arena = server_call_tracer_->arena_;
    // The ServerCallTracerInterface is allocated on the arena and hence needs
    // to be reset before unreffing the call.
    server_call_tracer_.reset();
    arena->GetContext<grpc_core::Call>()->InternalUnref(
        "OpenTelemetryPluginImpl::ServerCallTracerInterface::~TcpCallTracer");
  }

  void RecordEvent(grpc_event_engine::experimental::internal::WriteEvent type,
                   absl::Time time, size_t byte_offset,
                   const std::vector<TcpEventMetric>& metrics) override {
    server_call_tracer_->RecordAnnotation(
        absl::StrCat(
            "TCP: ", grpc_event_engine::experimental::WriteEventToString(type),
            " byte_offset=", byte_offset, " ",
            grpc_core::TcpCallTracer::TcpEventMetricsToString(metrics)),
        time);
  }

 private:
  grpc_core::RefCountedPtr<OpenTelemetryPluginImpl::ServerCallTracerInterface>
      server_call_tracer_;
};

OpenTelemetryPluginImpl::ServerCallTracerInterface::ServerCallTracerInterface(
    OpenTelemetryPluginImpl* otel_plugin, grpc_core::Arena* arena,
    std::shared_ptr<OpenTelemetryPluginImpl::ServerScopeConfig> scope_config)
    : start_time_(absl::Now()),
      injected_labels_from_plugin_options_(
          otel_plugin->plugin_options().size()),
      otel_plugin_(otel_plugin),
      arena_(arena),
      scope_config_(std::move(scope_config)) {}

OpenTelemetryPluginImpl::ServerCallTracerInterface::
    ~ServerCallTracerInterface() {
  if (span_ != nullptr) {
    span_->End();
  }
}

void OpenTelemetryPluginImpl::ServerCallTracerInterface::
    RecordReceivedInitialMetadata(grpc_metadata_batch* recv_initial_metadata) {
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
    opentelemetry::trace::StartSpanOptions options;
    if (otel_plugin_->text_map_propagator_ != nullptr) {
      GrpcTextMapCarrier carrier(recv_initial_metadata);
      opentelemetry::context::Context context;
      context = otel_plugin_->text_map_propagator_->Extract(carrier, context);
      options.parent = context;
    }
    span_ = otel_plugin_->tracer_->StartSpan(
        absl::StrCat("Recv.", GetMethodFromPath(path_)), options);
    // We are intentionally reusing census_context to save opentelemetry's Span
    // on the context to avoid introducing a new type for opentelemetry inside
    // gRPC Core. There's no risk of collisions since we do not allow multiple
    // tracing systems active for the same call.
    grpc_core::SetContext<census_context>(
        reinterpret_cast<census_context*>(span_.get()));
    if (IsSampled()) {
      arena_->GetContext<grpc_core::Call>()->set_traced(true);
    }
  }
}

void OpenTelemetryPluginImpl::ServerCallTracerInterface::RecordReceivedMessage(
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

void OpenTelemetryPluginImpl::ServerCallTracerInterface::
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

void OpenTelemetryPluginImpl::ServerCallTracerInterface::
    RecordSendInitialMetadata(grpc_metadata_batch* send_initial_metadata) {
  GRPC_CHECK(
      !grpc_core::IsCallTracerSendInitialMetadataIsAnAnnotationEnabled());
  MutateSendInitialMetadata(send_initial_metadata);
}

void OpenTelemetryPluginImpl::ServerCallTracerInterface::
    MutateSendInitialMetadata(grpc_metadata_batch* send_initial_metadata) {
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

void OpenTelemetryPluginImpl::ServerCallTracerInterface::RecordSendMessage(
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

void OpenTelemetryPluginImpl::ServerCallTracerInterface::
    RecordSendCompressedMessage(
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

void OpenTelemetryPluginImpl::ServerCallTracerInterface::
    RecordSendTrailingMetadata(
        grpc_metadata_batch* /*send_trailing_metadata*/) {
  // We need to record the time when the trailing metadata was sent to
  // mark the completeness of the request.
  elapsed_time_ = absl::Now() - start_time_;
}

void OpenTelemetryPluginImpl::ServerCallTracerInterface::RecordEnd(
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
  }
  Unref(DEBUG_LOCATION, "RecordEnd");
}

void OpenTelemetryPluginImpl::ServerCallTracerInterface::RecordIncomingBytes(
    const TransportByteSize& transport_byte_size) {
  incoming_bytes_.fetch_add(transport_byte_size.data_bytes);
}

void OpenTelemetryPluginImpl::ServerCallTracerInterface::RecordOutgoingBytes(
    const TransportByteSize& transport_byte_size) {
  outgoing_bytes_.fetch_add(transport_byte_size.data_bytes);
}

void OpenTelemetryPluginImpl::ServerCallTracerInterface::RecordAnnotation(
    const Annotation& annotation) {
  if (annotation.type() == grpc_core::CallTracerAnnotationInterface::
                               AnnotationType::kSendInitialMetadata) {
    // Otel does not have any immutable tracing for send initial metadata.
    // All Otel work for send initial metadata is mutation, which is handled in
    // MutateSendInitialMetadata.
    return;
  }
  RecordAnnotation(annotation.ToString());
}

void OpenTelemetryPluginImpl::ServerCallTracerInterface::RecordAnnotation(
    absl::string_view annotation) {
  if (span_ != nullptr) {
    span_->AddEvent(AbslStringViewToNoStdStringView(annotation));
  }
}

void OpenTelemetryPluginImpl::ServerCallTracerInterface::RecordAnnotation(
    absl::string_view annotation, absl::Time time) {
  if (span_ != nullptr) {
    span_->AddEvent(AbslStringViewToNoStdStringView(annotation),
                    absl::ToChronoTime(time));
  }
}

std::shared_ptr<grpc_core::TcpCallTracer>
OpenTelemetryPluginImpl::ServerCallTracerInterface::StartNewTcpTrace() {
  if (span_ != nullptr) {
    return std::make_shared<TcpCallTracer>(
        Ref(DEBUG_LOCATION, "StartNewTcpTrace"));
  }
  return nullptr;
}

}  // namespace internal
}  // namespace grpc

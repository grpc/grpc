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

#include "src/cpp/ext/otel/otel_client_call_tracer.h"

#include <grpc/status.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/time.h>
#include <stdint.h>

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/functional/any_invocable.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "opentelemetry/context/context.h"
#include "opentelemetry/metrics/sync_instruments.h"
#include "opentelemetry/trace/context.h"
#include "opentelemetry/trace/tracer.h"
#include "src/core/call/metadata_batch.h"
#include "src/core/call/status_util.h"
#include "src/core/client_channel/client_channel_filter.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/surface/call.h"
#include "src/core/telemetry/tcp_tracer.h"
#include "src/core/util/sync.h"
#include "src/cpp/ext/otel/key_value_iterable.h"
#include "src/cpp/ext/otel/otel_plugin.h"

namespace grpc {
namespace internal {

//
// OpenTelemetryPluginImpl::ClientCallTracer::CallAttemptTracer
//

OpenTelemetryPluginImpl::ClientCallTracer::CallAttemptTracer::CallAttemptTracer(
    const OpenTelemetryPluginImpl::ClientCallTracer* parent,
    uint64_t attempt_num, bool is_transparent_retry, bool arena_allocated)
    : parent_(parent),
      arena_allocated_(arena_allocated),
      start_time_(absl::Now()) {
  if (parent_->otel_plugin_->client_.attempt.started != nullptr) {
    std::array<std::pair<absl::string_view, absl::string_view>, 2>
        additional_labels = {
            {{OpenTelemetryMethodKey(), parent_->MethodForStats()},
             {OpenTelemetryTargetKey(),
              parent_->scope_config_->filtered_target()}}};
    // We might not have all the injected labels that we want at this point, so
    // avoid recording a subset of injected labels here.
    parent_->otel_plugin_->client_.attempt.started->Add(
        1, KeyValueIterable(
               /*injected_labels_from_plugin_options=*/{}, additional_labels,
               /*active_plugin_options_view=*/nullptr,
               /*optional_labels=*/{},
               /*is_client=*/true, parent_->otel_plugin_));
  }
  if (parent_->otel_plugin_->tracer_ != nullptr) {
    std::array<std::pair<opentelemetry::nostd::string_view,
                         opentelemetry::common::AttributeValue>,
               2>
        attributes = {std::pair("previous-rpc-attempts", attempt_num),
                      std::pair("transparent-retry", is_transparent_retry)};
    opentelemetry::trace::StartSpanOptions options;
    options.parent = parent_->span_->GetContext();
    span_ = parent_->otel_plugin_->tracer_->StartSpan(
        absl::StrCat("Attempt.", GetMethodFromPath(parent_->path_)), attributes,
        options);
  }
}

void OpenTelemetryPluginImpl::ClientCallTracer::CallAttemptTracer::
    RecordReceivedInitialMetadata(grpc_metadata_batch* recv_initial_metadata) {
  if (recv_initial_metadata != nullptr &&
      recv_initial_metadata->get(grpc_core::GrpcTrailersOnly())
          .value_or(false)) {
    is_trailers_only_ = true;
    return;
  }
  PopulateLabelInjectors(recv_initial_metadata);
}

void OpenTelemetryPluginImpl::ClientCallTracer::CallAttemptTracer::
    RecordSendInitialMetadata(grpc_metadata_batch* send_initial_metadata) {
  parent_->scope_config_->active_plugin_options_view().ForEach(
      [&](const InternalOpenTelemetryPluginOption& plugin_option,
          size_t /*index*/) {
        auto* labels_injector = plugin_option.labels_injector();
        if (labels_injector != nullptr) {
          labels_injector->AddLabels(send_initial_metadata, nullptr);
        }
        return true;
      },
      parent_->otel_plugin_);
  if (span_ != nullptr) {
    GrpcTextMapCarrier carrier(send_initial_metadata);
    opentelemetry::context::Context context;
    context = opentelemetry::trace::SetSpan(context, span_);
    parent_->otel_plugin_->text_map_propagator_->Inject(carrier, context);
  }
}

void OpenTelemetryPluginImpl::ClientCallTracer::CallAttemptTracer::
    RecordSendMessage(const grpc_core::Message& send_message) {
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

void OpenTelemetryPluginImpl::ClientCallTracer::CallAttemptTracer::
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

void OpenTelemetryPluginImpl::ClientCallTracer::CallAttemptTracer::
    RecordReceivedMessage(const grpc_core::Message& recv_message) {
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

void OpenTelemetryPluginImpl::ClientCallTracer::CallAttemptTracer::
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

void OpenTelemetryPluginImpl::ClientCallTracer::CallAttemptTracer::
    RecordReceivedTrailingMetadata(
        absl::Status status, grpc_metadata_batch* recv_trailing_metadata,
        const grpc_transport_stream_stats* transport_stream_stats) {
  if (is_trailers_only_) {
    PopulateLabelInjectors(recv_trailing_metadata);
  }
  std::array<std::pair<absl::string_view, absl::string_view>, 3>
      additional_labels = {
          {{OpenTelemetryMethodKey(), parent_->MethodForStats()},
           {OpenTelemetryTargetKey(),
            parent_->scope_config_->filtered_target()},
           {OpenTelemetryStatusKey(),
            grpc_status_code_to_string(
                static_cast<grpc_status_code>(status.code()))}}};
  KeyValueIterable labels(
      injected_labels_from_plugin_options_, additional_labels,
      &parent_->scope_config_->active_plugin_options_view(), optional_labels_,
      /*is_client=*/true, parent_->otel_plugin_);
  if (parent_->otel_plugin_->client_.attempt.duration != nullptr) {
    parent_->otel_plugin_->client_.attempt.duration->Record(
        absl::ToDoubleSeconds(absl::Now() - start_time_), labels,
        opentelemetry::context::Context{});
  }
  uint64_t outgoing_bytes = 0;
  uint64_t incoming_bytes = 0;
  if (grpc_core::IsCallTracerInTransportEnabled()) {
    outgoing_bytes = outgoing_bytes_.load();
    incoming_bytes = incoming_bytes_.load();
  } else if (transport_stream_stats != nullptr) {
    outgoing_bytes = transport_stream_stats->outgoing.data_bytes;
    incoming_bytes = transport_stream_stats->incoming.data_bytes;
  }
  if (parent_->otel_plugin_->client_.attempt
          .sent_total_compressed_message_size != nullptr) {
    parent_->otel_plugin_->client_.attempt.sent_total_compressed_message_size
        ->Record(outgoing_bytes, labels, opentelemetry::context::Context{});
  }
  if (parent_->otel_plugin_->client_.attempt
          .rcvd_total_compressed_message_size != nullptr) {
    parent_->otel_plugin_->client_.attempt.rcvd_total_compressed_message_size
        ->Record(incoming_bytes, labels, opentelemetry::context::Context{});
  }
  if (span_ != nullptr) {
    if (status.ok()) {
      span_->SetStatus(opentelemetry::trace::StatusCode::kOk);
    } else {
      span_->SetStatus(opentelemetry::trace::StatusCode::kError,
                       status.ToString());
    }
  }
}

void OpenTelemetryPluginImpl::ClientCallTracer::CallAttemptTracer::
    RecordIncomingBytes(const TransportByteSize& transport_byte_size) {
  incoming_bytes_.fetch_add(transport_byte_size.data_bytes);
}

void OpenTelemetryPluginImpl::ClientCallTracer::CallAttemptTracer::
    RecordOutgoingBytes(const TransportByteSize& transport_byte_size) {
  outgoing_bytes_.fetch_add(transport_byte_size.data_bytes);
}

void OpenTelemetryPluginImpl::ClientCallTracer::CallAttemptTracer::RecordCancel(
    absl::Status /*cancel_error*/) {}

void OpenTelemetryPluginImpl::ClientCallTracer::CallAttemptTracer::RecordEnd() {
  if (span_ != nullptr) {
    span_->End();
  }
  if (arena_allocated_) {
    this->~CallAttemptTracer();
  } else {
    delete this;
  }
}

void OpenTelemetryPluginImpl::ClientCallTracer::CallAttemptTracer::
    RecordAnnotation(absl::string_view annotation) {
  if (span_ != nullptr) {
    span_->AddEvent(AbslStringViewToNoStdStringView(annotation));
  }
}

void OpenTelemetryPluginImpl::ClientCallTracer::CallAttemptTracer::
    RecordAnnotation(const Annotation& /*annotation*/) {
  // Not implemented
}

std::shared_ptr<grpc_core::TcpCallTracer> OpenTelemetryPluginImpl::
    ClientCallTracer::CallAttemptTracer::StartNewTcpTrace() {
  // No TCP trace.
  return nullptr;
}

void OpenTelemetryPluginImpl::ClientCallTracer::CallAttemptTracer::
    SetOptionalLabel(OptionalLabelKey key,
                     grpc_core::RefCountedStringValue value) {
  CHECK(key < OptionalLabelKey::kSize);
  optional_labels_[static_cast<size_t>(key)] = std::move(value);
}

void OpenTelemetryPluginImpl::ClientCallTracer::CallAttemptTracer::
    PopulateLabelInjectors(grpc_metadata_batch* metadata) {
  parent_->scope_config_->active_plugin_options_view().ForEach(
      [&](const InternalOpenTelemetryPluginOption& plugin_option,
          size_t /*index*/) {
        auto* labels_injector = plugin_option.labels_injector();
        if (labels_injector != nullptr) {
          injected_labels_from_plugin_options_.push_back(
              labels_injector->GetLabels(metadata));
        }
        return true;
      },
      parent_->otel_plugin_);
}

//
// OpenTelemetryPluginImpl::ClientCallTracer
//

OpenTelemetryPluginImpl::ClientCallTracer::ClientCallTracer(
    const grpc_core::Slice& path, grpc_core::Arena* arena,
    bool registered_method, OpenTelemetryPluginImpl* otel_plugin,
    std::shared_ptr<OpenTelemetryPluginImpl::ClientScopeConfig> scope_config)
    : path_(path.Ref()),
      arena_(arena),
      registered_method_(registered_method),
      otel_plugin_(otel_plugin),
      scope_config_(std::move(scope_config)) {
  if (otel_plugin_->tracer_ != nullptr) {
    opentelemetry::trace::StartSpanOptions options;
    // Get the parent span from the parent call if available, otherwise fall
    // back to the threadlocal span.
    // We are intentionally reusing census_context to save opentelemetry's Span
    // on the context to avoid introducing a new type for opentelemetry inside
    // gRPC Core. There's no risk of collisions since we do not allow multiple
    // tracing systems active for the same call.
    // TODO(yashykt) : We might want to allow multiple tracing systems. A
    // potential idea is to expose arena based contexts via ServerContext and
    // ClientContext to the application, allowing us to propagate multiple span
    // contexts for the same call.
    auto* parent_span = reinterpret_cast<opentelemetry::trace::Span*>(
        arena->GetContext<census_context>());
    if (parent_span != nullptr) {
      options.parent = parent_span->GetContext();
    } else {
      options.parent =
          opentelemetry::trace::Tracer::GetCurrentSpan()->GetContext();
    }
    span_ = otel_plugin_->tracer_->StartSpan(
        absl::StrCat("Sent.", GetMethodFromPath(path_)), options);
  }
}

OpenTelemetryPluginImpl::ClientCallTracer::~ClientCallTracer() {
  if (span_ != nullptr) {
    span_->End();
  }
}

OpenTelemetryPluginImpl::ClientCallTracer::CallAttemptTracer*
OpenTelemetryPluginImpl::ClientCallTracer::StartNewAttempt(
    bool is_transparent_retry) {
  // We allocate the first attempt on the arena and all subsequent attempts
  // on the heap, so that in the common case we don't require a heap
  // allocation, nor do we unnecessarily grow the arena.
  bool is_first_attempt = true;
  uint64_t attempt_num;
  {
    grpc_core::MutexLock lock(&mu_);
    if (transparent_retries_ != 0 || retries_ != 0) {
      is_first_attempt = false;
    }
    if (is_transparent_retry) {
      ++transparent_retries_;
    } else {
      ++retries_;
    }
    attempt_num = retries_ - 1;  // Sequence starts at 0
  }
  if (is_first_attempt) {
    return arena_->New<CallAttemptTracer>(
        this, attempt_num, is_transparent_retry, /*arena_allocated=*/true);
  }
  return new CallAttemptTracer(this, attempt_num, is_transparent_retry,
                               /*arena_allocated=*/false);
}

absl::string_view OpenTelemetryPluginImpl::ClientCallTracer::MethodForStats()
    const {
  absl::string_view method = absl::StripPrefix(path_.as_string_view(), "/");
  if (registered_method_ ||
      (otel_plugin_->generic_method_attribute_filter() != nullptr &&
       otel_plugin_->generic_method_attribute_filter()(method))) {
    return method;
  }
  return "other";
}

void OpenTelemetryPluginImpl::ClientCallTracer::RecordAnnotation(
    absl::string_view annotation) {
  if (span_ != nullptr) {
    span_->AddEvent(AbslStringViewToNoStdStringView(annotation));
  }
}

void OpenTelemetryPluginImpl::ClientCallTracer::RecordAnnotation(
    const Annotation& /*annotation*/) {
  // Not implemented
}

}  // namespace internal
}  // namespace grpc

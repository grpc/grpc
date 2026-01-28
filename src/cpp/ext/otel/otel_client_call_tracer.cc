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
#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/time.h>
#include <stdint.h>

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "opentelemetry/context/context.h"
#include "opentelemetry/metrics/sync_instruments.h"
#include "opentelemetry/trace/context.h"
#include "opentelemetry/trace/tracer.h"
#include "src/core/call/metadata_batch.h"
#include "src/core/call/status_util.h"
#include "src/core/client_channel/client_channel_filter.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/event_engine/utils.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/surface/call.h"
#include "src/core/telemetry/tcp_tracer.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/sync.h"
#include "src/cpp/ext/otel/key_value_iterable.h"
#include "src/cpp/ext/otel/otel_plugin.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/span.h"

namespace grpc {
namespace internal {

template <typename UnrefBehavior>
class OpenTelemetryPluginImpl::ClientCallTracerInterface::CallAttemptTracer<
    UnrefBehavior>::TcpCallTracer : public grpc_core::TcpCallTracer {
 public:
  explicit TcpCallTracer(
      grpc_core::RefCountedPtr<
          OpenTelemetryPluginImpl::ClientCallTracerInterface::CallAttemptTracer<
              UnrefBehavior>>
          call_attempt_tracer)
      : call_attempt_tracer_(std::move(call_attempt_tracer)) {
    // Take a ref on the call if tracing is enabled, since TCP traces might
    // arrive after all the other refs on the call are gone.
    call_attempt_tracer_->parent_->arena_
        ->template GetContext<grpc_core::Call>()
        ->InternalRef(
            "OpenTelemetryPluginImpl::ClientCallTracerInterface::"
            "CallAttemptTracer::TcpCallTracer");
  }

  ~TcpCallTracer() override {
    grpc_core::ExecCtx exec_ctx;
    auto* arena = call_attempt_tracer_->parent_->arena_;
    // The CallAttemptTracer can be allocated on the arena and hence needs to be
    // reset before unreffing the call.
    call_attempt_tracer_.reset();
    arena->template GetContext<grpc_core::Call>()->InternalUnref(
        "OpenTelemetryPluginImpl::ClientCallTracerInterface::"
        "CallAttemptTracer::~TcpCallTracer");
  }

  void RecordEvent(grpc_event_engine::experimental::internal::WriteEvent type,
                   absl::Time time, size_t byte_offset,
                   const std::vector<TcpEventMetric>& metrics) override {
    call_attempt_tracer_->RecordAnnotation(
        absl::StrCat(
            "TCP: ", grpc_event_engine::experimental::WriteEventToString(type),
            " byte_offset=", byte_offset, " ",
            grpc_core::TcpCallTracer::TcpEventMetricsToString(metrics)),
        time);
  }

 private:
  grpc_core::RefCountedPtr<OpenTelemetryPluginImpl::ClientCallTracerInterface::
                               CallAttemptTracer<UnrefBehavior>>
      call_attempt_tracer_;
};

//
// OpenTelemetryPluginImpl::ClientCallTracerInterface::CallAttemptTracer
//
template <typename UnrefBehavior>
OpenTelemetryPluginImpl::ClientCallTracerInterface::
    CallAttemptTracer<UnrefBehavior>::CallAttemptTracer(
        OpenTelemetryPluginImpl::ClientCallTracerInterface* const parent,
        uint64_t attempt_num, bool is_transparent_retry)
    : parent_(parent), start_time_(absl::Now()) {
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

template <typename UnrefBehavior>
OpenTelemetryPluginImpl::ClientCallTracerInterface::CallAttemptTracer<
    UnrefBehavior>::~CallAttemptTracer<UnrefBehavior>() {
  if (span_ != nullptr) {
    span_->End();
  }
}

template <typename UnrefBehavior>
void OpenTelemetryPluginImpl::ClientCallTracerInterface::CallAttemptTracer<
    UnrefBehavior>::RecordReceivedInitialMetadata(grpc_metadata_batch*
                                                      recv_initial_metadata) {
  if (recv_initial_metadata != nullptr &&
      recv_initial_metadata->get(grpc_core::GrpcTrailersOnly())
          .value_or(false)) {
    is_trailers_only_ = true;
    return;
  }
  PopulateLabelInjectors(recv_initial_metadata);
}

template <typename UnrefBehavior>
void OpenTelemetryPluginImpl::ClientCallTracerInterface::CallAttemptTracer<
    UnrefBehavior>::RecordSendInitialMetadata(grpc_metadata_batch*
                                                  send_initial_metadata) {
  GRPC_CHECK(
      !grpc_core::IsCallTracerSendInitialMetadataIsAnAnnotationEnabled());
  MutateSendInitialMetadata(send_initial_metadata);
}

template <typename UnrefBehavior>
void OpenTelemetryPluginImpl::ClientCallTracerInterface::CallAttemptTracer<
    UnrefBehavior>::MutateSendInitialMetadata(grpc_metadata_batch*
                                                  send_initial_metadata) {
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
    if (parent_->otel_plugin_->text_map_propagator_ != nullptr) {
      GrpcTextMapCarrier carrier(send_initial_metadata);
      opentelemetry::context::Context context;
      context = opentelemetry::trace::SetSpan(context, span_);
      parent_->otel_plugin_->text_map_propagator_->Inject(carrier, context);
    }
    if (IsSampled()) {
      parent_->arena_->GetContext<grpc_core::Call>()->set_traced(true);
    }
  }
}

template <typename UnrefBehavior>
void OpenTelemetryPluginImpl::ClientCallTracerInterface::CallAttemptTracer<
    UnrefBehavior>::RecordSendMessage(const grpc_core::Message& send_message) {
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

template <typename UnrefBehavior>
void OpenTelemetryPluginImpl::ClientCallTracerInterface::CallAttemptTracer<
    UnrefBehavior>::RecordSendCompressedMessage(const grpc_core::Message&
                                                    send_compressed_message) {
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

template <typename UnrefBehavior>
void OpenTelemetryPluginImpl::ClientCallTracerInterface::CallAttemptTracer<
    UnrefBehavior>::RecordReceivedMessage(const grpc_core::Message&
                                              recv_message) {
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

template <typename UnrefBehavior>
void OpenTelemetryPluginImpl::ClientCallTracerInterface::
    CallAttemptTracer<UnrefBehavior>::RecordReceivedDecompressedMessage(
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

template <typename UnrefBehavior>
void OpenTelemetryPluginImpl::ClientCallTracerInterface::
    CallAttemptTracer<UnrefBehavior>::RecordReceivedTrailingMetadata(
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
  if (parent_->otel_plugin_->client_.call.retry_delay != nullptr) {
    grpc_core::MutexLock lock(&parent_->mu_);
    if (--parent_->num_active_attempts_ == 0) {
      parent_->time_at_last_attempt_end_ = absl::Now();
    }
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

template <typename UnrefBehavior>
void OpenTelemetryPluginImpl::ClientCallTracerInterface::CallAttemptTracer<
    UnrefBehavior>::RecordIncomingBytes(const TransportByteSize&
                                            transport_byte_size) {
  incoming_bytes_.fetch_add(transport_byte_size.data_bytes);
}

template <typename UnrefBehavior>
void OpenTelemetryPluginImpl::ClientCallTracerInterface::CallAttemptTracer<
    UnrefBehavior>::RecordOutgoingBytes(const TransportByteSize&
                                            transport_byte_size) {
  outgoing_bytes_.fetch_add(transport_byte_size.data_bytes);
}

template <typename UnrefBehavior>
void OpenTelemetryPluginImpl::ClientCallTracerInterface::CallAttemptTracer<
    UnrefBehavior>::RecordCancel(absl::Status /*cancel_error*/) {}

template <typename UnrefBehavior>
void OpenTelemetryPluginImpl::ClientCallTracerInterface::CallAttemptTracer<
    UnrefBehavior>::RecordEnd() {
  this->Unref(DEBUG_LOCATION, "RecordEnd");
}

template <typename UnrefBehavior>
void OpenTelemetryPluginImpl::ClientCallTracerInterface::CallAttemptTracer<
    UnrefBehavior>::RecordAnnotation(absl::string_view annotation) {
  if (span_ != nullptr) {
    span_->AddEvent(AbslStringViewToNoStdStringView(annotation));
  }
}

template <typename UnrefBehavior>
void OpenTelemetryPluginImpl::ClientCallTracerInterface::CallAttemptTracer<
    UnrefBehavior>::RecordAnnotation(const Annotation& annotation) {
  if (annotation.type() == grpc_core::CallTracerAnnotationInterface::
                               AnnotationType::kSendInitialMetadata) {
    // Otel does not have any immutable tracing for send initial metadata.
    // All Otel work for send initial metadata is mutation, which is handled in
    // MutateSendInitialMetadata.
    return;
  }
  RecordAnnotation(annotation.ToString());
}

template <typename UnrefBehavior>
void OpenTelemetryPluginImpl::ClientCallTracerInterface::CallAttemptTracer<
    UnrefBehavior>::RecordAnnotation(absl::string_view annotation,
                                     absl::Time time) {
  if (span_ != nullptr) {
    span_->AddEvent(AbslStringViewToNoStdStringView(annotation),
                    absl::ToChronoTime(time));
  }
}

template <typename UnrefBehavior>
std::shared_ptr<grpc_core::TcpCallTracer>
OpenTelemetryPluginImpl::ClientCallTracerInterface::CallAttemptTracer<
    UnrefBehavior>::StartNewTcpTrace() {
  if (span_ != nullptr) {
    return std::make_shared<TcpCallTracer>(
        this->Ref(DEBUG_LOCATION, "StartNewTcpTrace"));
  }
  return nullptr;
}

template <typename UnrefBehavior>
void OpenTelemetryPluginImpl::ClientCallTracerInterface::CallAttemptTracer<
    UnrefBehavior>::SetOptionalLabel(OptionalLabelKey key,
                                     grpc_core::RefCountedStringValue value) {
  GRPC_CHECK(key < OptionalLabelKey::kSize);
  optional_labels_[static_cast<size_t>(key)] = std::move(value);
}

template <typename UnrefBehavior>
void OpenTelemetryPluginImpl::ClientCallTracerInterface::CallAttemptTracer<
    UnrefBehavior>::PopulateLabelInjectors(grpc_metadata_batch* metadata) {
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
// OpenTelemetryPluginImpl::ClientCallTracerInterface
//

OpenTelemetryPluginImpl::ClientCallTracerInterface::ClientCallTracerInterface(
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

OpenTelemetryPluginImpl::ClientCallTracerInterface::
    ~ClientCallTracerInterface() {
  std::array<std::pair<opentelemetry::nostd::string_view,
                       opentelemetry::common::AttributeValue>,
             2>
      attributes = {
          std::pair(AbslStringViewToNoStdStringView(OpenTelemetryMethodKey()),
                    opentelemetry::common::AttributeValue(
                        AbslStringViewToNoStdStringView(MethodForStats()))),
          std::pair(AbslStringViewToNoStdStringView(OpenTelemetryTargetKey()),
                    opentelemetry::common::AttributeValue(
                        AbslStringViewToNoStdStringView(
                            scope_config_->filtered_target())))};
  if (otel_plugin_->client_.call.retries != nullptr && retries_ > 1) {
    otel_plugin_->client_.call.retries->Record(
        retries_ - 1, attributes, opentelemetry::context::Context{});
  }
  if (otel_plugin_->client_.call.transparent_retries != nullptr &&
      transparent_retries_ != 0) {
    otel_plugin_->client_.call.transparent_retries->Record(
        transparent_retries_, attributes, opentelemetry::context::Context{});
  }
  if (otel_plugin_->client_.call.retry_delay != nullptr &&
      retry_delay_ != absl::ZeroDuration() && retries_ > 1) {
    otel_plugin_->client_.call.retry_delay->Record(
        absl::ToDoubleSeconds(retry_delay_), attributes,
        opentelemetry::context::Context{});
  }
  if (span_ != nullptr) {
    span_->End();
  }
}

grpc_core::ClientCallTracerInterface::CallAttemptTracer*
OpenTelemetryPluginImpl::ClientCallTracerInterface::StartNewAttempt(
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
      if (otel_plugin_->client_.call.retry_delay != nullptr &&
          num_active_attempts_ == 0 && !is_transparent_retry) {
        retry_delay_ += absl::Now() - time_at_last_attempt_end_;
      }
    }
    if (is_transparent_retry) {
      ++transparent_retries_;
    } else {
      ++retries_;
    }
    attempt_num = retries_ - 1;  // Sequence starts at 0
    ++num_active_attempts_;
  }
  if (is_first_attempt) {
    return arena_
        ->MakeRefCounted<CallAttemptTracer<grpc_core::UnrefCallDtor>>(
            this, attempt_num, is_transparent_retry)
        .release();
  }
  return new CallAttemptTracer<grpc_core::UnrefDelete>(this, attempt_num,
                                                       is_transparent_retry);
}

absl::string_view
OpenTelemetryPluginImpl::ClientCallTracerInterface::MethodForStats() const {
  absl::string_view method = absl::StripPrefix(path_.as_string_view(), "/");
  if (registered_method_ ||
      (otel_plugin_->generic_method_attribute_filter() != nullptr &&
       otel_plugin_->generic_method_attribute_filter()(method))) {
    return method;
  }
  return "other";
}

void OpenTelemetryPluginImpl::ClientCallTracerInterface::RecordAnnotation(
    absl::string_view annotation) {
  if (span_ != nullptr) {
    span_->AddEvent(AbslStringViewToNoStdStringView(annotation));
  }
}

void OpenTelemetryPluginImpl::ClientCallTracerInterface::RecordAnnotation(
    const Annotation& /*annotation*/) {
  // Not implemented
}

}  // namespace internal
}  // namespace grpc

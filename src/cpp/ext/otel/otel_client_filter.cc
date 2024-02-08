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

#include "src/cpp/ext/otel/otel_client_filter.h"

#include <stdint.h>

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/optional.h"
#include "absl/types/span.h"
#include "opentelemetry/context/context.h"
#include "opentelemetry/metrics/sync_instruments.h"

#include <grpc/status.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/client_channel/client_channel_filter.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/context.h"
#include "src/core/lib/channel/status_util.h"
#include "src/core/lib/channel/tcp_tracer.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/cpp/ext/otel/key_value_iterable.h"
#include "src/cpp/ext/otel/otel_call_tracer.h"
#include "src/cpp/ext/otel/otel_plugin.h"

namespace grpc {
namespace internal {

//
// OpenTelemetryClientFilter
//

const grpc_channel_filter OpenTelemetryClientFilter::kFilter =
    grpc_core::MakePromiseBasedFilter<OpenTelemetryClientFilter,
                                      grpc_core::FilterEndpoint::kClient>(
        "otel_client");

absl::StatusOr<OpenTelemetryClientFilter> OpenTelemetryClientFilter::Create(
    const grpc_core::ChannelArgs& args, ChannelFilter::Args /*filter_args*/) {
  return OpenTelemetryClientFilter(
      args.GetOwnedString(GRPC_ARG_SERVER_URI).value_or(""));
}

grpc_core::ArenaPromise<grpc_core::ServerMetadataHandle>
OpenTelemetryClientFilter::MakeCallPromise(
    grpc_core::CallArgs call_args,
    grpc_core::NextPromiseFactory next_promise_factory) {
  auto* path = call_args.client_initial_metadata->get_pointer(
      grpc_core::HttpPathMetadata());
  bool registered_method = reinterpret_cast<uintptr_t>(
      call_args.client_initial_metadata->get(grpc_core::GrpcRegisteredMethod())
          .value_or(nullptr));
  auto* call_context = grpc_core::GetContext<grpc_call_context_element>();
  auto* tracer =
      grpc_core::GetContext<grpc_core::Arena>()
          ->ManagedNew<OpenTelemetryCallTracer>(
              this, path != nullptr ? path->Ref() : grpc_core::Slice(),
              grpc_core::GetContext<grpc_core::Arena>(), registered_method);
  GPR_DEBUG_ASSERT(
      call_context[GRPC_CONTEXT_CALL_TRACER_ANNOTATION_INTERFACE].value ==
      nullptr);
  call_context[GRPC_CONTEXT_CALL_TRACER_ANNOTATION_INTERFACE].value = tracer;
  call_context[GRPC_CONTEXT_CALL_TRACER_ANNOTATION_INTERFACE].destroy = nullptr;
  return next_promise_factory(std::move(call_args));
}

OpenTelemetryClientFilter::OpenTelemetryClientFilter(std::string target)
    : active_plugin_options_view_(
          ActivePluginOptionsView::MakeForClient(target)) {
  // Use the original target string only if a filter on the attribute is not
  // registered or if the filter returns true, otherwise use "other".
  if (OpenTelemetryPluginState().target_attribute_filter == nullptr ||
      OpenTelemetryPluginState().target_attribute_filter(target)) {
    filtered_target_ = std::move(target);
  } else {
    filtered_target_ = "other";
  }
}

//
// OpenTelemetryCallTracer::OpenTelemetryCallAttemptTracer
//

OpenTelemetryCallTracer::OpenTelemetryCallAttemptTracer::
    OpenTelemetryCallAttemptTracer(const OpenTelemetryCallTracer* parent,
                                   bool arena_allocated)
    : parent_(parent),
      arena_allocated_(arena_allocated),
      start_time_(absl::Now()) {
  if (OpenTelemetryPluginState().client.attempt.started != nullptr) {
    std::array<std::pair<absl::string_view, absl::string_view>, 2>
        additional_labels = {
            {{OpenTelemetryMethodKey(), parent_->MethodForStats()},
             {OpenTelemetryTargetKey(), parent_->parent_->filtered_target()}}};
    // We might not have all the injected labels that we want at this point, so
    // avoid recording a subset of injected labels here.
    OpenTelemetryPluginState().client.attempt.started->Add(
        1, KeyValueIterable(/*injected_labels_from_plugin_options=*/{},
                            additional_labels,
                            /*active_plugin_options_view=*/nullptr,
                            /*optional_labels_span=*/{}, /*is_client=*/true));
  }
}

void OpenTelemetryCallTracer::OpenTelemetryCallAttemptTracer::
    RecordReceivedInitialMetadata(grpc_metadata_batch* recv_initial_metadata) {
  parent_->parent_->active_plugin_options_view().ForEach(
      [&](const InternalOpenTelemetryPluginOption& plugin_option,
          size_t /*index*/) {
        auto* labels_injector = plugin_option.labels_injector();
        if (labels_injector != nullptr) {
          injected_labels_from_plugin_options_.push_back(
              labels_injector->GetLabels(recv_initial_metadata));
        }
        return true;
      });
}

void OpenTelemetryCallTracer::OpenTelemetryCallAttemptTracer::
    RecordSendInitialMetadata(grpc_metadata_batch* send_initial_metadata) {
  parent_->parent_->active_plugin_options_view().ForEach(
      [&](const InternalOpenTelemetryPluginOption& plugin_option,
          size_t /*index*/) {
        auto* labels_injector = plugin_option.labels_injector();
        if (labels_injector != nullptr) {
          labels_injector->AddLabels(send_initial_metadata, nullptr);
        }
        return true;
      });
}

void OpenTelemetryCallTracer::OpenTelemetryCallAttemptTracer::RecordSendMessage(
    const grpc_core::SliceBuffer& send_message) {
  RecordAnnotation(
      absl::StrFormat("Send message: %ld bytes", send_message.Length()));
}

void OpenTelemetryCallTracer::OpenTelemetryCallAttemptTracer::
    RecordSendCompressedMessage(
        const grpc_core::SliceBuffer& send_compressed_message) {
  RecordAnnotation(absl::StrFormat("Send compressed message: %ld bytes",
                                   send_compressed_message.Length()));
}

void OpenTelemetryCallTracer::OpenTelemetryCallAttemptTracer::
    RecordReceivedMessage(const grpc_core::SliceBuffer& recv_message) {
  RecordAnnotation(
      absl::StrFormat("Received message: %ld bytes", recv_message.Length()));
}

void OpenTelemetryCallTracer::OpenTelemetryCallAttemptTracer::
    RecordReceivedDecompressedMessage(
        const grpc_core::SliceBuffer& recv_decompressed_message) {
  RecordAnnotation(absl::StrFormat("Received decompressed message: %ld bytes",
                                   recv_decompressed_message.Length()));
}

void OpenTelemetryCallTracer::OpenTelemetryCallAttemptTracer::
    RecordReceivedTrailingMetadata(
        absl::Status status, grpc_metadata_batch* /*recv_trailing_metadata*/,
        const grpc_transport_stream_stats* transport_stream_stats) {
  std::array<std::pair<absl::string_view, absl::string_view>, 3>
      additional_labels = {
          {{OpenTelemetryMethodKey(), parent_->MethodForStats()},
           {OpenTelemetryTargetKey(), parent_->parent_->filtered_target()},
           {OpenTelemetryStatusKey(),
            grpc_status_code_to_string(
                static_cast<grpc_status_code>(status.code()))}}};
  KeyValueIterable labels(injected_labels_from_plugin_options_,
                          additional_labels,
                          &parent_->parent_->active_plugin_options_view(),
                          optional_labels_array_, /*is_client=*/true);
  if (OpenTelemetryPluginState().client.attempt.duration != nullptr) {
    OpenTelemetryPluginState().client.attempt.duration->Record(
        absl::ToDoubleSeconds(absl::Now() - start_time_), labels,
        opentelemetry::context::Context{});
  }
  if (OpenTelemetryPluginState()
          .client.attempt.sent_total_compressed_message_size != nullptr) {
    OpenTelemetryPluginState()
        .client.attempt.sent_total_compressed_message_size->Record(
            transport_stream_stats != nullptr
                ? transport_stream_stats->outgoing.data_bytes
                : 0,
            labels, opentelemetry::context::Context{});
  }
  if (OpenTelemetryPluginState()
          .client.attempt.rcvd_total_compressed_message_size != nullptr) {
    OpenTelemetryPluginState()
        .client.attempt.rcvd_total_compressed_message_size->Record(
            transport_stream_stats != nullptr
                ? transport_stream_stats->incoming.data_bytes
                : 0,
            labels, opentelemetry::context::Context{});
  }
}

void OpenTelemetryCallTracer::OpenTelemetryCallAttemptTracer::RecordCancel(
    absl::Status /*cancel_error*/) {}

void OpenTelemetryCallTracer::OpenTelemetryCallAttemptTracer::RecordEnd(
    const gpr_timespec& /*latency*/) {
  if (arena_allocated_) {
    this->~OpenTelemetryCallAttemptTracer();
  } else {
    delete this;
  }
}

void OpenTelemetryCallTracer::OpenTelemetryCallAttemptTracer::RecordAnnotation(
    absl::string_view /*annotation*/) {
  // Not implemented
}

void OpenTelemetryCallTracer::OpenTelemetryCallAttemptTracer::RecordAnnotation(
    const Annotation& /*annotation*/) {
  // Not implemented
}

std::shared_ptr<grpc_core::TcpTracerInterface>
OpenTelemetryCallTracer::OpenTelemetryCallAttemptTracer::StartNewTcpTrace() {
  // No TCP trace.
  return nullptr;
}

void OpenTelemetryCallTracer::OpenTelemetryCallAttemptTracer::AddOptionalLabels(
    OptionalLabelComponent component,
    std::shared_ptr<std::map<std::string, std::string>> optional_labels) {
  optional_labels_array_[static_cast<std::size_t>(component)] =
      std::move(optional_labels);
}

//
// OpenTelemetryCallTracer
//

OpenTelemetryCallTracer::OpenTelemetryCallTracer(
    OpenTelemetryClientFilter* parent, grpc_core::Slice path,
    grpc_core::Arena* arena, bool registered_method)
    : parent_(parent),
      path_(std::move(path)),
      arena_(arena),
      registered_method_(registered_method) {}

OpenTelemetryCallTracer::~OpenTelemetryCallTracer() {}

OpenTelemetryCallTracer::OpenTelemetryCallAttemptTracer*
OpenTelemetryCallTracer::StartNewAttempt(bool is_transparent_retry) {
  // We allocate the first attempt on the arena and all subsequent attempts
  // on the heap, so that in the common case we don't require a heap
  // allocation, nor do we unnecessarily grow the arena.
  bool is_first_attempt = true;
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
  }
  if (is_first_attempt) {
    return arena_->New<OpenTelemetryCallAttemptTracer>(
        this, /*arena_allocated=*/true);
  }
  return new OpenTelemetryCallAttemptTracer(this, /*arena_allocated=*/false);
}

absl::string_view OpenTelemetryCallTracer::MethodForStats() const {
  absl::string_view method = absl::StripPrefix(path_.as_string_view(), "/");
  if (registered_method_ ||
      (OpenTelemetryPluginState().generic_method_attribute_filter != nullptr &&
       OpenTelemetryPluginState().generic_method_attribute_filter(method))) {
    return method;
  }
  return "other";
}

void OpenTelemetryCallTracer::RecordAnnotation(
    absl::string_view /*annotation*/) {
  // Not implemented
}

void OpenTelemetryCallTracer::RecordAnnotation(
    const Annotation& /*annotation*/) {
  // Not implemented
}

}  // namespace internal
}  // namespace grpc

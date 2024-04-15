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
#include <grpc/support/port_platform.h>
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
#include "src/cpp/ext/otel/otel_plugin.h"

namespace grpc {
namespace internal {

//
// OpenTelemetryPlugin::ClientCallTracer::CallAttemptTracer
//

OpenTelemetryPlugin::ClientCallTracer::CallAttemptTracer::CallAttemptTracer(
    const OpenTelemetryPlugin::ClientCallTracer* parent, bool arena_allocated)
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
}

void OpenTelemetryPlugin::ClientCallTracer::CallAttemptTracer::
    RecordReceivedInitialMetadata(grpc_metadata_batch* recv_initial_metadata) {
  parent_->scope_config_->active_plugin_options_view().ForEach(
      [&](const InternalOpenTelemetryPluginOption& plugin_option,
          size_t /*index*/) {
        auto* labels_injector = plugin_option.labels_injector();
        if (labels_injector != nullptr) {
          injected_labels_from_plugin_options_.push_back(
              labels_injector->GetLabels(recv_initial_metadata));
        }
        return true;
      },
      parent_->otel_plugin_);
}

void OpenTelemetryPlugin::ClientCallTracer::CallAttemptTracer::
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
}

void OpenTelemetryPlugin::ClientCallTracer::CallAttemptTracer::
    RecordSendMessage(const grpc_core::SliceBuffer& send_message) {
  RecordAnnotation(
      absl::StrFormat("Send message: %ld bytes", send_message.Length()));
}

void OpenTelemetryPlugin::ClientCallTracer::CallAttemptTracer::
    RecordSendCompressedMessage(
        const grpc_core::SliceBuffer& send_compressed_message) {
  RecordAnnotation(absl::StrFormat("Send compressed message: %ld bytes",
                                   send_compressed_message.Length()));
}

void OpenTelemetryPlugin::ClientCallTracer::CallAttemptTracer::
    RecordReceivedMessage(const grpc_core::SliceBuffer& recv_message) {
  RecordAnnotation(
      absl::StrFormat("Received message: %ld bytes", recv_message.Length()));
}

void OpenTelemetryPlugin::ClientCallTracer::CallAttemptTracer::
    RecordReceivedDecompressedMessage(
        const grpc_core::SliceBuffer& recv_decompressed_message) {
  RecordAnnotation(absl::StrFormat("Received decompressed message: %ld bytes",
                                   recv_decompressed_message.Length()));
}

void OpenTelemetryPlugin::ClientCallTracer::CallAttemptTracer::
    RecordReceivedTrailingMetadata(
        absl::Status status, grpc_metadata_batch* /*recv_trailing_metadata*/,
        const grpc_transport_stream_stats* transport_stream_stats) {
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
  if (parent_->otel_plugin_->client_.attempt
          .sent_total_compressed_message_size != nullptr) {
    parent_->otel_plugin_->client_.attempt.sent_total_compressed_message_size
        ->Record(transport_stream_stats != nullptr
                     ? transport_stream_stats->outgoing.data_bytes
                     : 0,
                 labels, opentelemetry::context::Context{});
  }
  if (parent_->otel_plugin_->client_.attempt
          .rcvd_total_compressed_message_size != nullptr) {
    parent_->otel_plugin_->client_.attempt.rcvd_total_compressed_message_size
        ->Record(transport_stream_stats != nullptr
                     ? transport_stream_stats->incoming.data_bytes
                     : 0,
                 labels, opentelemetry::context::Context{});
  }
}

void OpenTelemetryPlugin::ClientCallTracer::CallAttemptTracer::RecordCancel(
    absl::Status /*cancel_error*/) {}

void OpenTelemetryPlugin::ClientCallTracer::CallAttemptTracer::RecordEnd(
    const gpr_timespec& /*latency*/) {
  if (arena_allocated_) {
    this->~CallAttemptTracer();
  } else {
    delete this;
  }
}

void OpenTelemetryPlugin::ClientCallTracer::CallAttemptTracer::RecordAnnotation(
    absl::string_view /*annotation*/) {
  // Not implemented
}

void OpenTelemetryPlugin::ClientCallTracer::CallAttemptTracer::RecordAnnotation(
    const Annotation& /*annotation*/) {
  // Not implemented
}

std::shared_ptr<grpc_core::TcpTracerInterface>
OpenTelemetryPlugin::ClientCallTracer::CallAttemptTracer::StartNewTcpTrace() {
  // No TCP trace.
  return nullptr;
}

void OpenTelemetryPlugin::ClientCallTracer::CallAttemptTracer::SetOptionalLabel(
    OptionalLabelKey key, grpc_core::RefCountedStringValue value) {
  GPR_ASSERT(key < OptionalLabelKey::kSize);
  optional_labels_[static_cast<size_t>(key)] = std::move(value);
}

//
// OpenTelemetryPlugin::ClientCallTracer
//

OpenTelemetryPlugin::ClientCallTracer::ClientCallTracer(
    const grpc_core::Slice& path, grpc_core::Arena* arena,
    bool registered_method, OpenTelemetryPlugin* otel_plugin,
    std::shared_ptr<OpenTelemetryPlugin::ClientScopeConfig> scope_config)
    : path_(path.Ref()),
      arena_(arena),
      registered_method_(registered_method),
      otel_plugin_(otel_plugin),
      scope_config_(std::move(scope_config)) {}

OpenTelemetryPlugin::ClientCallTracer::~ClientCallTracer() {}

OpenTelemetryPlugin::ClientCallTracer::CallAttemptTracer*
OpenTelemetryPlugin::ClientCallTracer::StartNewAttempt(
    bool is_transparent_retry) {
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
    return arena_->New<CallAttemptTracer>(this, /*arena_allocated=*/true);
  }
  return new CallAttemptTracer(this, /*arena_allocated=*/false);
}

absl::string_view OpenTelemetryPlugin::ClientCallTracer::MethodForStats()
    const {
  absl::string_view method = absl::StripPrefix(path_.as_string_view(), "/");
  if (registered_method_ ||
      (otel_plugin_->generic_method_attribute_filter() != nullptr &&
       otel_plugin_->generic_method_attribute_filter()(method))) {
    return method;
  }
  return "other";
}

void OpenTelemetryPlugin::ClientCallTracer::RecordAnnotation(
    absl::string_view /*annotation*/) {
  // Not implemented
}

void OpenTelemetryPlugin::ClientCallTracer::RecordAnnotation(
    const Annotation& /*annotation*/) {
  // Not implemented
}

}  // namespace internal
}  // namespace grpc

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

#include "src/cpp/ext/filters/otel/otel_client_filter.h"

#include <functional>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>

#include "absl/container/inlined_vector.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "opentelemetry/context/context.h"
#include "opentelemetry/metrics/sync_instruments.h"

#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/context.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/cpp/ext/filters/otel/otel_call_tracer.h"
#include "src/cpp/ext/filters/otel/otel_plugin.h"

namespace grpc {
namespace internal {

//
// OpenTelemetryClientFilter
//

const grpc_channel_filter OpenTelemetryClientFilter::kFilter =
    grpc_core::MakePromiseBasedFilter<OpenTelemetryClientFilter,
                                      grpc_core::FilterEndpoint::kClient, 0>(
        "otel_client");

absl::StatusOr<OpenTelemetryClientFilter> OpenTelemetryClientFilter::Create(
    const grpc_core::ChannelArgs& args, ChannelFilter::Args /*filter_args*/) {
  return OpenTelemetryClientFilter();
}

grpc_core::ArenaPromise<grpc_core::ServerMetadataHandle>
OpenTelemetryClientFilter::MakeCallPromise(
    grpc_core::CallArgs call_args,
    grpc_core::NextPromiseFactory next_promise_factory) {
  auto* path = call_args.client_initial_metadata->get_pointer(
      grpc_core::HttpPathMetadata());
  auto* call_context = grpc_core::GetContext<grpc_call_context_element>();
  auto* tracer = grpc_core::GetContext<grpc_core::Arena>()
                     ->ManagedNew<OpenTelemetryCallTracer>(
                         path != nullptr ? path->Ref() : grpc_core::Slice(),
                         grpc_core::GetContext<grpc_core::Arena>());
  GPR_DEBUG_ASSERT(
      call_context[GRPC_CONTEXT_CALL_TRACER_ANNOTATION_INTERFACE].value ==
      nullptr);
  call_context[GRPC_CONTEXT_CALL_TRACER_ANNOTATION_INTERFACE].value = tracer;
  call_context[GRPC_CONTEXT_CALL_TRACER_ANNOTATION_INTERFACE].destroy = nullptr;
  return next_promise_factory(std::move(call_args));
}

//
// OpenTelemetryCallTracer::OpenTelemetryCallAttemptTracer
//

OpenTelemetryCallTracer::OpenTelemetryCallAttemptTracer::
    OpenTelemetryCallAttemptTracer(OpenTelemetryCallTracer* parent,
                                   bool arena_allocated)
    : parent_(parent),
      arena_allocated_(arena_allocated),
      start_time_(absl::Now()) {
  // TODO(yashykt): Figure out how to get this to work with absl::string_view
  OTelPluginState().client.attempt.started->Add(
      1, {{std::string(OTelMethodKey()), std::string(parent_->method_)}});
}

void OpenTelemetryCallTracer::OpenTelemetryCallAttemptTracer::
    RecordSendInitialMetadata(grpc_metadata_batch* send_initial_metadata) {}

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
        absl::Status status, grpc_metadata_batch* recv_trailing_metadata,
        const grpc_transport_stream_stats* transport_stream_stats) {
  absl::InlinedVector<std::pair<std::string, std::string>, 2> attributes = {
      {std::string(OTelMethodKey()), std::string(parent_->method_)},
      {std::string(OTelStatusKey()), absl::StatusCodeToString(status.code())}};
  OTelPluginState().client.attempt.duration->Record(
      absl::ToDoubleSeconds(absl::Now() - start_time_), attributes,
      opentelemetry::context::Context{});
  OTelPluginState().client.attempt.sent_total_compressed_message_size->Record(
      transport_stream_stats != nullptr
          ? transport_stream_stats->outgoing.data_bytes
          : 0,
      attributes, opentelemetry::context::Context{});
  OTelPluginState().client.attempt.rcvd_total_compressed_message_size->Record(
      transport_stream_stats != nullptr
          ? transport_stream_stats->incoming.data_bytes
          : 0,
      attributes, opentelemetry::context::Context{});
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
    absl::string_view annotation) {
  // Not implemented
}

//
// OpenTelemetryCallTracer
//

OpenTelemetryCallTracer::OpenTelemetryCallTracer(grpc_core::Slice path,
                                                 grpc_core::Arena* arena)
    : path_(std::move(path)),
      method_(absl::StripPrefix(path_.as_string_view(), "/")),
      arena_(arena) {}

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

void OpenTelemetryCallTracer::RecordAnnotation(absl::string_view annotation) {
  // Not implemented
}

}  // namespace internal
}  // namespace grpc

//
//
// Copyright 2024 gRPC authors.
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

#include "src/core/ext/transport/chttp2/transport/call_tracer_wrapper.h"

#include "src/core/ext/transport/chttp2/transport/flow_control.h"
#include "src/core/ext/transport/chttp2/transport/internal.h"

namespace grpc_core {

void Chttp2CallTracerWrapper::RecordIncomingBytes(
    const CallTracerInterface::TransportByteSize& transport_byte_size) {
  // Update legacy API.
  stream_->stats.incoming.framing_bytes += transport_byte_size.framing_bytes;
  stream_->stats.incoming.data_bytes += transport_byte_size.data_bytes;
  stream_->stats.incoming.header_bytes += transport_byte_size.header_bytes;
  // Update new API.
  if (!IsCallTracerInTransportEnabled()) return;
  auto* call_tracer = stream_->arena->GetContext<CallTracerInterface>();
  if (call_tracer != nullptr) {
    call_tracer->RecordIncomingBytes(transport_byte_size);
  }
}

void Chttp2CallTracerWrapper::RecordOutgoingBytes(
    const CallTracerInterface::TransportByteSize& transport_byte_size) {
  // Update legacy API.
  stream_->stats.outgoing.framing_bytes += transport_byte_size.framing_bytes;
  stream_->stats.outgoing.data_bytes += transport_byte_size.data_bytes;
  stream_->stats.outgoing.header_bytes +=
      transport_byte_size.header_bytes;  // Update new API.
  if (!IsCallTracerInTransportEnabled()) return;
  auto* call_tracer = stream_->arena->GetContext<CallTracerInterface>();
  if (call_tracer != nullptr) {
    call_tracer->RecordOutgoingBytes(transport_byte_size);
  }
}

HttpAnnotation::HttpAnnotation(Type type, gpr_timespec time)
    : CallTracerAnnotationInterface::Annotation(
          CallTracerAnnotationInterface::AnnotationType::kHttpTransport),
      type_(type),
      time_(time) {}

std::string HttpAnnotation::ToString() const {
  std::string s = "HttpAnnotation type: ";
  switch (type_) {
    case Type::kStart:
      absl::StrAppend(&s, "Start");
      break;
    case Type::kHeadWritten:
      absl::StrAppend(&s, "HeadWritten");
      break;
    case Type::kEnd:
      absl::StrAppend(&s, "End");
      break;
    default:
      absl::StrAppend(&s, "Unknown");
  }
  absl::StrAppend(&s, " time: ", gpr_format_timespec(time_));
  if (transport_stats_.has_value()) {
    absl::StrAppend(&s, " transport:[", transport_stats_->ToString(), "]");
  }
  if (stream_stats_.has_value()) {
    absl::StrAppend(&s, " stream:[", stream_stats_->ToString(), "]");
  }
  return s;
}

}  // namespace grpc_core
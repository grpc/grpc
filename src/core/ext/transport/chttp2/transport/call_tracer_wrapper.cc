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
  if (is_client_) {
    if (stream_->call_attempt_tracer != nullptr) {
      stream_->call_attempt_tracer->RecordIncomingBytes(transport_byte_size);
    }
  } else {
    if (stream_->server_call_tracer != nullptr) {
      stream_->server_call_tracer->RecordIncomingBytes(transport_byte_size);
    }
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
  if (is_client_) {
    if (stream_->call_attempt_tracer != nullptr) {
      stream_->call_attempt_tracer->RecordOutgoingBytes(transport_byte_size);
    }
  } else {
    if (stream_->server_call_tracer != nullptr) {
      stream_->server_call_tracer->RecordOutgoingBytes(transport_byte_size);
    }
  }
}

std::shared_ptr<TcpCallTracer> Chttp2CallTracerWrapper::StartNewTcpTrace() {
  if (is_client_) {
    if (stream_->call_attempt_tracer != nullptr) {
      return stream_->call_attempt_tracer->StartNewTcpTrace();
    }
  } else {
    if (stream_->server_call_tracer != nullptr) {
      return stream_->server_call_tracer->StartNewTcpTrace();
    }
  }
  return nullptr;
}

}  // namespace grpc_core
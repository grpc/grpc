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

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HTTP_ANNOTATION_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HTTP_ANNOTATION_H

#include "src/core/ext/transport/chttp2/transport/flow_control.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/telemetry/call_tracer.h"

namespace grpc_core {

class HttpAnnotation : public CallTracerAnnotationInterface::Annotation {
 public:
  enum class Type : uint8_t {
    kUnknown = 0,
    // When the first byte enters the HTTP transport.
    kStart,
    // When the first byte leaves the HTTP transport.
    kHeadWritten,
    // When the last byte leaves the HTTP transport.
    kEnd,
  };

  // A snapshot of write stats to export.
  struct WriteStats {
    size_t target_write_size;
  };

  HttpAnnotation(Type type, gpr_timespec time);

  HttpAnnotation& Add(const chttp2::TransportFlowControl::Stats& stats) {
    transport_stats_ = stats;
    return *this;
  }

  HttpAnnotation& Add(const chttp2::StreamFlowControl::Stats& stats) {
    stream_stats_ = stats;
    return *this;
  }

  HttpAnnotation& Add(const WriteStats& stats) {
    write_stats_ = stats;
    return *this;
  }

  std::string ToString() const override;

  Type http_type() const { return type_; }
  gpr_timespec time() const { return time_; }
  absl::optional<chttp2::TransportFlowControl::Stats> transport_stats() const {
    return transport_stats_;
  }
  absl::optional<chttp2::StreamFlowControl::Stats> stream_stats() const {
    return stream_stats_;
  }
  absl::optional<WriteStats> write_stats() const { return write_stats_; }

 private:
  const Type type_;
  const gpr_timespec time_;
  absl::optional<chttp2::TransportFlowControl::Stats> transport_stats_;
  absl::optional<chttp2::StreamFlowControl::Stats> stream_stats_;
  absl::optional<WriteStats> write_stats_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HTTP_ANNOTATION_H

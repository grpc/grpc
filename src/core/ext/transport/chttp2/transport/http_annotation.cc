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

#include "src/core/ext/transport/chttp2/transport/http_annotation.h"

#include "src/core/ext/transport/chttp2/transport/flow_control.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/telemetry/call_tracer.h"

namespace grpc_core {

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

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HTTP_ANNOTATION_H

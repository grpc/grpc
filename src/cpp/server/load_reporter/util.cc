/*
 *
 * Copyright 2018 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "src/cpp/server/load_reporter/util.h"

#include <grpcpp/impl/codegen/config.h>

namespace grpc {
namespace load_reporter {

// Measures.
// TODO(juanlishen): The measure definition follows the recommended style from
// OpenCensus
// (https://github.com/census-instrumentation/opencensus-cpp/blob/master/opencensus/stats/examples/view_and_recording_example.cc#L29).
// But it violates the Google C++ Style Guide
// (https://google.github.io/styleguide/cppguide.html#Static_and_Global_Variables).

::opencensus::stats::MeasureInt64 MeasureStartCount() {
  static const ::opencensus::stats::MeasureInt64 start_count =
      ::opencensus::stats::MeasureInt64::Register(
          grpc::string(kMeasureStartCount), kMeasureStartCount,
          kMeasureStartCount);
  return start_count;
}

::opencensus::stats::MeasureInt64 MeasureEndCount() {
  static const ::opencensus::stats::MeasureInt64 end_count =
      ::opencensus::stats::MeasureInt64::Register(
          grpc::string(kMeasureEndCount), kMeasureEndCount, kMeasureEndCount);
  return end_count;
}

::opencensus::stats::MeasureDouble MeasureEndBytesSent() {
  static const ::opencensus::stats::MeasureDouble end_bytes_sent =
      ::opencensus::stats::MeasureDouble::Register(
          grpc::string(kMeasureEndBytesSent), kMeasureEndBytesSent,
          kMeasureEndBytesSent);
  return end_bytes_sent;
}

::opencensus::stats::MeasureDouble MeasureEndBytesReceived() {
  static const ::opencensus::stats::MeasureDouble end_bytes_received =
      ::opencensus::stats::MeasureDouble::Register(
          grpc::string(kMeasureEndBytesReceived), kMeasureEndBytesReceived,
          kMeasureEndBytesReceived);
  return end_bytes_received;
}

::opencensus::stats::MeasureDouble MeasureEndLatencyMs() {
  static const ::opencensus::stats::MeasureDouble end_latency_ms =
      ::opencensus::stats::MeasureDouble::Register(
          grpc::string(kMeasureEndLatencyMs), kMeasureEndLatencyMs,
          kMeasureEndLatencyMs);
  return end_latency_ms;
}

::opencensus::stats::MeasureDouble MeasureOtherCallMetric() {
  static const ::opencensus::stats::MeasureDouble other_call_metric =
      ::opencensus::stats::MeasureDouble::Register(
          grpc::string(kMeasureOtherCallMetric), kMeasureOtherCallMetric,
          kMeasureOtherCallMetric);
  return other_call_metric;
}

}  // namespace load_reporter
}  // namespace grpc

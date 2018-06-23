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

#ifndef GRPC_CORE_EXT_FILTERS_LOAD_REPORTING_REGISTERED_OPENCENSUS_OBJECTS_H
#define GRPC_CORE_EXT_FILTERS_LOAD_REPORTING_REGISTERED_OPENCENSUS_OBJECTS_H

#include <grpc/support/port_platform.h>

#include <grpc/support/log.h>

#include "opencensus/stats/stats.h"

#include "src/cpp/server/load_reporter/constants.h"

namespace grpc {
namespace load_reporter {

// Measures.

// TODO(juanlishen): This seems weird, but without the dummy logging,
// server_load_reporting_end2end_test will fail on the opt build. See
// https://github.com/census-instrumentation/opencensus-cpp/issues/164.

::opencensus::stats::MeasureInt64 MeasureStartCount() {
  gpr_log(GPR_DEBUG, "MeasureStartCount()");
  static const ::opencensus::stats::MeasureInt64 start_count =
      ::opencensus::stats::MeasureInt64::Register(
          kMeasureStartCount, kMeasureStartCount, kMeasureStartCount);
  return start_count;
}

::opencensus::stats::MeasureInt64 MeasureEndCount() {
  gpr_log(GPR_DEBUG, "MeasureEndCount()");
  static const ::opencensus::stats::MeasureInt64 end_count =
      ::opencensus::stats::MeasureInt64::Register(
          kMeasureEndCount, kMeasureEndCount, kMeasureEndCount);
  return end_count;
}

::opencensus::stats::MeasureInt64 MeasureEndBytesSent() {
  gpr_log(GPR_DEBUG, "MeasureEndBytesSent()");
  static const ::opencensus::stats::MeasureInt64 end_bytes_sent =
      ::opencensus::stats::MeasureInt64::Register(
          kMeasureEndBytesSent, kMeasureEndBytesSent, kMeasureEndBytesSent);
  return end_bytes_sent;
}

::opencensus::stats::MeasureInt64 MeasureEndBytesReceived() {
  gpr_log(GPR_DEBUG, "MeasureEndBytesReceived()");
  static const ::opencensus::stats::MeasureInt64 end_bytes_received =
      ::opencensus::stats::MeasureInt64::Register(kMeasureEndBytesReceived,
                                                  kMeasureEndBytesReceived,
                                                  kMeasureEndBytesReceived);
  return end_bytes_received;
}

::opencensus::stats::MeasureInt64 MeasureEndLatencyMs() {
  gpr_log(GPR_DEBUG, "MeasureEndLatencyMs()");
  static const ::opencensus::stats::MeasureInt64 end_latency_ms =
      ::opencensus::stats::MeasureInt64::Register(
          kMeasureEndLatencyMs, kMeasureEndLatencyMs, kMeasureEndLatencyMs);
  return end_latency_ms;
}

::opencensus::stats::MeasureDouble MeasureOtherCallMetric() {
  gpr_log(GPR_DEBUG, "MeasureOtherCallMetric");
  static const ::opencensus::stats::MeasureDouble other_call_metric =
      ::opencensus::stats::MeasureDouble::Register(kMeasureOtherCallMetric,
                                                   kMeasureOtherCallMetric,
                                                   kMeasureOtherCallMetric);
  return other_call_metric;
}

// Tags.

opencensus::stats::TagKey TagKeyToken() {
  static const auto token = opencensus::stats::TagKey::Register(kTagKeyToken);
  return token;
}

opencensus::stats::TagKey TagKeyHost() {
  static const auto token = opencensus::stats::TagKey::Register(kTagKeyHost);
  return token;
}
opencensus::stats::TagKey TagKeyUserId() {
  static const auto token = opencensus::stats::TagKey::Register(kTagKeyUserId);
  return token;
}

opencensus::stats::TagKey TagKeyStatus() {
  static const auto token = opencensus::stats::TagKey::Register(kTagKeyStatus);
  return token;
}

opencensus::stats::TagKey TagKeyMetricName() {
  static const auto token =
      opencensus::stats::TagKey::Register(kTagKeyMetricName);
  return token;
}

}  // namespace load_reporter
}  // namespace grpc

#endif /* GRPC_CORE_EXT_FILTERS_LOAD_REPORTING_REGISTERED_OPENCENSUS_OBJECTS_H \
        */

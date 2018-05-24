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

#ifndef GRPC_SRC_CPP_SERVER_LOAD_REPORTER_UTIL_H
#define GRPC_SRC_CPP_SERVER_LOAD_REPORTER_UTIL_H

#include "opencensus/stats/stats.h"

namespace grpc {
namespace load_reporter {

constexpr uint8_t kLbIdLength = 8;
constexpr size_t kIpv4AddressLength = 8;
constexpr size_t kIpv6AddressLength = 32;

// Call statuses.

constexpr char kCallStatusOk[] = "OK";
constexpr char kCallStatusServerError[] = "5XX";
constexpr char kCallStatusClientError[] = "4XX";

// Tag keys.

constexpr char kTagKeyToken[] = "token";
constexpr char kTagKeyHost[] = "host";
constexpr char kTagKeyUserId[] = "user_id";
constexpr char kTagKeyStatus[] = "status";
constexpr char kTagKeyMetricName[] = "metric_name";

// Measure names.

constexpr char kMeasureStartCount[] = "grpc.io/lb/start_count";
constexpr char kMeasureEndCount[] = "grpc.io/lb/end_count";
constexpr char kMeasureEndBytesSent[] = "grpc.io/lb/bytes_sent";
constexpr char kMeasureEndBytesReceived[] = "grpc.io/lb/bytes_received";
constexpr char kMeasureEndLatencyMs[] = "grpc.io/lb/latency_ms";
constexpr char kMeasureOtherCallMetric[] = "grpc.io/lb/other_call_metric";

// View names.

constexpr char kViewStartCount[] = "grpc.io/lb_view/start_count";
constexpr char kViewEndCount[] = "grpc.io/lb_view/end_count";
constexpr char kViewEndBytesSent[] = "grpc.io/lb_view/bytes_sent";
constexpr char kViewEndBytesReceived[] = "grpc.io/lb_view/bytes_received";
constexpr char kViewEndLatencyMs[] = "grpc.io/lb_view/latency_ms";
constexpr char kViewOtherCallMetricCount[] =
    "grpc.io/lb_view/other_call_metric_count";
constexpr char kViewOtherCallMetricValue[] =
    "grpc.io/lb_view/other_call_metric_value";

// Measures.

::opencensus::stats::MeasureInt64 MeasureStartCount();
::opencensus::stats::MeasureInt64 MeasureEndCount();
::opencensus::stats::MeasureDouble MeasureEndBytesSent();
::opencensus::stats::MeasureDouble MeasureEndBytesReceived();
::opencensus::stats::MeasureDouble MeasureEndLatencyMs();
::opencensus::stats::MeasureDouble MeasureOtherCallMetric();

}  // namespace load_reporter
}  // namespace grpc

#endif  // GRPC_SRC_CPP_SERVER_LOAD_REPORTER_UTIL_H

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

#ifndef GRPC_CORE_EXT_CENSUS_MEASURES_H
#define GRPC_CORE_EXT_CENSUS_MEASURES_H

#include <grpc/support/port_platform.h>

#include "opencensus/stats/stats.h"
#include "src/core/ext/census/grpc_plugin.h"

namespace opencensus {

stats::MeasureInt RpcClientErrorCount();
stats::MeasureDouble RpcClientRequestBytes();
stats::MeasureDouble RpcClientResponseBytes();
stats::MeasureDouble RpcClientRoundtripLatency();
stats::MeasureDouble RpcClientServerElapsedTime();
stats::MeasureInt RpcClientStartedCount();
stats::MeasureInt RpcClientFinishedCount();
stats::MeasureInt RpcClientRequestCount();
stats::MeasureInt RpcClientResponseCount();

stats::MeasureInt RpcServerErrorCount();
stats::MeasureDouble RpcServerRequestBytes();
stats::MeasureDouble RpcServerResponseBytes();
stats::MeasureDouble RpcServerServerElapsedTime();
stats::MeasureInt RpcServerStartedCount();
stats::MeasureInt RpcServerFinishedCount();
stats::MeasureInt RpcServerRequestCount();
stats::MeasureInt RpcServerResponseCount();

}  // namespace opencensus

#endif /* GRPC_CORE_EXT_CENSUS_MEASURES_H */

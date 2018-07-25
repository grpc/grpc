/*
 *
 * Copyright 2016 gRPC authors.
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

#ifndef GRPC_INTERNAL_CPP_UTIL_CORE_STATS_H
#define GRPC_INTERNAL_CPP_UTIL_CORE_STATS_H

#include "src/proto/grpc/core/stats.pb.h"

#include "src/core/lib/debug/stats.h"

namespace grpc {

void CoreStatsToProto(const grpc_stats_data& core, grpc::core::Stats* proto);
void ProtoToCoreStats(const grpc::core::Stats& proto, grpc_stats_data* core);

}  // namespace grpc

#endif  // GRPC_INTERNAL_CPP_UTIL_CORE_STATS_H

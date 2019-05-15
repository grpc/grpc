//
// Copyright 2019 gRPC authors.
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

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_BACKEND_METRIC_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_BACKEND_METRIC_H

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/client_channel/lb_policy.h"
#include "src/core/lib/channel/context.h"
#include "src/core/lib/gprpp/arena.h"
#include "src/core/lib/gprpp/map.h"
#include "src/core/lib/transport/metadata_batch.h"

namespace grpc_core {

// Gets the backend metrics for a particular call sent by the server in
// recv_trailing_metadata.  Caches the parsed metrics in call_context element
// GRPC_CONTEXT_BACKEND_METRIC_DATA for subsequent calls.
// TODO(roth): Implement this once we can use upb.
// FIXME: change this to not use call_context
LoadBalancingPolicy::BackendMetricData* GetBackendMetricDataForCall(
    grpc_call_context_element* call_context,
    grpc_metadata_batch* recv_trailing_metadata, Arena* arena);

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_BACKEND_METRIC_H */

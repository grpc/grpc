//
// Copyright 2018 gRPC authors.
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

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_HEALTH_HEALTH_CHECK_CLIENT_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_HEALTH_HEALTH_CHECK_CLIENT_H

#include <grpc/support/port_platform.h>

#include <string>

#include "src/core/ext/filters/client_channel/client_channel_channelz.h"
#include "src/core/ext/filters/client_channel/subchannel.h"
#include "src/core/ext/filters/client_channel/subchannel_stream_client.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"
#include "src/core/lib/transport/connectivity_state.h"

namespace grpc_core {

OrphanablePtr<SubchannelStreamClient> MakeHealthCheckClient(
    std::string service_name,
    RefCountedPtr<ConnectedSubchannel> connected_subchannel,
    grpc_pollset_set* interested_parties,
    RefCountedPtr<channelz::SubchannelNode> channelz_node,
    RefCountedPtr<ConnectivityStateWatcherInterface> watcher);

}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_HEALTH_HEALTH_CHECK_CLIENT_H

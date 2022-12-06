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

#ifndef GRPC_CORE_EXT_TRANSPORT_CHTTP2_SERVER_CHTTP2_SERVER_H
#define GRPC_CORE_EXT_TRANSPORT_CHTTP2_SERVER_CHTTP2_SERVER_H

#include <grpc/support/port_platform.h>

#include <functional>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/surface/server.h"

namespace grpc_core {

// A function to modify channel args for a listening addr:port. Note that this
// is used to create a security connector for listeners when the servers are
// configured with a config fetcher. Not invoked if there is no config fetcher
// added to the server. On failure, the error parameter will be set.
using Chttp2ServerArgsModifier =
    std::function<ChannelArgs(const ChannelArgs&, grpc_error_handle*)>;

/// Adds a port to \a server.  Sets \a port_num to the port number.
/// Takes ownership of \a args.
grpc_error_handle Chttp2ServerAddPort(
    Server* server, const char* addr, const ChannelArgs& args,
    Chttp2ServerArgsModifier connection_args_modifier, int* port_num);

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_TRANSPORT_CHTTP2_SERVER_CHTTP2_SERVER_H */

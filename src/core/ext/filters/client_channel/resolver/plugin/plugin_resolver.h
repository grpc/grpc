//
// Copyright 2016 gRPC authors.
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

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RESOLVER_PLUGIN_PLUGIN_RESOLVER_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RESOLVER_PLUGIN_PLUGIN_RESOLVER_H

#include <grpc/support/port_platform.h>

#include <grpc/impl/codegen/grpc_types.h>

namespace grpc_core {

/** Add the resolver \a result to \a base_args.
  NOTE: This method is exposed for testing purposes only. */
grpc_channel_args* AddResolverResultToChannelArgs(
    grpc_channel_args* base_args, const grpc_resolver_result* result);

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RESOLVER_PLUGIN_PLUGIN_RESOLVER_H \
        */

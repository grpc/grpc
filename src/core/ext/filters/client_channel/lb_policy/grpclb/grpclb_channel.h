/*
 *
 * Copyright 2017 gRPC authors.
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

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_GRPCLB_GRPCLB_CHANNEL_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_GRPCLB_GRPCLB_CHANNEL_H

#include <grpc/support/port_platform.h>

#include <grpc/impl/codegen/grpc_types.h>

#include "src/core/ext/filters/client_channel/server_address.h"

/// Makes any necessary modifications to \a args for use in the grpclb
/// balancer channel.
///
/// Takes ownership of \a args.
///
/// Caller takes ownership of the returned args.
grpc_channel_args* grpc_lb_policy_grpclb_modify_lb_channel_args(
    const grpc_core::ServerAddressList& addresses,
    grpc_channel_args* args);

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_GRPCLB_GRPCLB_CHANNEL_H \
        */

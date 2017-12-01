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

#include "src/core/ext/filters/client_channel/lb_policy_factory.h"
#include "src/core/ext/filters/client_channel/resolver/fake/fake_resolver.h"
#include "src/core/lib/slice/slice_hash_table.h"

/** Create the channel used for communicating with an LB service.
 * Note that an LB *service* may be comprised of several LB *servers*.
 *
 * \a lb_service_target_addresses is the target URI containing the addresses
 * from resolving the LB service's name (eg, ipv4:10.0.0.1:1234,10.2.3.4:9876).
 * \a client_channel_factory will be used for the creation of the LB channel,
 * alongside the channel args passed in \a args. */
grpc_channel* grpc_lb_policy_grpclb_create_lb_channel(
    const char* lb_service_target_addresses,
    grpc_client_channel_factory* client_channel_factory,
    grpc_channel_args* args);

grpc_channel_args* grpc_lb_policy_grpclb_build_lb_channel_args(
    grpc_slice_hash_table* targets_info,
    grpc_fake_resolver_response_generator* response_generator,
    const grpc_channel_args* args);

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_GRPCLB_GRPCLB_CHANNEL_H \
        */

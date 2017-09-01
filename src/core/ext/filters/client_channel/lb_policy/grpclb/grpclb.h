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

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_GRPCLB_GRPCLB_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_GRPCLB_GRPCLB_H

#include "src/core/ext/filters/client_channel/lb_policy_factory.h"

/** Returns a load balancing factory for the glb policy, which tries to connect
 * to a load balancing server to decide the next successfully connected
 * subchannel to pick. */
grpc_lb_policy_factory *grpc_glb_lb_factory_create();

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_GRPCLB_GRPCLB_H */

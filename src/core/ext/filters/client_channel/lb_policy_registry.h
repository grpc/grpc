/*
 *
 * Copyright 2015 gRPC authors.
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

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_REGISTRY_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_REGISTRY_H

#include "src/core/ext/filters/client_channel/lb_policy_factory.h"
#include "src/core/lib/iomgr/exec_ctx.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Initialize the registry and set \a default_factory as the factory to be
 * returned when no name is provided in a lookup */
void grpc_lb_policy_registry_init(void);
void grpc_lb_policy_registry_shutdown(void);

/** Register a LB policy factory. */
void grpc_register_lb_policy(grpc_lb_policy_factory* factory);

/** Create a \a grpc_lb_policy instance.
 *
 * If \a name is NULL, the default factory from \a grpc_lb_policy_registry_init
 * will be returned. */
grpc_lb_policy* grpc_lb_policy_create(grpc_exec_ctx* exec_ctx, const char* name,
                                      grpc_lb_policy_args* args);

#ifdef __cplusplus
}
#endif

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_REGISTRY_H */

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

#ifndef GRPC_CORE_LIB_IOMGR_NETWORK_STATUS_TRACKER_H
#define GRPC_CORE_LIB_IOMGR_NETWORK_STATUS_TRACKER_H
#include "src/core/lib/iomgr/endpoint.h"

void grpc_network_status_init(void);
void grpc_network_status_shutdown(void);

void grpc_network_status_register_endpoint(grpc_endpoint* ep);
void grpc_network_status_unregister_endpoint(grpc_endpoint* ep);
void grpc_network_status_shutdown_all_endpoints();

#endif /* GRPC_CORE_LIB_IOMGR_NETWORK_STATUS_TRACKER_H */

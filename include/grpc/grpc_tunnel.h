/*
 *
 * Copyright 2018 gRPC authors.
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

#ifndef GRPC_GRPC_TUNNEL_H
#define GRPC_GRPC_TUNNEL_H

#include "grpc.h"

#ifdef __cplusplus
extern "C" {
#endif

grpc_channel* grpc_tunnel_client_from_call(grpc_call* call, const grpc_channel_args* args);
void grpc_tunnel_server_from_call(grpc_call* call, grpc_server* server, const grpc_channel_args* args);

#ifdef __cplusplus
}
#endif

#endif

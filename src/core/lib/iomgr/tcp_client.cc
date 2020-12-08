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

#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/tcp_client.h"

grpc_tcp_client_vtable* grpc_tcp_client_impl;

void grpc_tcp_client_connect(grpc_closure* on_connect, grpc_endpoint** endpoint,
                             grpc_pollset_set* interested_parties,
                             const grpc_channel_args* channel_args,
                             const grpc_resolved_address* addr,
                             grpc_millis deadline) {
  grpc_tcp_client_impl->connect(on_connect, endpoint, interested_parties,
                                channel_args, addr, deadline);
}

void grpc_set_tcp_client_impl(grpc_tcp_client_vtable* impl) {
  grpc_tcp_client_impl = impl;
}

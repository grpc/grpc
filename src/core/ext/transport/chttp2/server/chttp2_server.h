/*
 *
 * Copyright 2016, gRPC authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
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

#include <grpc/impl/codegen/grpc_types.h>

#include "src/core/lib/iomgr/exec_ctx.h"

/// Adds a port to \a server.  Sets \a port_num to the port number.
/// Takes ownership of \a args.
grpc_error *grpc_chttp2_server_add_port(grpc_exec_ctx *exec_ctx,
                                        grpc_server *server, const char *addr,
                                        grpc_channel_args *args, int *port_num);

#endif /* GRPC_CORE_EXT_TRANSPORT_CHTTP2_SERVER_CHTTP2_SERVER_H */

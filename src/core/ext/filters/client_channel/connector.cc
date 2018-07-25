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

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/client_channel/connector.h"

grpc_connector* grpc_connector_ref(grpc_connector* connector) {
  connector->vtable->ref(connector);
  return connector;
}

void grpc_connector_unref(grpc_connector* connector) {
  connector->vtable->unref(connector);
}

void grpc_connector_connect(grpc_connector* connector,
                            const grpc_connect_in_args* in_args,
                            grpc_connect_out_args* out_args,
                            grpc_closure* notify) {
  connector->vtable->connect(connector, in_args, out_args, notify);
}

void grpc_connector_shutdown(grpc_connector* connector, grpc_error* why) {
  connector->vtable->shutdown(connector, why);
}

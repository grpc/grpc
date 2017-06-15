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

#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/transport/metadata.h"

void grpc_chttp2_plugin_init(void) {
  grpc_register_tracer("http", &grpc_http_trace);
  grpc_register_tracer("flowctl", &grpc_flowctl_trace);
}

void grpc_chttp2_plugin_shutdown(void) {}

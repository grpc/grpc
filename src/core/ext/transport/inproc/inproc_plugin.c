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

#include "src/core/ext/transport/inproc/inproc_transport.h"
#include "src/core/lib/debug/trace.h"

grpc_tracer_flag grpc_inproc_trace = GRPC_TRACER_INITIALIZER(false, "inproc");

void grpc_inproc_plugin_init(void) {
  grpc_register_tracer(&grpc_inproc_trace);
  grpc_inproc_transport_init();
}

void grpc_inproc_plugin_shutdown(void) { grpc_inproc_transport_shutdown(); }

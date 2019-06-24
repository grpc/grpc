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

#ifndef GRPC_TEST_CORE_UTIL_DEBUGGER_MACROS_H
#define GRPC_TEST_CORE_UTIL_DEBUGGER_MACROS_H

#include "src/core/ext/transport/chttp2/transport/internal.h"
#include "src/core/lib/surface/call.h"

grpc_chttp2_stream* grpc_chttp2_stream_from_call(grpc_call* call);

#endif /* GRPC_TEST_CORE_UTIL_DEBUGGER_MACROS_H */

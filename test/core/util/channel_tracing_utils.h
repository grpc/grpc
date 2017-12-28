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

#ifndef GRPC_TEST_CORE_UTIL_CHANNEL_TRACING_UTILS_H
#define GRPC_TEST_CORE_UTIL_CHANNEL_TRACING_UTILS_H

#include "src/core/lib/channel/channel_tracer.h"

void validate_json_array_size(grpc_json* json, const char* key,
                              size_t expected_size);

void validate_channel_data(grpc_json* json, size_t num_nodes_logged_expected,
                           size_t actual_num_nodes_expected);

#endif /* GRPC_TEST_CORE_UTIL_CHANNEL_TRACING_UTILS_H */

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

#include <stdlib.h>
#include <string.h>

#include <grpc/support/log.h>
#include "src/core/lib/channel/channel_tracer.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/json/json.h"

static grpc_json* get_json_child(grpc_json* parent, const char* key) {
  GPR_ASSERT(parent != nullptr);
  for (grpc_json* child = parent->child; child != nullptr;
       child = child->next) {
    if (child->key != nullptr && strcmp(child->key, key) == 0) return child;
  }
  return nullptr;
}

void validate_json_array_size(grpc_json* json, const char* key,
                              size_t expected_size) {
  grpc_json* arr = get_json_child(json, key);
  GPR_ASSERT(arr);
  GPR_ASSERT(arr->type == GRPC_JSON_ARRAY);
  size_t count = 0;
  for (grpc_json* child = arr->child; child != nullptr; child = child->next) {
    ++count;
  }
  GPR_ASSERT(count == expected_size);
}

void validate_channel_data(grpc_json* json, size_t num_nodes_logged_expected,
                           size_t actual_num_nodes_expected) {
  GPR_ASSERT(json);
  grpc_json* channel_data = get_json_child(json, "channelData");
  grpc_json* num_nodes_logged_json =
      get_json_child(channel_data, "numNodesLogged");
  GPR_ASSERT(num_nodes_logged_json);
  grpc_json* start_time = get_json_child(channel_data, "startTime");
  GPR_ASSERT(start_time);
  size_t num_nodes_logged =
      (size_t)strtol(num_nodes_logged_json->value, nullptr, 0);
  GPR_ASSERT(num_nodes_logged == num_nodes_logged_expected);
  validate_json_array_size(channel_data, "nodes", actual_num_nodes_expected);
}

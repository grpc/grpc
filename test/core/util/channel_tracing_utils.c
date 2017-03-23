/*
 *
 * Copyright 2017, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <stdlib.h>
#include <string.h>

#include <grpc/support/log.h>
#include <grpc/support/useful.h>
#include "src/core/lib/channel/channel_tracer.h"
#include "src/core/lib/json/json.h"

static grpc_json* get_json_child(grpc_json* parent, const char* key) {
  grpc_json* child = parent->child;
  while (child) {
    if (child->key && !strcmp(child->key, key)) {
      return child;
    }
    child = child->next;
  }
  return NULL;
}

static size_t get_json_array_size(grpc_json* arr) {
  GPR_ASSERT(arr->type == GRPC_JSON_ARRAY);
  size_t count = 0;
  grpc_json* child = arr->child;
  while (child) {
    count++;
    child = child->next;
  }
  return count;
}

void validate_channel_data(grpc_json* json, size_t num_nodes_logged_golden,
                           size_t actual_num_nodes_golden) {
  grpc_json* channel_data = get_json_child(json, "channelData");

  grpc_json* num_nodes_logged_json =
      get_json_child(channel_data, "numNodesLogged");
  GPR_ASSERT(num_nodes_logged_json);
  size_t num_nodes_logged =
      (size_t)strtol(num_nodes_logged_json->value, NULL, 0);
  GPR_ASSERT(num_nodes_logged == num_nodes_logged_golden);

  grpc_json* nodes = get_json_child(channel_data, "nodes");
  GPR_ASSERT(nodes);
  size_t actual_num_nodes = get_json_array_size(nodes);
  GPR_ASSERT(actual_num_nodes == actual_num_nodes_golden);
}
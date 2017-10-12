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

#include <string.h>

#include <grpc/support/alloc.h>

#include "src/core/lib/json/json.h"

grpc_json* grpc_json_create(grpc_json_type type) {
  grpc_json* json = (grpc_json*)gpr_zalloc(sizeof(*json));
  json->type = type;

  return json;
}

void grpc_json_destroy(grpc_json* json) {
  while (json->child) {
    grpc_json_destroy(json->child);
  }

  if (json->next) {
    json->next->prev = json->prev;
  }

  if (json->prev) {
    json->prev->next = json->next;
  } else if (json->parent) {
    json->parent->child = json->next;
  }

  gpr_free(json);
}

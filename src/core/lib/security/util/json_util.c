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

#include "src/core/lib/security/util/json_util.h"

#include <string.h>

#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

const char *grpc_json_get_string_property(const grpc_json *json,
                                          const char *prop_name) {
  grpc_json *child;
  for (child = json->child; child != NULL; child = child->next) {
    if (strcmp(child->key, prop_name) == 0) break;
  }
  if (child == NULL || child->type != GRPC_JSON_STRING) {
    gpr_log(GPR_ERROR, "Invalid or missing %s property.", prop_name);
    return NULL;
  }
  return child->value;
}

bool grpc_copy_json_string_property(const grpc_json *json,
                                    const char *prop_name,
                                    char **copied_value) {
  const char *prop_value = grpc_json_get_string_property(json, prop_name);
  if (prop_value == NULL) return false;
  *copied_value = gpr_strdup(prop_value);
  return true;
}

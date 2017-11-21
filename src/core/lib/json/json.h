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

#ifndef GRPC_CORE_LIB_JSON_JSON_H
#define GRPC_CORE_LIB_JSON_JSON_H

#include <stdlib.h>

#include "src/core/lib/json/json_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A tree-like structure to hold json values. The key and value pointers
 * are not owned by it.
 */
typedef struct grpc_json {
  struct grpc_json* next;
  struct grpc_json* prev;
  struct grpc_json* child;
  struct grpc_json* parent;

  grpc_json_type type;
  const char* key;
  const char* value;
} grpc_json;

/* The next two functions are going to parse the input string, and
 * modify it in the process, in order to use its space to store
 * all of the keys and values for the returned object tree.
 *
 * They assume UTF-8 input stream, and will output UTF-8 encoded
 * strings in the tree. The input stream's UTF-8 isn't validated,
 * as in, what you input is what you get as an output.
 *
 * All the keys and values in the grpc_json objects will be strings
 * pointing at your input buffer.
 *
 * Delete the allocated tree afterward using grpc_json_destroy().
 */
grpc_json* grpc_json_parse_string_with_len(char* input, size_t size);
grpc_json* grpc_json_parse_string(char* input);

/* This function will create a new string using gpr_realloc, and will
 * deserialize the grpc_json tree into it. It'll be zero-terminated,
 * but will be allocated in chunks of 256 bytes.
 *
 * The indent parameter controls the way the output is formatted.
 * If indent is 0, then newlines will be suppressed as well, and the
 * output will be condensed at its maximum.
 */
char* grpc_json_dump_to_string(grpc_json* json, int indent);

/* Use these to create or delete a grpc_json object.
 * Deletion is recursive. We will not attempt to free any of the strings
 * in any of the objects of that tree.
 */
grpc_json* grpc_json_create(grpc_json_type type);
void grpc_json_destroy(grpc_json* json);

#ifdef __cplusplus
}
#endif

#endif /* GRPC_CORE_LIB_JSON_JSON_H */

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

#include <stdlib.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/json/json.h"
#include "src/core/lib/json/json_writer.h"

/* This json writer will put everything in a big string.
 * The point is that we allocate that string in chunks of 256 bytes.
 */
typedef struct {
  char* output;
  size_t free_space;
  size_t string_len;
  size_t allocated;
} json_writer_userdata;

/* This function checks if there's enough space left in the output buffer,
 * and will enlarge it if necessary. We're only allocating chunks of 256
 * bytes at a time (or multiples thereof).
 */
static void json_writer_output_check(void* userdata, size_t needed) {
  json_writer_userdata* state = static_cast<json_writer_userdata*>(userdata);
  if (state->free_space >= needed) return;
  needed -= state->free_space;
  /* Round up by 256 bytes. */
  needed = (needed + 0xff) & ~0xffU;
  state->output =
      static_cast<char*>(gpr_realloc(state->output, state->allocated + needed));
  state->free_space += needed;
  state->allocated += needed;
}

/* These are needed by the writer's implementation. */
static void json_writer_output_char(void* userdata, char c) {
  json_writer_userdata* state = static_cast<json_writer_userdata*>(userdata);
  json_writer_output_check(userdata, 1);
  state->output[state->string_len++] = c;
  state->free_space--;
}

static void json_writer_output_string_with_len(void* userdata, const char* str,
                                               size_t len) {
  json_writer_userdata* state = static_cast<json_writer_userdata*>(userdata);
  json_writer_output_check(userdata, len);
  memcpy(state->output + state->string_len, str, len);
  state->string_len += len;
  state->free_space -= len;
}

static void json_writer_output_string(void* userdata, const char* str) {
  size_t len = strlen(str);
  json_writer_output_string_with_len(userdata, str, len);
}

static void json_dump_recursive(grpc_json_writer* writer, const grpc_json* json,
                                int in_object) {
  while (json) {
    if (in_object) grpc_json_writer_object_key(writer, json->key);

    switch (json->type) {
      case GRPC_JSON_OBJECT:
      case GRPC_JSON_ARRAY:
        grpc_json_writer_container_begins(writer, json->type);
        if (json->child)
          json_dump_recursive(writer, json->child,
                              json->type == GRPC_JSON_OBJECT);
        grpc_json_writer_container_ends(writer, json->type);
        break;
      case GRPC_JSON_STRING:
        grpc_json_writer_value_string(writer, json->value);
        break;
      case GRPC_JSON_NUMBER:
        grpc_json_writer_value_raw(writer, json->value);
        break;
      case GRPC_JSON_TRUE:
        grpc_json_writer_value_raw_with_len(writer, "true", 4);
        break;
      case GRPC_JSON_FALSE:
        grpc_json_writer_value_raw_with_len(writer, "false", 5);
        break;
      case GRPC_JSON_NULL:
        grpc_json_writer_value_raw_with_len(writer, "null", 4);
        break;
      default:
        GPR_UNREACHABLE_CODE(abort());
    }
    json = json->next;
  }
}

static grpc_json_writer_vtable writer_vtable = {
    json_writer_output_char, json_writer_output_string,
    json_writer_output_string_with_len};

char* grpc_json_dump_to_string(const grpc_json* json, int indent) {
  grpc_json_writer writer;
  json_writer_userdata state;

  state.output = nullptr;
  state.free_space = state.string_len = state.allocated = 0;
  grpc_json_writer_init(&writer, indent, &writer_vtable, &state);

  json_dump_recursive(&writer, json, 0);

  json_writer_output_char(&state, 0);

  return state.output;
}

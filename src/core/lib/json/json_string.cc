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
#include "src/core/lib/json/json_reader.h"
#include "src/core/lib/json/json_writer.h"

/* The json reader will construct a bunch of grpc_json objects and
 * link them all up together in a tree-like structure that will represent
 * the json data in memory.
 *
 * It also uses its own input as a scratchpad to store all of the decoded,
 * unescaped strings. So we need to keep track of all these pointers in
 * that opaque structure the reader will carry for us.
 *
 * Note that this works because the act of parsing json always reduces its
 * input size, and never expands it.
 */
typedef struct {
  grpc_json* top;
  grpc_json* current_container;
  grpc_json* current_value;
  uint8_t* input;
  uint8_t* key;
  uint8_t* string;
  uint8_t* string_ptr;
  size_t remaining_input;
} json_reader_userdata;

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

/* The reader asks us to clear our scratchpad. In our case, we'll simply mark
 * the end of the current string, and advance our output pointer.
 */
static void json_reader_string_clear(void* userdata) {
  json_reader_userdata* state = static_cast<json_reader_userdata*>(userdata);
  if (state->string) {
    GPR_ASSERT(state->string_ptr < state->input);
    *state->string_ptr++ = 0;
  }
  state->string = state->string_ptr;
}

static void json_reader_string_add_char(void* userdata, uint32_t c) {
  json_reader_userdata* state = static_cast<json_reader_userdata*>(userdata);
  GPR_ASSERT(state->string_ptr < state->input);
  GPR_ASSERT(c <= 0xff);
  *state->string_ptr++ = static_cast<uint8_t>(c);
}

/* We are converting a UTF-32 character into UTF-8 here,
 * as described by RFC3629.
 */
static void json_reader_string_add_utf32(void* userdata, uint32_t c) {
  if (c <= 0x7f) {
    json_reader_string_add_char(userdata, c);
  } else if (c <= 0x7ff) {
    uint32_t b1 = 0xc0 | ((c >> 6) & 0x1f);
    uint32_t b2 = 0x80 | (c & 0x3f);
    json_reader_string_add_char(userdata, b1);
    json_reader_string_add_char(userdata, b2);
  } else if (c <= 0xffff) {
    uint32_t b1 = 0xe0 | ((c >> 12) & 0x0f);
    uint32_t b2 = 0x80 | ((c >> 6) & 0x3f);
    uint32_t b3 = 0x80 | (c & 0x3f);
    json_reader_string_add_char(userdata, b1);
    json_reader_string_add_char(userdata, b2);
    json_reader_string_add_char(userdata, b3);
  } else if (c <= 0x1fffff) {
    uint32_t b1 = 0xf0 | ((c >> 18) & 0x07);
    uint32_t b2 = 0x80 | ((c >> 12) & 0x3f);
    uint32_t b3 = 0x80 | ((c >> 6) & 0x3f);
    uint32_t b4 = 0x80 | (c & 0x3f);
    json_reader_string_add_char(userdata, b1);
    json_reader_string_add_char(userdata, b2);
    json_reader_string_add_char(userdata, b3);
    json_reader_string_add_char(userdata, b4);
  }
}

/* We consider that the input may be a zero-terminated string. So we
 * can end up hitting eof before the end of the alleged string length.
 */
static uint32_t json_reader_read_char(void* userdata) {
  uint32_t r;
  json_reader_userdata* state = static_cast<json_reader_userdata*>(userdata);

  if (state->remaining_input == 0) return GRPC_JSON_READ_CHAR_EOF;

  r = *state->input++;
  state->remaining_input--;

  if (r == 0) {
    state->remaining_input = 0;
    return GRPC_JSON_READ_CHAR_EOF;
  }

  return r;
}

/* Helper function to create a new grpc_json object and link it into
 * our tree-in-progress inside our opaque structure.
 */
static grpc_json* json_create_and_link(void* userdata, grpc_json_type type) {
  json_reader_userdata* state = static_cast<json_reader_userdata*>(userdata);
  grpc_json* json = grpc_json_create(type);

  json->parent = state->current_container;
  json->prev = state->current_value;
  state->current_value = json;

  if (json->prev) {
    json->prev->next = json;
  }
  if (json->parent) {
    if (!json->parent->child) {
      json->parent->child = json;
    }
    if (json->parent->type == GRPC_JSON_OBJECT) {
      json->key = reinterpret_cast<char*>(state->key);
    }
  }
  if (!state->top) {
    state->top = json;
  }

  return json;
}

static void json_reader_container_begins(void* userdata, grpc_json_type type) {
  json_reader_userdata* state = static_cast<json_reader_userdata*>(userdata);
  grpc_json* container;

  GPR_ASSERT(type == GRPC_JSON_ARRAY || type == GRPC_JSON_OBJECT);

  container = json_create_and_link(userdata, type);
  state->current_container = container;
  state->current_value = nullptr;
}

/* It's important to remember that the reader is mostly stateless, so it
 * isn't trying to remember what the container was prior the one that just
 * ends. Since we're keeping track of these for our own purpose, we are
 * able to return that information back, which is useful for it to validate
 * the input json stream.
 *
 * Also note that if we're at the top of the tree, and the last container
 * ends, we have to return GRPC_JSON_TOP_LEVEL.
 */
static grpc_json_type json_reader_container_ends(void* userdata) {
  grpc_json_type container_type = GRPC_JSON_TOP_LEVEL;
  json_reader_userdata* state = static_cast<json_reader_userdata*>(userdata);

  GPR_ASSERT(state->current_container);

  state->current_value = state->current_container;
  state->current_container = state->current_container->parent;

  if (state->current_container) {
    container_type = state->current_container->type;
  }

  return container_type;
}

/* The next 3 functions basically are the reader asking us to use our string
 * scratchpad for one of these 3 purposes.
 *
 * Note that in the set_number case, we're not going to try interpreting it.
 * We'll keep it as a string, and leave it to the caller to evaluate it.
 */
static void json_reader_set_key(void* userdata) {
  json_reader_userdata* state = static_cast<json_reader_userdata*>(userdata);
  state->key = state->string;
}

static void json_reader_set_string(void* userdata) {
  json_reader_userdata* state = static_cast<json_reader_userdata*>(userdata);
  grpc_json* json = json_create_and_link(userdata, GRPC_JSON_STRING);
  json->value = reinterpret_cast<char*>(state->string);
}

static int json_reader_set_number(void* userdata) {
  json_reader_userdata* state = static_cast<json_reader_userdata*>(userdata);
  grpc_json* json = json_create_and_link(userdata, GRPC_JSON_NUMBER);
  json->value = reinterpret_cast<char*>(state->string);
  return 1;
}

/* The object types true, false and null are self-sufficient, and don't need
 * any more information beside their type.
 */
static void json_reader_set_true(void* userdata) {
  json_create_and_link(userdata, GRPC_JSON_TRUE);
}

static void json_reader_set_false(void* userdata) {
  json_create_and_link(userdata, GRPC_JSON_FALSE);
}

static void json_reader_set_null(void* userdata) {
  json_create_and_link(userdata, GRPC_JSON_NULL);
}

static grpc_json_reader_vtable reader_vtable = {
    json_reader_string_clear,     json_reader_string_add_char,
    json_reader_string_add_utf32, json_reader_read_char,
    json_reader_container_begins, json_reader_container_ends,
    json_reader_set_key,          json_reader_set_string,
    json_reader_set_number,       json_reader_set_true,
    json_reader_set_false,        json_reader_set_null};

/* And finally, let's define our public API. */
grpc_json* grpc_json_parse_string_with_len(char* input, size_t size) {
  grpc_json_reader reader;
  json_reader_userdata state;
  grpc_json* json = nullptr;
  grpc_json_reader_status status;

  if (!input) return nullptr;

  state.top = state.current_container = state.current_value = nullptr;
  state.string = state.key = nullptr;
  state.string_ptr = state.input = reinterpret_cast<uint8_t*>(input);
  state.remaining_input = size;
  grpc_json_reader_init(&reader, &reader_vtable, &state);

  status = grpc_json_reader_run(&reader);
  json = state.top;

  if ((status != GRPC_JSON_DONE) && json) {
    grpc_json_destroy(json);
    json = nullptr;
  }

  return json;
}

#define UNBOUND_JSON_STRING_LENGTH 0x7fffffff

grpc_json* grpc_json_parse_string(char* input) {
  return grpc_json_parse_string_with_len(input, UNBOUND_JSON_STRING_LENGTH);
}

static void json_dump_recursive(grpc_json_writer* writer, grpc_json* json,
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

char* grpc_json_dump_to_string(grpc_json* json, int indent) {
  grpc_json_writer writer;
  json_writer_userdata state;

  state.output = nullptr;
  state.free_space = state.string_len = state.allocated = 0;
  grpc_json_writer_init(&writer, indent, &writer_vtable, &state);

  json_dump_recursive(&writer, json, 0);

  json_writer_output_char(&state, 0);

  return state.output;
}

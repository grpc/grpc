/*
 *
 * Copyright 2014, Google Inc.
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

#include <string.h>
#include <stdlib.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/json/json.h"
#include "src/core/json/json_reader.h"
#include "src/core/json/json_writer.h"

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
  char* input;
  char* key;
  char* string;
  char* string_ptr;
  size_t remaining_input;
} grpc_json_reader_opaque;

/* This json writer will put everything in a big string.
 * The point is that we allocate that string in chunks of 256 bytes.
 */
typedef struct {
  char* output;
  size_t free_space, string_len, allocated;
} grpc_json_writer_opaque;


/* This function checks if there's enough space left in the output buffer,
 * and will enlarge it if necessary. We're only allocating chunks of 256
 * bytes at a time (or multiples thereof).
 */
static void grpc_json_writer_output_check(grpc_json_writer* writer,
                                          size_t needed) {
  grpc_json_writer_opaque* state = writer->userdata;
  if (state->free_space >= needed) return;
  needed -= state->free_space;
  /* Round up by 256 bytes. */
  needed = (needed + 0xff) & ~0xff;
  state->output = gpr_realloc(state->output, state->allocated + needed);
  state->free_space += needed;
  state->allocated += needed;
}

/* These are needed by the writer's implementation. */
static void grpc_json_writer_output_char(grpc_json_writer* writer,
                                         char c) {
  grpc_json_writer_opaque* state = writer->userdata;
  grpc_json_writer_output_check(writer, 1);
  state->output[state->string_len++] = c;
  state->free_space--;
}

static void grpc_json_writer_output_string_with_len(
    grpc_json_writer* writer, const char* str, size_t len) {
  grpc_json_writer_opaque* state = writer->userdata;
  grpc_json_writer_output_check(writer, len);
  memcpy(state->output + state->string_len, str, len);
  state->string_len += len;
  state->free_space -= len;
}

static void grpc_json_writer_output_string(grpc_json_writer* writer,
                                           const char* str) {
  size_t len = strlen(str);
  grpc_json_writer_output_string_with_len(writer, str, len);
}

/* The reader asks us to clear our scratchpad. In our case, we'll simply mark
 * the end of the current string, and advance our output pointer.
 */
static void grpc_json_reader_string_clear(grpc_json_reader* reader) {
  grpc_json_reader_opaque* state = reader->userdata;
  if (state->string) {
    GPR_ASSERT(state->string_ptr < state->input);
    *state->string_ptr++ = 0;
  }
  state->string = state->string_ptr;
}

static void grpc_json_reader_string_add_char(grpc_json_reader* reader, gpr_uint32 c) {
  grpc_json_reader_opaque* state = reader->userdata;
  GPR_ASSERT(state->string_ptr < state->input);
  GPR_ASSERT(c <= 0xff);
  *state->string_ptr++ = (char)c;
}

/* We are converting a UTF-32 character into UTF-8 here. */
static void grpc_json_reader_string_add_utf32(grpc_json_reader* reader, gpr_uint32 c) {
  if (c <= 0x7f) {
    grpc_json_reader_string_add_char(reader, c);
  } else if (c <= 0x7ff) {
    int b1 = 0xc0 | ((c >> 6) & 0x1f);
    int b2 = 0x80 | (c & 0x3f);
    grpc_json_reader_string_add_char(reader, b1);
    grpc_json_reader_string_add_char(reader, b2);
  } else if (c <= 0xffff) {
    int b1 = 0xe0 | ((c >> 12) & 0x0f);
    int b2 = 0x80 | ((c >> 6) & 0x3f);
    int b3 = 0x80 | (c & 0x3f);
    grpc_json_reader_string_add_char(reader, b1);
    grpc_json_reader_string_add_char(reader, b2);
    grpc_json_reader_string_add_char(reader, b3);
  } else if (c <= 0x1fffff) {
    int b1 = 0xf0 | ((c >> 18) & 0x07);
    int b2 = 0x80 | ((c >> 12) & 0x3f);
    int b3 = 0x80 | ((c >> 6) & 0x3f);
    int b4 = 0x80 | (c & 0x3f);
    grpc_json_reader_string_add_char(reader, b1);
    grpc_json_reader_string_add_char(reader, b2);
    grpc_json_reader_string_add_char(reader, b3);
    grpc_json_reader_string_add_char(reader, b4);
  }
}

/* We consider that the input may be a zero-terminated string. So we
 * can end up hitting eof before the end of the alleged string length.
 */
static gpr_uint32 grpc_json_reader_read_char(grpc_json_reader* reader) {
  gpr_uint32 r;
  grpc_json_reader_opaque* state = reader->userdata;

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
static grpc_json* grpc_json_new_and_link(grpc_json_reader* reader,
                                         grpc_json_type type) {
  grpc_json_reader_opaque* state = reader->userdata;
  grpc_json* json = grpc_json_new(type);

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
      json->key = state->key;
    }
  }
  if (!state->top) {
    state->top = json;
  }

  return json;
}

static void grpc_json_reader_container_begins(grpc_json_reader* reader,
                                              grpc_json_type type) {
  grpc_json_reader_opaque* state = reader->userdata;
  grpc_json* container;

  GPR_ASSERT(type == GRPC_JSON_ARRAY || type == GRPC_JSON_OBJECT);

  container = grpc_json_new_and_link(reader, type);
  state->current_container = container;
  state->current_value = NULL;
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
static grpc_json_type grpc_json_reader_container_ends(
    grpc_json_reader* reader) {
  grpc_json_type container_type = GRPC_JSON_TOP_LEVEL;
  grpc_json_reader_opaque* state = reader->userdata;

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
static void grpc_json_reader_set_key(grpc_json_reader* reader) {
  grpc_json_reader_opaque* state = reader->userdata;
  state->key = state->string;
}

static void grpc_json_reader_set_string(
    grpc_json_reader* reader) {
  grpc_json_reader_opaque* state = reader->userdata;
  grpc_json* json = grpc_json_new_and_link(reader, GRPC_JSON_STRING);
  json->value = state->string;
}

static int grpc_json_reader_set_number(
    grpc_json_reader* reader) {
  grpc_json_reader_opaque* state = reader->userdata;
  grpc_json* json = grpc_json_new_and_link(reader, GRPC_JSON_NUMBER);
  json->value = state->string;
  return 1;
}

/* The object types true, false and null are self-sufficient, and don't need
 * any more information beside their type.
 */
static void grpc_json_reader_set_true(
    grpc_json_reader *reader) {
  grpc_json_new_and_link(reader, GRPC_JSON_TRUE);
}

static void grpc_json_reader_set_false(
    grpc_json_reader *reader) {
  grpc_json_new_and_link(reader, GRPC_JSON_FALSE);
}

static void grpc_json_reader_set_null(
    grpc_json_reader *reader) {
  grpc_json_new_and_link(reader, GRPC_JSON_NULL);
}

/* And finally, let's define our public API. */
grpc_json* grpc_json_parse_string_with_len(char* input, size_t size) {
  grpc_json_reader reader;
  grpc_json_reader_opaque state;
  grpc_json *json = NULL;
  grpc_json_reader_ret status;

  if (!input) return NULL;

  state.top = state.current_container = state.current_value = NULL;
  state.string = state.key = NULL;
  state.string_ptr = state.input = input;
  state.remaining_input = size;
  reader.userdata = &state;
  reader.string_clear = grpc_json_reader_string_clear;
  reader.string_add_char = grpc_json_reader_string_add_char;
  reader.string_add_utf32 = grpc_json_reader_string_add_utf32;
  reader.read_char = grpc_json_reader_read_char;
  reader.container_begins = grpc_json_reader_container_begins;
  reader.container_ends = grpc_json_reader_container_ends;
  reader.set_key = grpc_json_reader_set_key;
  reader.set_string = grpc_json_reader_set_string;
  reader.set_number = grpc_json_reader_set_number;
  reader.set_true = grpc_json_reader_set_true;
  reader.set_false = grpc_json_reader_set_false;
  reader.set_null = grpc_json_reader_set_null;
  grpc_json_reader_init(&reader);

  status = grpc_json_reader_run(&reader);
  json = state.top;

  if ((status != GRPC_JSON_DONE) && json) {
    grpc_json_delete(json);
    json = NULL;
  }

  return json;
}

#define UNBOUND_JSON_STRING_LENGTH 0x7fffffff

grpc_json* grpc_json_parse_string(char* input) {
  return grpc_json_parse_string_with_len(input, UNBOUND_JSON_STRING_LENGTH);
}

static void grpc_json_dump_recursive(grpc_json_writer* writer,
                                     grpc_json* json, int in_object) {
  while (json) {
    if (in_object) grpc_json_writer_object_key(writer, json->key);

    switch (json->type) {
      case GRPC_JSON_OBJECT:
      case GRPC_JSON_ARRAY:
        grpc_json_writer_container_begins(writer, json->type);
        if (json->child)
          grpc_json_dump_recursive(writer, json->child,
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
        abort();
    }
    json = json->next;
  }
}

char* grpc_json_dump_to_string(grpc_json* json, int indent) {
  grpc_json_writer writer;
  grpc_json_writer_opaque state;
  state.output = NULL;
  state.free_space = state.string_len = state.allocated = 0;
  writer.userdata = &state;
  writer.output_char = grpc_json_writer_output_char;
  writer.output_string = grpc_json_writer_output_string;
  writer.output_string_with_len = grpc_json_writer_output_string_with_len;
  grpc_json_writer_init(&writer, indent);

  grpc_json_dump_recursive(&writer, json, 0);

  grpc_json_writer_output_char(&writer, 0);

  return state.output;
}

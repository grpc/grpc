/*
 *
 * Copyright 2015, Google Inc.
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

#include <stdio.h>
#include <stdlib.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/useful.h>
#include "test/core/util/test_config.h"

#include "src/core/lib/json/json_reader.h"
#include "src/core/lib/json/json_writer.h"

typedef struct json_writer_userdata { FILE *cmp; } json_writer_userdata;

typedef struct stacked_container {
  grpc_json_type type;
  struct stacked_container *next;
} stacked_container;

typedef struct json_reader_userdata {
  FILE *in;
  grpc_json_writer *writer;
  char *scratchpad;
  char *ptr;
  size_t free_space;
  size_t allocated;
  size_t string_len;
  stacked_container *top;
  int did_eagain;
} json_reader_userdata;

static void json_writer_output_char(void *userdata, char c) {
  json_writer_userdata *state = userdata;
  int cmp = fgetc(state->cmp);

  /* treat CRLF as LF */
  if (cmp == '\r' && c == '\n') {
    cmp = fgetc(state->cmp);
  }
  GPR_ASSERT(cmp == c);
}

static void json_writer_output_string(void *userdata, const char *str) {
  while (*str) {
    json_writer_output_char(userdata, *str++);
  }
}

static void json_writer_output_string_with_len(void *userdata, const char *str,
                                               size_t len) {
  size_t i;
  for (i = 0; i < len; i++) {
    json_writer_output_char(userdata, str[i]);
  }
}

grpc_json_writer_vtable writer_vtable = {json_writer_output_char,
                                         json_writer_output_string,
                                         json_writer_output_string_with_len};

static void check_string(json_reader_userdata *state, size_t needed) {
  if (state->free_space >= needed) return;
  needed -= state->free_space;
  needed = (needed + 0xffu) & ~0xffu;
  state->scratchpad = gpr_realloc(state->scratchpad, state->allocated + needed);
  state->free_space += needed;
  state->allocated += needed;
}

static void json_reader_string_clear(void *userdata) {
  json_reader_userdata *state = userdata;
  state->free_space = state->allocated;
  state->string_len = 0;
}

static void json_reader_string_add_char(void *userdata, uint32_t c) {
  json_reader_userdata *state = userdata;
  check_string(state, 1);
  GPR_ASSERT(c <= 256);
  state->scratchpad[state->string_len++] = (char)c;
}

static void json_reader_string_add_utf32(void *userdata, uint32_t c) {
  if (c <= 0x7f) {
    json_reader_string_add_char(userdata, c);
  } else if (c <= 0x7ffu) {
    uint32_t b1 = 0xc0u | ((c >> 6u) & 0x1fu);
    uint32_t b2 = 0x80u | (c & 0x3fu);
    json_reader_string_add_char(userdata, b1);
    json_reader_string_add_char(userdata, b2);
  } else if (c <= 0xffffu) {
    uint32_t b1 = 0xe0u | ((c >> 12u) & 0x0fu);
    uint32_t b2 = 0x80u | ((c >> 6u) & 0x3fu);
    uint32_t b3 = 0x80u | (c & 0x3fu);
    json_reader_string_add_char(userdata, b1);
    json_reader_string_add_char(userdata, b2);
    json_reader_string_add_char(userdata, b3);
  } else if (c <= 0x1fffffu) {
    uint32_t b1 = 0xf0u | ((c >> 18u) & 0x07u);
    uint32_t b2 = 0x80u | ((c >> 12u) & 0x3fu);
    uint32_t b3 = 0x80u | ((c >> 6u) & 0x3fu);
    uint32_t b4 = 0x80u | (c & 0x3fu);
    json_reader_string_add_char(userdata, b1);
    json_reader_string_add_char(userdata, b2);
    json_reader_string_add_char(userdata, b3);
    json_reader_string_add_char(userdata, b4);
  }
}

static uint32_t json_reader_read_char(void *userdata) {
  int r;
  json_reader_userdata *state = userdata;

  if (!state->did_eagain) {
    state->did_eagain = 1;
    return GRPC_JSON_READ_CHAR_EAGAIN;
  }

  state->did_eagain = 0;

  r = fgetc(state->in);
  if (r == EOF) r = GRPC_JSON_READ_CHAR_EOF;
  return (uint32_t)r;
}

static void json_reader_container_begins(void *userdata, grpc_json_type type) {
  json_reader_userdata *state = userdata;
  stacked_container *container = gpr_malloc(sizeof(stacked_container));

  container->type = type;
  container->next = state->top;
  state->top = container;

  grpc_json_writer_container_begins(state->writer, type);
}

static grpc_json_type json_reader_container_ends(void *userdata) {
  json_reader_userdata *state = userdata;
  stacked_container *container = state->top;

  grpc_json_writer_container_ends(state->writer, container->type);
  state->top = container->next;
  gpr_free(container);
  return state->top ? state->top->type : GRPC_JSON_TOP_LEVEL;
}

static void json_reader_set_key(void *userdata) {
  json_reader_userdata *state = userdata;
  json_reader_string_add_char(userdata, 0);

  grpc_json_writer_object_key(state->writer, state->scratchpad);
}

static void json_reader_set_string(void *userdata) {
  json_reader_userdata *state = userdata;
  json_reader_string_add_char(userdata, 0);

  grpc_json_writer_value_string(state->writer, state->scratchpad);
}

static int json_reader_set_number(void *userdata) {
  json_reader_userdata *state = userdata;

  grpc_json_writer_value_raw_with_len(state->writer, state->scratchpad,
                                      state->string_len);

  return 1;
}

static void json_reader_set_true(void *userdata) {
  json_reader_userdata *state = userdata;

  grpc_json_writer_value_raw_with_len(state->writer, "true", 4);
}

static void json_reader_set_false(void *userdata) {
  json_reader_userdata *state = userdata;

  grpc_json_writer_value_raw_with_len(state->writer, "false", 5);
}

static void json_reader_set_null(void *userdata) {
  json_reader_userdata *state = userdata;

  grpc_json_writer_value_raw_with_len(state->writer, "null", 4);
}

static grpc_json_reader_vtable reader_vtable = {
    json_reader_string_clear,     json_reader_string_add_char,
    json_reader_string_add_utf32, json_reader_read_char,
    json_reader_container_begins, json_reader_container_ends,
    json_reader_set_key,          json_reader_set_string,
    json_reader_set_number,       json_reader_set_true,
    json_reader_set_false,        json_reader_set_null};

int rewrite_and_compare(FILE *in, FILE *cmp, int indent) {
  grpc_json_writer writer;
  grpc_json_reader reader;
  grpc_json_reader_status status;
  json_writer_userdata writer_user;
  json_reader_userdata reader_user;

  GPR_ASSERT(in);
  GPR_ASSERT(cmp);

  reader_user.writer = &writer;
  reader_user.in = in;
  reader_user.top = NULL;
  reader_user.scratchpad = NULL;
  reader_user.string_len = 0;
  reader_user.free_space = 0;
  reader_user.allocated = 0;
  reader_user.did_eagain = 0;

  writer_user.cmp = cmp;

  grpc_json_writer_init(&writer, indent, &writer_vtable, &writer_user);
  grpc_json_reader_init(&reader, &reader_vtable, &reader_user);

  do {
    status = grpc_json_reader_run(&reader);
  } while (status == GRPC_JSON_EAGAIN);

  free(reader_user.scratchpad);
  while (reader_user.top) {
    stacked_container *container = reader_user.top;
    reader_user.top = container->next;
    free(container);
  }

  return status == GRPC_JSON_DONE;
}

typedef struct test_file {
  const char *input;
  const char *cmp;
  int indent;
} test_file;

static test_file test_files[] = {
    {"test/core/json/rewrite_test_input.json",
     "test/core/json/rewrite_test_output_condensed.json", 0},
    {"test/core/json/rewrite_test_input.json",
     "test/core/json/rewrite_test_output_indented.json", 2},
    {"test/core/json/rewrite_test_output_indented.json",
     "test/core/json/rewrite_test_output_condensed.json", 0},
    {"test/core/json/rewrite_test_output_condensed.json",
     "test/core/json/rewrite_test_output_indented.json", 2},
};

void test_rewrites() {
  unsigned i;

  for (i = 0; i < GPR_ARRAY_SIZE(test_files); i++) {
    test_file *test = test_files + i;
    FILE *input = fopen(test->input, "rb");
    FILE *cmp = fopen(test->cmp, "rb");
    int status;
    gpr_log(GPR_INFO, "Testing file %s against %s using indent=%i", test->input,
            test->cmp, test->indent);
    status = rewrite_and_compare(input, cmp, test->indent);
    GPR_ASSERT(status);
    fclose(input);
    fclose(cmp);
  }
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  test_rewrites();
  gpr_log(GPR_INFO, "json_rewrite_test success");
  return 0;
}

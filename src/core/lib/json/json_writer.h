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

/* The idea of the writer is basically symmetrical of the reader. While the
 * reader emits various calls to your code, the writer takes basically the
 * same calls and emit json out of it. It doesn't try to make any check on
 * the order of the calls you do on it. Meaning you can theorically force
 * it to generate invalid json.
 *
 * Also, unlike the reader, the writer expects UTF-8 encoded input strings.
 * These strings will be UTF-8 validated, and any invalid character will
 * cut the conversion short, before any invalid UTF-8 sequence, thus forming
 * a valid UTF-8 string overall.
 */

#ifndef GRPC_CORE_LIB_JSON_JSON_WRITER_H
#define GRPC_CORE_LIB_JSON_JSON_WRITER_H

#include <stdlib.h>

#include "src/core/lib/json/json_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct grpc_json_writer_vtable {
  /* Adds a character to the output stream. */
  void (*output_char)(void* userdata, char);
  /* Adds a zero-terminated string to the output stream. */
  void (*output_string)(void* userdata, const char* str);
  /* Adds a fixed-length string to the output stream. */
  void (*output_string_with_len)(void* userdata, const char* str, size_t len);

} grpc_json_writer_vtable;

typedef struct grpc_json_writer {
  void* userdata;
  grpc_json_writer_vtable* vtable;
  int indent;
  int depth;
  int container_empty;
  int got_key;
} grpc_json_writer;

/* Call this to initialize your writer structure. The indent parameter is
 * specifying the number of spaces to use for indenting the output. If you
 * use indent=0, then the output will not have any newlines either, thus
 * emitting a condensed json output.
 */
void grpc_json_writer_init(grpc_json_writer* writer, int indent,
                           grpc_json_writer_vtable* vtable, void* userdata);

/* Signals the beginning of a container. */
void grpc_json_writer_container_begins(grpc_json_writer* writer,
                                       grpc_json_type type);
/* Signals the end of a container. */
void grpc_json_writer_container_ends(grpc_json_writer* writer,
                                     grpc_json_type type);
/* Writes down an object key for the next value. */
void grpc_json_writer_object_key(grpc_json_writer* writer, const char* string);
/* Sets a raw value. Useful for numbers. */
void grpc_json_writer_value_raw(grpc_json_writer* writer, const char* string);
/* Sets a raw value with its length. Useful for values like true or false. */
void grpc_json_writer_value_raw_with_len(grpc_json_writer* writer,
                                         const char* string, size_t len);
/* Sets a string value. It'll be escaped, and utf-8 validated. */
void grpc_json_writer_value_string(grpc_json_writer* writer,
                                   const char* string);

#ifdef __cplusplus
}
#endif

#endif /* GRPC_CORE_LIB_JSON_JSON_WRITER_H */

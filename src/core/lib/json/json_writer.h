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

typedef struct grpc_json_writer_vtable {
  /* Adds a character to the output stream. */
  void (*output_char)(void *userdata, char);
  /* Adds a zero-terminated string to the output stream. */
  void (*output_string)(void *userdata, const char *str);
  /* Adds a fixed-length string to the output stream. */
  void (*output_string_with_len)(void *userdata, const char *str, size_t len);

} grpc_json_writer_vtable;

typedef struct grpc_json_writer {
  void *userdata;
  grpc_json_writer_vtable *vtable;
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
void grpc_json_writer_init(grpc_json_writer *writer, int indent,
                           grpc_json_writer_vtable *vtable, void *userdata);

/* Signals the beginning of a container. */
void grpc_json_writer_container_begins(grpc_json_writer *writer,
                                       grpc_json_type type);
/* Signals the end of a container. */
void grpc_json_writer_container_ends(grpc_json_writer *writer,
                                     grpc_json_type type);
/* Writes down an object key for the next value. */
void grpc_json_writer_object_key(grpc_json_writer *writer, const char *string);
/* Sets a raw value. Useful for numbers. */
void grpc_json_writer_value_raw(grpc_json_writer *writer, const char *string);
/* Sets a raw value with its length. Useful for values like true or false. */
void grpc_json_writer_value_raw_with_len(grpc_json_writer *writer,
                                         const char *string, size_t len);
/* Sets a string value. It'll be escaped, and utf-8 validated. */
void grpc_json_writer_value_string(grpc_json_writer *writer,
                                   const char *string);

#endif /* GRPC_CORE_LIB_JSON_JSON_WRITER_H */

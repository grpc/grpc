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

#ifndef GRPC_CORE_LIB_JSON_JSON_READER_H
#define GRPC_CORE_LIB_JSON_JSON_READER_H

#include <grpc/support/port_platform.h>
#include "src/core/lib/json/json_common.h"

typedef enum {
  GRPC_JSON_STATE_OBJECT_KEY_BEGIN,
  GRPC_JSON_STATE_OBJECT_KEY_STRING,
  GRPC_JSON_STATE_OBJECT_KEY_END,
  GRPC_JSON_STATE_VALUE_BEGIN,
  GRPC_JSON_STATE_VALUE_STRING,
  GRPC_JSON_STATE_STRING_ESCAPE,
  GRPC_JSON_STATE_STRING_ESCAPE_U1,
  GRPC_JSON_STATE_STRING_ESCAPE_U2,
  GRPC_JSON_STATE_STRING_ESCAPE_U3,
  GRPC_JSON_STATE_STRING_ESCAPE_U4,
  GRPC_JSON_STATE_VALUE_NUMBER,
  GRPC_JSON_STATE_VALUE_NUMBER_WITH_DECIMAL,
  GRPC_JSON_STATE_VALUE_NUMBER_ZERO,
  GRPC_JSON_STATE_VALUE_NUMBER_DOT,
  GRPC_JSON_STATE_VALUE_NUMBER_E,
  GRPC_JSON_STATE_VALUE_NUMBER_EPM,
  GRPC_JSON_STATE_VALUE_TRUE_R,
  GRPC_JSON_STATE_VALUE_TRUE_U,
  GRPC_JSON_STATE_VALUE_TRUE_E,
  GRPC_JSON_STATE_VALUE_FALSE_A,
  GRPC_JSON_STATE_VALUE_FALSE_L,
  GRPC_JSON_STATE_VALUE_FALSE_S,
  GRPC_JSON_STATE_VALUE_FALSE_E,
  GRPC_JSON_STATE_VALUE_NULL_U,
  GRPC_JSON_STATE_VALUE_NULL_L1,
  GRPC_JSON_STATE_VALUE_NULL_L2,
  GRPC_JSON_STATE_VALUE_END,
  GRPC_JSON_STATE_END
} grpc_json_reader_state;

enum {
  /* The first non-unicode value is 0x110000. But let's pick
   * a value high enough to start our error codes from. These
   * values are safe to return from the read_char function.
   */
  GRPC_JSON_READ_CHAR_EOF = 0x7ffffff0,
  GRPC_JSON_READ_CHAR_EAGAIN,
  GRPC_JSON_READ_CHAR_ERROR
};

struct grpc_json_reader;

typedef struct grpc_json_reader_vtable {
  /* Clears your internal string scratchpad. */
  void (*string_clear)(void *userdata);
  /* Adds a char to the string scratchpad. */
  void (*string_add_char)(void *userdata, uint32_t c);
  /* Adds a utf32 char to the string scratchpad. */
  void (*string_add_utf32)(void *userdata, uint32_t c);
  /* Reads a character from your input. May be utf-8, 16 or 32. */
  uint32_t (*read_char)(void *userdata);
  /* Starts a container of type GRPC_JSON_ARRAY or GRPC_JSON_OBJECT. */
  void (*container_begins)(void *userdata, grpc_json_type type);
  /* Ends the current container. Must return the type of its parent. */
  grpc_json_type (*container_ends)(void *userdata);
  /* Your internal string scratchpad is an object's key. */
  void (*set_key)(void *userdata);
  /* Your internal string scratchpad is a string value. */
  void (*set_string)(void *userdata);
  /* Your internal string scratchpad is a numerical value. Return 1 if valid. */
  int (*set_number)(void *userdata);
  /* Sets the values true, false or null. */
  void (*set_true)(void *userdata);
  void (*set_false)(void *userdata);
  void (*set_null)(void *userdata);
} grpc_json_reader_vtable;

typedef struct grpc_json_reader {
  /* That structure is fully private, and initialized by grpc_json_reader_init.
   * The definition is public so you can put it on your stack.
   */

  void *userdata;
  grpc_json_reader_vtable *vtable;
  int depth;
  int in_object;
  int in_array;
  int escaped_string_was_key;
  int container_just_begun;
  uint16_t unicode_char, unicode_high_surrogate;
  grpc_json_reader_state state;
} grpc_json_reader;

/* The return type of the parser. */
typedef enum {
  GRPC_JSON_DONE,          /* The parser finished successfully. */
  GRPC_JSON_EAGAIN,        /* The parser yields to get more data. */
  GRPC_JSON_READ_ERROR,    /* The parser passes through a read error. */
  GRPC_JSON_PARSE_ERROR,   /* The parser found an error in the json stream. */
  GRPC_JSON_INTERNAL_ERROR /* The parser got an internal error. */
} grpc_json_reader_status;

/* Call this function to start parsing the input. It will return the following:
 *    . GRPC_JSON_DONE if the input got eof, and the parsing finished
 *      successfully.
 *    . GRPC_JSON_EAGAIN if the read_char function returned again. Call the
 *      parser again as needed. It is okay to call the parser in polling mode,
 *      although a bit dull.
 *    . GRPC_JSON_READ_ERROR if the read_char function returned an error. The
 *      state isn't broken however, and the function can be called again if the
 *      error has been corrected. But please use the EAGAIN feature instead for
 *      consistency.
 *    . GRPC_JSON_PARSE_ERROR if the input was somehow invalid.
 *    . GRPC_JSON_INTERNAL_ERROR if the parser somehow ended into an invalid
 *      internal state.
 */
grpc_json_reader_status grpc_json_reader_run(grpc_json_reader *reader);

/* Call this function to initialize the reader structure. */
void grpc_json_reader_init(grpc_json_reader *reader,
                           grpc_json_reader_vtable *vtable, void *userdata);

/* You may call this from the read_char callback if you don't know where is the
 * end of your input stream, and you'd like the json reader to hint you that it
 * has completed reading its input, so you can return an EOF to it. Note that
 * there might still be trailing whitespaces after that point.
 */
int grpc_json_reader_is_complete(grpc_json_reader *reader);

#endif /* GRPC_CORE_LIB_JSON_JSON_READER_H */

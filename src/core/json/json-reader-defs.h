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

/* the following need to be pre-defined:
 *    grpc_json_reader_opaque_t  // A type you can use to keep track of your
 *                               // own stuff.
 *    grpc_json_wchar_t          // A type that can hold a unicode character
 *                               // unsigned is good enough.
 *    grpc_json_string_t         // A type that can hold a growable string.
 */

enum grpc_json_reader_state_t {
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
};

struct grpc_json_reader_t {
  /* You are responsible for the initialization of the following. */
  grpc_json_reader_opaque_t opaque;

  /* Everything down here is private,
     and initialized by grpc_json_reader_init. */
  int depth;
  int in_object;
  int in_array;
  int escaped_string_was_key;
  int container_just_begun;
  grpc_json_wchar_t unicode;
  enum grpc_json_reader_state_t state;
};

/* The return type of the parser. */
typedef enum {
  GRPC_JSON_DONE,          /* The parser finished successfully. */
  GRPC_JSON_EAGAIN,        /* The parser yields to get more data. */
  GRPC_JSON_READ_ERROR,    /* The parser passes through a read error. */
  GRPC_JSON_PARSE_ERROR,   /* The parser found an error in the json stream. */
  GRPC_JSON_INTERNAL_ERROR /* The parser got an internal error. */
} grpc_json_reader_ret_t;

/*
 *
 * Copyright 2015-2016 gRPC authors.
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

#include <string.h>

#include <grpc/support/log.h>

#include "src/core/lib/json/json.h"

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

typedef struct grpc_json_reader {
  /* That structure is fully private, and initialized by grpc_json_reader_init.
   * The definition is public so you can put it on your stack.
   */

  int depth;
  int in_object;
  int in_array;
  int escaped_string_was_key;
  int container_just_begun;
  uint16_t unicode_char, unicode_high_surrogate;
  grpc_json_reader_state state;

  grpc_json* top;
  grpc_json* current_container;
  grpc_json* current_value;
  uint8_t* input;
  uint8_t* key;
  uint8_t* string;
  uint8_t* string_ptr;
  size_t remaining_input;
} grpc_json_reader;

/* The return type of the parser. */
typedef enum {
  GRPC_JSON_DONE,          /* The parser finished successfully. */
  GRPC_JSON_EAGAIN,        /* The parser yields to get more data. */
  GRPC_JSON_READ_ERROR,    /* The parser passes through a read error. */
  GRPC_JSON_PARSE_ERROR,   /* The parser found an error in the json stream. */
  GRPC_JSON_INTERNAL_ERROR /* The parser got an internal error. */
} grpc_json_reader_status;

static void json_reader_string_clear(grpc_json_reader* reader) {
  if (reader->string) {
    GPR_ASSERT(reader->string_ptr < reader->input);
    *reader->string_ptr++ = 0;
  }
  reader->string = reader->string_ptr;
}

static void json_reader_string_add_char(grpc_json_reader* reader, uint32_t c) {
  GPR_ASSERT(reader->string_ptr < reader->input);
  GPR_ASSERT(c <= 0xff);
  *reader->string_ptr++ = static_cast<uint8_t>(c);
}

static void json_reader_string_add_utf32(grpc_json_reader* reader, uint32_t c) {
  if (c <= 0x7f) {
    json_reader_string_add_char(reader, c);
  } else if (c <= 0x7ff) {
    uint32_t b1 = 0xc0 | ((c >> 6) & 0x1f);
    uint32_t b2 = 0x80 | (c & 0x3f);
    json_reader_string_add_char(reader, b1);
    json_reader_string_add_char(reader, b2);
  } else if (c <= 0xffff) {
    uint32_t b1 = 0xe0 | ((c >> 12) & 0x0f);
    uint32_t b2 = 0x80 | ((c >> 6) & 0x3f);
    uint32_t b3 = 0x80 | (c & 0x3f);
    json_reader_string_add_char(reader, b1);
    json_reader_string_add_char(reader, b2);
    json_reader_string_add_char(reader, b3);
  } else if (c <= 0x1fffff) {
    uint32_t b1 = 0xf0 | ((c >> 18) & 0x07);
    uint32_t b2 = 0x80 | ((c >> 12) & 0x3f);
    uint32_t b3 = 0x80 | ((c >> 6) & 0x3f);
    uint32_t b4 = 0x80 | (c & 0x3f);
    json_reader_string_add_char(reader, b1);
    json_reader_string_add_char(reader, b2);
    json_reader_string_add_char(reader, b3);
    json_reader_string_add_char(reader, b4);
  }
}

static uint32_t grpc_json_reader_read_char(grpc_json_reader* reader) {
  if (reader->remaining_input == 0) return GRPC_JSON_READ_CHAR_EOF;
  uint32_t r = *reader->input++;
  reader->remaining_input--;
  if (r == 0) {
    reader->remaining_input = 0;
    return GRPC_JSON_READ_CHAR_EOF;
  }
  return r;
}

/* Helper function to create a new grpc_json object and link it into
 * our tree-in-progress inside our opaque structure.
 */
static grpc_json* json_create_and_link(grpc_json_reader* reader,
                                       grpc_json_type type) {
  grpc_json* json = grpc_json_create(type);
  json->parent = reader->current_container;
  json->prev = reader->current_value;
  reader->current_value = json;
  if (json->prev) {
    json->prev->next = json;
  }
  if (json->parent) {
    if (!json->parent->child) {
      json->parent->child = json;
    }
    if (json->parent->type == GRPC_JSON_OBJECT) {
      json->key = reinterpret_cast<char*>(reader->key);
    }
  }
  if (!reader->top) {
    reader->top = json;
  }
  return json;
}

static void json_reader_container_begins(grpc_json_reader* reader,
                                         grpc_json_type type) {
  GPR_ASSERT(type == GRPC_JSON_ARRAY || type == GRPC_JSON_OBJECT);
  grpc_json* container = json_create_and_link(reader, type);
  reader->current_container = container;
  reader->current_value = nullptr;
}

static grpc_json_type grpc_json_reader_container_ends(
    grpc_json_reader* reader) {
  grpc_json_type container_type = GRPC_JSON_TOP_LEVEL;
  GPR_ASSERT(reader->current_container);
  reader->current_value = reader->current_container;
  reader->current_container = reader->current_container->parent;
  if (reader->current_container) {
    container_type = reader->current_container->type;
  }
  return container_type;
}

static void json_reader_set_key(grpc_json_reader* reader) {
  reader->key = reader->string;
}

static void json_reader_set_string(grpc_json_reader* reader) {
  grpc_json* json = json_create_and_link(reader, GRPC_JSON_STRING);
  json->value = reinterpret_cast<char*>(reader->string);
}

static int json_reader_set_number(grpc_json_reader* reader) {
  grpc_json* json = json_create_and_link(reader, GRPC_JSON_NUMBER);
  json->value = reinterpret_cast<char*>(reader->string);
  return 1;
}

static void json_reader_set_true(grpc_json_reader* reader) {
  json_create_and_link(reader, GRPC_JSON_TRUE);
}

static void json_reader_set_false(grpc_json_reader* reader) {
  json_create_and_link(reader, GRPC_JSON_FALSE);
}

static void json_reader_set_null(grpc_json_reader* reader) {
  json_create_and_link(reader, GRPC_JSON_NULL);
}

static int json_reader_is_complete(grpc_json_reader* reader) {
  return ((reader->depth == 0) &&
          ((reader->state == GRPC_JSON_STATE_END) ||
           (reader->state == GRPC_JSON_STATE_VALUE_END)));
}

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
static grpc_json_reader_status grpc_json_reader_run(grpc_json_reader* reader) {
  uint32_t c, success;

  /* This state-machine is a strict implementation of ECMA-404 */
  for (;;) {
    c = grpc_json_reader_read_char(reader);
    switch (c) {
      /* Let's process the error cases first. */
      case GRPC_JSON_READ_CHAR_ERROR:
        return GRPC_JSON_READ_ERROR;

      case GRPC_JSON_READ_CHAR_EAGAIN:
        return GRPC_JSON_EAGAIN;

      case GRPC_JSON_READ_CHAR_EOF:
        if (json_reader_is_complete(reader)) {
          return GRPC_JSON_DONE;
        } else {
          return GRPC_JSON_PARSE_ERROR;
        }
        break;

      /* Processing whitespaces. */
      case ' ':
      case '\t':
      case '\n':
      case '\r':
        switch (reader->state) {
          case GRPC_JSON_STATE_OBJECT_KEY_BEGIN:
          case GRPC_JSON_STATE_OBJECT_KEY_END:
          case GRPC_JSON_STATE_VALUE_BEGIN:
          case GRPC_JSON_STATE_VALUE_END:
          case GRPC_JSON_STATE_END:
            break;

          case GRPC_JSON_STATE_OBJECT_KEY_STRING:
          case GRPC_JSON_STATE_VALUE_STRING:
            if (c != ' ') return GRPC_JSON_PARSE_ERROR;
            if (reader->unicode_high_surrogate != 0)
              return GRPC_JSON_PARSE_ERROR;
            json_reader_string_add_char(reader, c);
            break;

          case GRPC_JSON_STATE_VALUE_NUMBER:
          case GRPC_JSON_STATE_VALUE_NUMBER_WITH_DECIMAL:
          case GRPC_JSON_STATE_VALUE_NUMBER_ZERO:
          case GRPC_JSON_STATE_VALUE_NUMBER_EPM:
            success = static_cast<uint32_t>(json_reader_set_number(reader));
            if (!success) return GRPC_JSON_PARSE_ERROR;
            json_reader_string_clear(reader);
            reader->state = GRPC_JSON_STATE_VALUE_END;
            break;

          default:
            return GRPC_JSON_PARSE_ERROR;
        }
        break;

      /* Value, object or array terminations. */
      case ',':
      case '}':
      case ']':
        switch (reader->state) {
          case GRPC_JSON_STATE_OBJECT_KEY_STRING:
          case GRPC_JSON_STATE_VALUE_STRING:
            if (reader->unicode_high_surrogate != 0) {
              return GRPC_JSON_PARSE_ERROR;
            }
            json_reader_string_add_char(reader, c);
            break;

          case GRPC_JSON_STATE_VALUE_NUMBER:
          case GRPC_JSON_STATE_VALUE_NUMBER_WITH_DECIMAL:
          case GRPC_JSON_STATE_VALUE_NUMBER_ZERO:
          case GRPC_JSON_STATE_VALUE_NUMBER_EPM:
            if (reader->depth == 0) {
              return GRPC_JSON_PARSE_ERROR;
            } else if ((c == '}') && !reader->in_object) {
              return GRPC_JSON_PARSE_ERROR;
            } else if ((c == ']') && !reader->in_array) {
              return GRPC_JSON_PARSE_ERROR;
            }
            success = static_cast<uint32_t>(json_reader_set_number(reader));
            if (!success) return GRPC_JSON_PARSE_ERROR;
            json_reader_string_clear(reader);
            reader->state = GRPC_JSON_STATE_VALUE_END;
            /* The missing break here is intentional. */
            /* fallthrough */

          case GRPC_JSON_STATE_VALUE_END:
          case GRPC_JSON_STATE_OBJECT_KEY_BEGIN:
          case GRPC_JSON_STATE_VALUE_BEGIN:
            if (c == ',') {
              if (reader->state != GRPC_JSON_STATE_VALUE_END) {
                return GRPC_JSON_PARSE_ERROR;
              }
              if (reader->in_object) {
                reader->state = GRPC_JSON_STATE_OBJECT_KEY_BEGIN;
              } else if (reader->in_array) {
                reader->state = GRPC_JSON_STATE_VALUE_BEGIN;
              } else {
                return GRPC_JSON_PARSE_ERROR;
              }
            } else {
              if (reader->depth-- == 0) return GRPC_JSON_PARSE_ERROR;
              if ((c == '}') && !reader->in_object) {
                return GRPC_JSON_PARSE_ERROR;
              }
              if ((c == '}') &&
                  (reader->state == GRPC_JSON_STATE_OBJECT_KEY_BEGIN) &&
                  !reader->container_just_begun) {
                return GRPC_JSON_PARSE_ERROR;
              }
              if ((c == ']') && !reader->in_array) return GRPC_JSON_PARSE_ERROR;
              if ((c == ']') &&
                  (reader->state == GRPC_JSON_STATE_VALUE_BEGIN) &&
                  !reader->container_just_begun) {
                return GRPC_JSON_PARSE_ERROR;
              }
              reader->state = GRPC_JSON_STATE_VALUE_END;
              switch (grpc_json_reader_container_ends(reader)) {
                case GRPC_JSON_OBJECT:
                  reader->in_object = 1;
                  reader->in_array = 0;
                  break;
                case GRPC_JSON_ARRAY:
                  reader->in_object = 0;
                  reader->in_array = 1;
                  break;
                case GRPC_JSON_TOP_LEVEL:
                  GPR_ASSERT(reader->depth == 0);
                  reader->in_object = 0;
                  reader->in_array = 0;
                  reader->state = GRPC_JSON_STATE_END;
                  break;
                default:
                  GPR_UNREACHABLE_CODE(return GRPC_JSON_INTERNAL_ERROR);
              }
            }
            break;

          default:
            return GRPC_JSON_PARSE_ERROR;
        }
        break;

      /* In-string escaping. */
      case '\\':
        switch (reader->state) {
          case GRPC_JSON_STATE_OBJECT_KEY_STRING:
            reader->escaped_string_was_key = 1;
            reader->state = GRPC_JSON_STATE_STRING_ESCAPE;
            break;

          case GRPC_JSON_STATE_VALUE_STRING:
            reader->escaped_string_was_key = 0;
            reader->state = GRPC_JSON_STATE_STRING_ESCAPE;
            break;

          /* This is the \\ case. */
          case GRPC_JSON_STATE_STRING_ESCAPE:
            if (reader->unicode_high_surrogate != 0)
              return GRPC_JSON_PARSE_ERROR;
            json_reader_string_add_char(reader, '\\');
            if (reader->escaped_string_was_key) {
              reader->state = GRPC_JSON_STATE_OBJECT_KEY_STRING;
            } else {
              reader->state = GRPC_JSON_STATE_VALUE_STRING;
            }
            break;

          default:
            return GRPC_JSON_PARSE_ERROR;
        }
        break;

      default:
        reader->container_just_begun = 0;
        switch (reader->state) {
          case GRPC_JSON_STATE_OBJECT_KEY_BEGIN:
            if (c != '"') return GRPC_JSON_PARSE_ERROR;
            reader->state = GRPC_JSON_STATE_OBJECT_KEY_STRING;
            break;

          case GRPC_JSON_STATE_OBJECT_KEY_STRING:
            if (reader->unicode_high_surrogate != 0) {
              return GRPC_JSON_PARSE_ERROR;
            }
            if (c == '"') {
              reader->state = GRPC_JSON_STATE_OBJECT_KEY_END;
              json_reader_set_key(reader);
              json_reader_string_clear(reader);
            } else {
              if (c < 32) return GRPC_JSON_PARSE_ERROR;
              json_reader_string_add_char(reader, c);
            }
            break;

          case GRPC_JSON_STATE_VALUE_STRING:
            if (reader->unicode_high_surrogate != 0) {
              return GRPC_JSON_PARSE_ERROR;
            }
            if (c == '"') {
              reader->state = GRPC_JSON_STATE_VALUE_END;
              json_reader_set_string(reader);
              json_reader_string_clear(reader);
            } else {
              if (c < 32) return GRPC_JSON_PARSE_ERROR;
              json_reader_string_add_char(reader, c);
            }
            break;

          case GRPC_JSON_STATE_OBJECT_KEY_END:
            if (c != ':') return GRPC_JSON_PARSE_ERROR;
            reader->state = GRPC_JSON_STATE_VALUE_BEGIN;
            break;

          case GRPC_JSON_STATE_VALUE_BEGIN:
            switch (c) {
              case 't':
                reader->state = GRPC_JSON_STATE_VALUE_TRUE_R;
                break;

              case 'f':
                reader->state = GRPC_JSON_STATE_VALUE_FALSE_A;
                break;

              case 'n':
                reader->state = GRPC_JSON_STATE_VALUE_NULL_U;
                break;

              case '"':
                reader->state = GRPC_JSON_STATE_VALUE_STRING;
                break;

              case '0':
                json_reader_string_add_char(reader, c);
                reader->state = GRPC_JSON_STATE_VALUE_NUMBER_ZERO;
                break;

              case '1':
              case '2':
              case '3':
              case '4':
              case '5':
              case '6':
              case '7':
              case '8':
              case '9':
              case '-':
                json_reader_string_add_char(reader, c);
                reader->state = GRPC_JSON_STATE_VALUE_NUMBER;
                break;

              case '{':
                reader->container_just_begun = 1;
                json_reader_container_begins(reader, GRPC_JSON_OBJECT);
                reader->depth++;
                reader->state = GRPC_JSON_STATE_OBJECT_KEY_BEGIN;
                reader->in_object = 1;
                reader->in_array = 0;
                break;

              case '[':
                reader->container_just_begun = 1;
                json_reader_container_begins(reader, GRPC_JSON_ARRAY);
                reader->depth++;
                reader->in_object = 0;
                reader->in_array = 1;
                break;
              default:
                return GRPC_JSON_PARSE_ERROR;
            }
            break;

          case GRPC_JSON_STATE_STRING_ESCAPE:
            if (reader->escaped_string_was_key) {
              reader->state = GRPC_JSON_STATE_OBJECT_KEY_STRING;
            } else {
              reader->state = GRPC_JSON_STATE_VALUE_STRING;
            }
            if (reader->unicode_high_surrogate && c != 'u') {
              return GRPC_JSON_PARSE_ERROR;
            }
            switch (c) {
              case '"':
              case '/':
                json_reader_string_add_char(reader, c);
                break;
              case 'b':
                json_reader_string_add_char(reader, '\b');
                break;
              case 'f':
                json_reader_string_add_char(reader, '\f');
                break;
              case 'n':
                json_reader_string_add_char(reader, '\n');
                break;
              case 'r':
                json_reader_string_add_char(reader, '\r');
                break;
              case 't':
                json_reader_string_add_char(reader, '\t');
                break;
              case 'u':
                reader->state = GRPC_JSON_STATE_STRING_ESCAPE_U1;
                reader->unicode_char = 0;
                break;
              default:
                return GRPC_JSON_PARSE_ERROR;
            }
            break;

          case GRPC_JSON_STATE_STRING_ESCAPE_U1:
          case GRPC_JSON_STATE_STRING_ESCAPE_U2:
          case GRPC_JSON_STATE_STRING_ESCAPE_U3:
          case GRPC_JSON_STATE_STRING_ESCAPE_U4:
            if ((c >= '0') && (c <= '9')) {
              c -= '0';
            } else if ((c >= 'A') && (c <= 'F')) {
              c -= 'A' - 10;
            } else if ((c >= 'a') && (c <= 'f')) {
              c -= 'a' - 10;
            } else {
              return GRPC_JSON_PARSE_ERROR;
            }
            reader->unicode_char =
                static_cast<uint16_t>(reader->unicode_char << 4);
            reader->unicode_char =
                static_cast<uint16_t>(reader->unicode_char | c);

            switch (reader->state) {
              case GRPC_JSON_STATE_STRING_ESCAPE_U1:
                reader->state = GRPC_JSON_STATE_STRING_ESCAPE_U2;
                break;
              case GRPC_JSON_STATE_STRING_ESCAPE_U2:
                reader->state = GRPC_JSON_STATE_STRING_ESCAPE_U3;
                break;
              case GRPC_JSON_STATE_STRING_ESCAPE_U3:
                reader->state = GRPC_JSON_STATE_STRING_ESCAPE_U4;
                break;
              case GRPC_JSON_STATE_STRING_ESCAPE_U4:
                /* See grpc_json_writer_escape_string to have a description
                 * of what's going on here.
                 */
                if ((reader->unicode_char & 0xfc00) == 0xd800) {
                  /* high surrogate utf-16 */
                  if (reader->unicode_high_surrogate != 0)
                    return GRPC_JSON_PARSE_ERROR;
                  reader->unicode_high_surrogate = reader->unicode_char;
                } else if ((reader->unicode_char & 0xfc00) == 0xdc00) {
                  /* low surrogate utf-16 */
                  uint32_t utf32;
                  if (reader->unicode_high_surrogate == 0)
                    return GRPC_JSON_PARSE_ERROR;
                  utf32 = 0x10000;
                  utf32 += static_cast<uint32_t>(
                      (reader->unicode_high_surrogate - 0xd800) * 0x400);
                  utf32 += static_cast<uint32_t>(reader->unicode_char - 0xdc00);
                  json_reader_string_add_utf32(reader, utf32);
                  reader->unicode_high_surrogate = 0;
                } else {
                  /* anything else */
                  if (reader->unicode_high_surrogate != 0)
                    return GRPC_JSON_PARSE_ERROR;
                  json_reader_string_add_utf32(reader, reader->unicode_char);
                }
                if (reader->escaped_string_was_key) {
                  reader->state = GRPC_JSON_STATE_OBJECT_KEY_STRING;
                } else {
                  reader->state = GRPC_JSON_STATE_VALUE_STRING;
                }
                break;
              default:
                GPR_UNREACHABLE_CODE(return GRPC_JSON_INTERNAL_ERROR);
            }
            break;

          case GRPC_JSON_STATE_VALUE_NUMBER:
            json_reader_string_add_char(reader, c);
            switch (c) {
              case '0':
              case '1':
              case '2':
              case '3':
              case '4':
              case '5':
              case '6':
              case '7':
              case '8':
              case '9':
                break;
              case 'e':
              case 'E':
                reader->state = GRPC_JSON_STATE_VALUE_NUMBER_E;
                break;
              case '.':
                reader->state = GRPC_JSON_STATE_VALUE_NUMBER_DOT;
                break;
              default:
                return GRPC_JSON_PARSE_ERROR;
            }
            break;

          case GRPC_JSON_STATE_VALUE_NUMBER_WITH_DECIMAL:
            json_reader_string_add_char(reader, c);
            switch (c) {
              case '0':
              case '1':
              case '2':
              case '3':
              case '4':
              case '5':
              case '6':
              case '7':
              case '8':
              case '9':
                break;
              case 'e':
              case 'E':
                reader->state = GRPC_JSON_STATE_VALUE_NUMBER_E;
                break;
              default:
                return GRPC_JSON_PARSE_ERROR;
            }
            break;

          case GRPC_JSON_STATE_VALUE_NUMBER_ZERO:
            if (c != '.') return GRPC_JSON_PARSE_ERROR;
            json_reader_string_add_char(reader, c);
            reader->state = GRPC_JSON_STATE_VALUE_NUMBER_DOT;
            break;

          case GRPC_JSON_STATE_VALUE_NUMBER_DOT:
            json_reader_string_add_char(reader, c);
            switch (c) {
              case '0':
              case '1':
              case '2':
              case '3':
              case '4':
              case '5':
              case '6':
              case '7':
              case '8':
              case '9':
                reader->state = GRPC_JSON_STATE_VALUE_NUMBER_WITH_DECIMAL;
                break;
              default:
                return GRPC_JSON_PARSE_ERROR;
            }
            break;

          case GRPC_JSON_STATE_VALUE_NUMBER_E:
            json_reader_string_add_char(reader, c);
            switch (c) {
              case '0':
              case '1':
              case '2':
              case '3':
              case '4':
              case '5':
              case '6':
              case '7':
              case '8':
              case '9':
              case '+':
              case '-':
                reader->state = GRPC_JSON_STATE_VALUE_NUMBER_EPM;
                break;
              default:
                return GRPC_JSON_PARSE_ERROR;
            }
            break;

          case GRPC_JSON_STATE_VALUE_NUMBER_EPM:
            json_reader_string_add_char(reader, c);
            switch (c) {
              case '0':
              case '1':
              case '2':
              case '3':
              case '4':
              case '5':
              case '6':
              case '7':
              case '8':
              case '9':
                break;
              default:
                return GRPC_JSON_PARSE_ERROR;
            }
            break;

          case GRPC_JSON_STATE_VALUE_TRUE_R:
            if (c != 'r') return GRPC_JSON_PARSE_ERROR;
            reader->state = GRPC_JSON_STATE_VALUE_TRUE_U;
            break;

          case GRPC_JSON_STATE_VALUE_TRUE_U:
            if (c != 'u') return GRPC_JSON_PARSE_ERROR;
            reader->state = GRPC_JSON_STATE_VALUE_TRUE_E;
            break;

          case GRPC_JSON_STATE_VALUE_TRUE_E:
            if (c != 'e') return GRPC_JSON_PARSE_ERROR;
            json_reader_set_true(reader);
            reader->state = GRPC_JSON_STATE_VALUE_END;
            break;

          case GRPC_JSON_STATE_VALUE_FALSE_A:
            if (c != 'a') return GRPC_JSON_PARSE_ERROR;
            reader->state = GRPC_JSON_STATE_VALUE_FALSE_L;
            break;

          case GRPC_JSON_STATE_VALUE_FALSE_L:
            if (c != 'l') return GRPC_JSON_PARSE_ERROR;
            reader->state = GRPC_JSON_STATE_VALUE_FALSE_S;
            break;

          case GRPC_JSON_STATE_VALUE_FALSE_S:
            if (c != 's') return GRPC_JSON_PARSE_ERROR;
            reader->state = GRPC_JSON_STATE_VALUE_FALSE_E;
            break;

          case GRPC_JSON_STATE_VALUE_FALSE_E:
            if (c != 'e') return GRPC_JSON_PARSE_ERROR;
            json_reader_set_false(reader);
            reader->state = GRPC_JSON_STATE_VALUE_END;
            break;

          case GRPC_JSON_STATE_VALUE_NULL_U:
            if (c != 'u') return GRPC_JSON_PARSE_ERROR;
            reader->state = GRPC_JSON_STATE_VALUE_NULL_L1;
            break;

          case GRPC_JSON_STATE_VALUE_NULL_L1:
            if (c != 'l') return GRPC_JSON_PARSE_ERROR;
            reader->state = GRPC_JSON_STATE_VALUE_NULL_L2;
            break;

          case GRPC_JSON_STATE_VALUE_NULL_L2:
            if (c != 'l') return GRPC_JSON_PARSE_ERROR;
            json_reader_set_null(reader);
            reader->state = GRPC_JSON_STATE_VALUE_END;
            break;

          /* All of the VALUE_END cases are handled in the specialized case
           * above. */
          case GRPC_JSON_STATE_VALUE_END:
            switch (c) {
              case ',':
              case '}':
              case ']':
                GPR_UNREACHABLE_CODE(return GRPC_JSON_INTERNAL_ERROR);
                break;

              default:
                return GRPC_JSON_PARSE_ERROR;
            }
            break;

          case GRPC_JSON_STATE_END:
            return GRPC_JSON_PARSE_ERROR;
        }
    }
  }

  GPR_UNREACHABLE_CODE(return GRPC_JSON_INTERNAL_ERROR);
}

/* And finally, let's define our public API. */
grpc_json* grpc_json_parse_string_with_len(char* input, size_t size) {
  if (input == nullptr) return nullptr;
  // Initialize reader.
  grpc_json_reader reader;
  memset(&reader, 0, sizeof(reader));
  reader.string_ptr = reader.input = reinterpret_cast<uint8_t*>(input);
  reader.remaining_input = size;
  json_reader_string_clear(&reader);
  reader.state = GRPC_JSON_STATE_VALUE_BEGIN;
  // Perform read.
  grpc_json_reader_status status = grpc_json_reader_run(&reader);
  // Process results.
  grpc_json* json = reader.top;
  if ((status != GRPC_JSON_DONE) && json != nullptr) {
    grpc_json_destroy(json);
    json = nullptr;
  }
  return json;
}

#define UNBOUND_JSON_STRING_LENGTH 0x7fffffff

grpc_json* grpc_json_parse_string(char* input) {
  return grpc_json_parse_string_with_len(input, UNBOUND_JSON_STRING_LENGTH);
}

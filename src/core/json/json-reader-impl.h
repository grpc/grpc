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
 *    grpc_json_static_inline      // A macro to declare a static inline
 *                                 // function
 *    grpc_json_eof                // A macro that can be used in a switch
 *                                 // statement, that grpc_json_read_char
 *                                 // can return
 *    grpc_json_eagain             // A macro that can be used in a switch
 *                                 // statement, that grpc_json_read_char
 *                                 // can return
 *    grpc_json_error              // A macro that can be used in a switch
 *                                 // statement, that grpc_json_read_char
 *                                 // can return
 *
 *    // A macro or a function that clears your internal scratchpad.
 *    grpc_json_reader_string_clear(struct grpc_json_reader_t*);
 *    // A macro or a function that adds a character to your internal
 *    // scratchpad.
 *    grpc_json_reader_string_add_char(struct grpc_json_reader_t*, int);
 *    // A macro or a function that adds a unicode character to your internal
 *    // scratchpad.
 *    grpc_json_reader_string_add_wchar(struct grpc_json_reader_t*,
 *                                      grpc_json_wchar_t);
 *
 *    // A macro or a function that returns the next character from the input.
 *    // It can return:
 *    //  . an actual character into an int - unicode, wchar_t, whatever, as
 *    //    long as it's going to work in a switch statement, and can be tested
 *    //    against typical json tokens, such as '{', '[', ',', '}', ']', digits
 *    //    and whitespaces.
 *    //  . grpc_json_eof, which means the end of the input has been reached.
 *    //  . grpc_json_eagain, which means the parser needs to yield before
 *    //    getting more input.
 *    //  . grpc_json_error, which means the parser needs to exit with an error.
 *    int grpc_json_reader_read_char(struct grpc_json_reader_t*);
 *
 *    // A macro or a function that will be called to signal the beginning of a
 *    // container.
 *    // The argument "type" can be either GRPC_JSON_OBJECT, or GRPC_JSON_ARRAY.
 *    void grpc_json_reader_container_begins(struct grpc_json_reader_t*,
 *                                           enum *grpc_json_type_t type)
 *    // A macro or a function that will be called to signal the end of the
 *    // current container. It must return GRPC_JSON_OBJECT or GRPC_JSON_ARRAY
 *    // to signal what is the new current container, or GRPC_JSON_NONE if the
 *    // stack of containers is now empty.
 *    enum grpc_json_type_t
 *      grpc_json_reader_container_ends(struct grpc_json_reader_t*);
 *
 *    // A macro or a function that will be called to signal that json->string
 *    // contains the string of a object's key that is being added.
 *    void grpc_json_reader_object_set_key(struct grpc_json_reader_t*);
 *
 *    // A set of macro or functions that will be called to signal that the
 *    // current container is getting a new value. set_string and set_number
 *    // are reading their value from your internal scratchpad. set_number
 *    // must return a boolean to signal if the number parsing succeeded or
 *    // not. There is little reason for it not to.
 *    void grpc_json_reader_container_set_string(struct grpc_json_reader_t*);
 *    int grpc_json_reader_container_set_number(struct grpc_json_reader_t*);
 *    void grpc_json_reader_container_set_true(struct grpc_json_reader_t*);
 *    void grpc_json_reader_container_set_false(struct grpc_json_reader_t*);
 *    void grpc_json_reader_container_set_null(struct grpc_json_reader_t*);
 */

/* Call this function to initialize the reader structure. */
grpc_json_static_inline void grpc_json_reader_init(
    struct grpc_json_reader_t* reader) {
  reader->depth = 0;
  reader->in_object = 0;
  reader->in_array = 0;
  grpc_json_reader_string_clear(reader);
  reader->state = GRPC_JSON_STATE_VALUE_BEGIN;
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

grpc_json_static_inline grpc_json_reader_ret_t
grpc_json_reader_run(struct grpc_json_reader_t* reader) {
  int c, success;

  /* This state-machine is a strict implementation of http://json.org/ */
  for (;;) {
    c = grpc_json_reader_read_char(reader);
    switch (c) {
      /* Let's process the error cases first. */
      case grpc_json_error:
        return GRPC_JSON_READ_ERROR;

      case grpc_json_eagain:
        return GRPC_JSON_EAGAIN;

      case grpc_json_eof:
        if ((reader->depth == 0) &&
            ((reader->state == GRPC_JSON_STATE_END) ||
             (reader->state == GRPC_JSON_STATE_VALUE_END))) {
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
            grpc_json_reader_string_add_char(reader, c);
            break;

          case GRPC_JSON_STATE_VALUE_NUMBER:
          case GRPC_JSON_STATE_VALUE_NUMBER_WITH_DECIMAL:
          case GRPC_JSON_STATE_VALUE_NUMBER_ZERO:
          case GRPC_JSON_STATE_VALUE_NUMBER_EPM:
            success = grpc_json_reader_container_set_number(reader);
            if (!success) return GRPC_JSON_PARSE_ERROR;
            grpc_json_reader_string_clear(reader);
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
            grpc_json_reader_string_add_char(reader, c);
            break;

          case GRPC_JSON_STATE_VALUE_NUMBER:
          case GRPC_JSON_STATE_VALUE_NUMBER_WITH_DECIMAL:
          case GRPC_JSON_STATE_VALUE_NUMBER_ZERO:
          case GRPC_JSON_STATE_VALUE_NUMBER_EPM:
            success = grpc_json_reader_container_set_number(reader);
            if (!success) return GRPC_JSON_PARSE_ERROR;
            grpc_json_reader_string_clear(reader);
            reader->state = GRPC_JSON_STATE_VALUE_END;
          /* The missing break here is intentional. */

          case GRPC_JSON_STATE_VALUE_END:
          case GRPC_JSON_STATE_OBJECT_KEY_BEGIN:
          case GRPC_JSON_STATE_VALUE_BEGIN:
            if (c == ',') {
              if (reader->state != GRPC_JSON_STATE_VALUE_END) {
                return GRPC_JSON_PARSE_ERROR;
              }
              if (reader->in_object) {
                reader->state = GRPC_JSON_STATE_OBJECT_KEY_BEGIN;
              } else {
                reader->state = GRPC_JSON_STATE_VALUE_BEGIN;
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
                case GRPC_JSON_NONE:
                  if (reader->depth != 0) return GRPC_JSON_INTERNAL_ERROR;
                  reader->in_object = 0;
                  reader->in_array = 0;
                  reader->state = GRPC_JSON_STATE_END;
                  break;
                default:
                  return GRPC_JSON_INTERNAL_ERROR;
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
            grpc_json_reader_string_add_char(reader, '\\');
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
            if (c == '"') {
              reader->state = GRPC_JSON_STATE_OBJECT_KEY_END;
              grpc_json_reader_object_set_key(reader);
              grpc_json_reader_string_clear(reader);
            } else {
              grpc_json_reader_string_add_char(reader, c);
            }
            break;

          case GRPC_JSON_STATE_VALUE_STRING:
            if (c == '"') {
              reader->state = GRPC_JSON_STATE_VALUE_END;
              grpc_json_reader_container_set_string(reader);
              grpc_json_reader_string_clear(reader);
            } else {
              grpc_json_reader_string_add_char(reader, c);
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
                grpc_json_reader_string_add_char(reader, c);
                reader->state = GRPC_JSON_STATE_VALUE_NUMBER;
                break;

              case '{':
                reader->container_just_begun = 1;
                grpc_json_reader_container_begins(reader, GRPC_JSON_OBJECT);
                reader->depth++;
                reader->state = GRPC_JSON_STATE_OBJECT_KEY_BEGIN;
                reader->in_object = 1;
                reader->in_array = 0;
                break;

              case '[':
                reader->container_just_begun = 1;
                grpc_json_reader_container_begins(reader, GRPC_JSON_ARRAY);
                reader->depth++;
                reader->in_object = 0;
                reader->in_array = 1;
                break;
            }
            break;

          case GRPC_JSON_STATE_STRING_ESCAPE:
            if (reader->escaped_string_was_key) {
              reader->state = GRPC_JSON_STATE_OBJECT_KEY_STRING;
            } else {
              reader->state = GRPC_JSON_STATE_VALUE_STRING;
            }
            switch (c) {
              case '"':
              case '/':
                grpc_json_reader_string_add_char(reader, c);
                break;
              case 'b':
                grpc_json_reader_string_add_char(reader, '\b');
                break;
              case 'f':
                grpc_json_reader_string_add_char(reader, '\f');
                break;
              case 'n':
                grpc_json_reader_string_add_char(reader, '\n');
                break;
              case 'r':
                grpc_json_reader_string_add_char(reader, '\r');
                break;
              case 't':
                grpc_json_reader_string_add_char(reader, '\t');
                break;
              case 'u':
                reader->state = GRPC_JSON_STATE_STRING_ESCAPE_U1;
                reader->unicode = 0;
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
            reader->unicode <<= 4;
            reader->unicode |= c;

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
                grpc_json_reader_string_add_wchar(reader, reader->unicode);
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

          case GRPC_JSON_STATE_VALUE_NUMBER:
            grpc_json_reader_string_add_char(reader, c);
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
              case '.':
                reader->state = GRPC_JSON_STATE_VALUE_NUMBER_DOT;
                break;
              default:
                return GRPC_JSON_PARSE_ERROR;
            }
            break;

          case GRPC_JSON_STATE_VALUE_NUMBER_WITH_DECIMAL:
            grpc_json_reader_string_add_char(reader, c);
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
            grpc_json_reader_string_add_char(reader, c);
            reader->state = GRPC_JSON_STATE_VALUE_NUMBER_DOT;
            break;

          case GRPC_JSON_STATE_VALUE_NUMBER_DOT:
            grpc_json_reader_string_add_char(reader, c);
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
            grpc_json_reader_string_add_char(reader, c);
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

          case GRPC_JSON_STATE_VALUE_NUMBER_EPM:
            grpc_json_reader_string_add_char(reader, c);
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
            grpc_json_reader_container_set_true(reader);
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
            grpc_json_reader_container_set_false(reader);
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
            grpc_json_reader_container_set_null(reader);
            reader->state = GRPC_JSON_STATE_VALUE_END;
            break;

          /* All of the VALUE_END cases are handled in the specialized case
           * above. */
          case GRPC_JSON_STATE_VALUE_END:
            switch (c) {
              case ',':
              case '}':
              case ']':
                return GRPC_JSON_INTERNAL_ERROR;
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

  return GRPC_JSON_INTERNAL_ERROR;
}

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

namespace grpc_core {

class JsonReader {
 public:
  static grpc_json* Parse(char* input, size_t size);

 private:
  enum class Status {
    GRPC_JSON_DONE,          /* The parser finished successfully. */
    GRPC_JSON_PARSE_ERROR,   /* The parser found an error in the json stream. */
    GRPC_JSON_INTERNAL_ERROR /* The parser got an internal error. */
  };

  enum class State {
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

  /* The first non-unicode value is 0x110000. But let's pick
   * a value high enough to start our error codes from. These
   * values are safe to return from the read_char function.
   */
  static constexpr uint32_t GRPC_JSON_READ_CHAR_EOF = 0x7ffffff0;

  JsonReader(char* input, size_t size)
      : input_(reinterpret_cast<uint8_t*>(input)),
        remaining_input_(size),
        string_ptr_(input_) {
    StringClear();
  }

  void StringClear();
  void StringAddChar(uint32_t c);
  void StringAddUtf32(uint32_t c);
  uint32_t ReadChar();
  grpc_json* CreateAndLink(grpc_json_type type);
  void ContainerBegins(grpc_json_type type);
  grpc_json_type ContainerEnds();
  void SetKey();
  void SetString();
  bool SetNumber();
  void SetTrue();
  void SetFalse();
  void SetNull();
  bool IsComplete();
  Status Run();

  State state_ = State::GRPC_JSON_STATE_VALUE_BEGIN;

  int depth_ = 0;
  int in_object_ = 0;
  int in_array_ = 0;
  int escaped_string_was_key_ = 0;
  int container_just_begun_ = 0;
  uint16_t unicode_char_ = 0;
  uint16_t unicode_high_surrogate_ = 0;

  grpc_json* top_ = nullptr;
  grpc_json* current_container_ = nullptr;
  grpc_json* current_value_ = nullptr;
  uint8_t* input_;
  size_t remaining_input_;
  uint8_t* string_ptr_;
  uint8_t* key_ = nullptr;
  uint8_t* string_ = nullptr;
};

void JsonReader::StringClear() {
  if (string_ != nullptr) {
    GPR_ASSERT(string_ptr_ < input_);
    *string_ptr_++ = 0;
  }
  string_ = string_ptr_;
}

void JsonReader::StringAddChar(uint32_t c) {
  GPR_ASSERT(string_ptr_ < input_);
  GPR_ASSERT(c <= 0xff);
  *string_ptr_++ = static_cast<uint8_t>(c);
}

void JsonReader::StringAddUtf32(uint32_t c) {
  if (c <= 0x7f) {
    StringAddChar(c);
  } else if (c <= 0x7ff) {
    uint32_t b1 = 0xc0 | ((c >> 6) & 0x1f);
    uint32_t b2 = 0x80 | (c & 0x3f);
    StringAddChar(b1);
    StringAddChar(b2);
  } else if (c <= 0xffff) {
    uint32_t b1 = 0xe0 | ((c >> 12) & 0x0f);
    uint32_t b2 = 0x80 | ((c >> 6) & 0x3f);
    uint32_t b3 = 0x80 | (c & 0x3f);
    StringAddChar(b1);
    StringAddChar(b2);
    StringAddChar(b3);
  } else if (c <= 0x1fffff) {
    uint32_t b1 = 0xf0 | ((c >> 18) & 0x07);
    uint32_t b2 = 0x80 | ((c >> 12) & 0x3f);
    uint32_t b3 = 0x80 | ((c >> 6) & 0x3f);
    uint32_t b4 = 0x80 | (c & 0x3f);
    StringAddChar(b1);
    StringAddChar(b2);
    StringAddChar(b3);
    StringAddChar(b4);
  }
}

uint32_t JsonReader::ReadChar() {
  if (remaining_input_ == 0) return GRPC_JSON_READ_CHAR_EOF;
  uint32_t r = *input_++;
  remaining_input_--;
  if (r == 0) {
    remaining_input_ = 0;
    return GRPC_JSON_READ_CHAR_EOF;
  }
  return r;
}

/* Helper function to create a new grpc_json object and link it into
 * our tree-in-progress inside our opaque structure.
 */
grpc_json* JsonReader::CreateAndLink(grpc_json_type type) {
  grpc_json* json = grpc_json_create(type);
  json->parent = current_container_;
  json->prev = current_value_;
  current_value_ = json;
  if (json->prev) {
    json->prev->next = json;
  }
  if (json->parent) {
    if (!json->parent->child) {
      json->parent->child = json;
    }
    if (json->parent->type == GRPC_JSON_OBJECT) {
      json->key = reinterpret_cast<char*>(key_);
    }
  }
  if (top_ == nullptr) {
    top_ = json;
  }
  return json;
}

void JsonReader::ContainerBegins(grpc_json_type type) {
  GPR_ASSERT(type == GRPC_JSON_ARRAY || type == GRPC_JSON_OBJECT);
  grpc_json* container = CreateAndLink(type);
  current_container_ = container;
  current_value_ = nullptr;
}

grpc_json_type JsonReader::ContainerEnds() {
  grpc_json_type container_type = GRPC_JSON_TOP_LEVEL;
  GPR_ASSERT(current_container_);
  current_value_ = current_container_;
  current_container_ = current_container_->parent;
  if (current_container_ != nullptr) {
    container_type = current_container_->type;
  }
  return container_type;
}

void JsonReader::SetKey() { key_ = string_; }

void JsonReader::SetString() {
  grpc_json* json = CreateAndLink(GRPC_JSON_STRING);
  json->value = reinterpret_cast<char*>(string_);
}

bool JsonReader::SetNumber() {
  grpc_json* json = CreateAndLink(GRPC_JSON_NUMBER);
  json->value = reinterpret_cast<char*>(string_);
  return true;
}

void JsonReader::SetTrue() { CreateAndLink(GRPC_JSON_TRUE); }

void JsonReader::SetFalse() { CreateAndLink(GRPC_JSON_FALSE); }

void JsonReader::SetNull() { CreateAndLink(GRPC_JSON_NULL); }

bool JsonReader::IsComplete() {
  return (depth_ == 0 && (state_ == State::GRPC_JSON_STATE_END ||
                          state_ == State::GRPC_JSON_STATE_VALUE_END));
}

/* Call this function to start parsing the input. It will return the following:
 *    . GRPC_JSON_DONE if the input got eof, and the parsing finished
 *      successfully.
 *    . GRPC_JSON_PARSE_ERROR if the input was somehow invalid.
 *    . GRPC_JSON_INTERNAL_ERROR if the parser somehow ended into an invalid
 *      internal state.
 */
JsonReader::Status JsonReader::Run() {
  uint32_t c, success;

  /* This state-machine is a strict implementation of ECMA-404 */
  while (true) {
    c = ReadChar();
    switch (c) {
      /* Let's process the error case first. */
      case GRPC_JSON_READ_CHAR_EOF:
        if (IsComplete()) {
          return Status::GRPC_JSON_DONE;
        } else {
          return Status::GRPC_JSON_PARSE_ERROR;
        }
        break;

      /* Processing whitespaces. */
      case ' ':
      case '\t':
      case '\n':
      case '\r':
        switch (state_) {
          case State::GRPC_JSON_STATE_OBJECT_KEY_BEGIN:
          case State::GRPC_JSON_STATE_OBJECT_KEY_END:
          case State::GRPC_JSON_STATE_VALUE_BEGIN:
          case State::GRPC_JSON_STATE_VALUE_END:
          case State::GRPC_JSON_STATE_END:
            break;

          case State::GRPC_JSON_STATE_OBJECT_KEY_STRING:
          case State::GRPC_JSON_STATE_VALUE_STRING:
            if (c != ' ') return Status::GRPC_JSON_PARSE_ERROR;
            if (unicode_high_surrogate_ != 0)
              return Status::GRPC_JSON_PARSE_ERROR;
            StringAddChar(c);
            break;

          case State::GRPC_JSON_STATE_VALUE_NUMBER:
          case State::GRPC_JSON_STATE_VALUE_NUMBER_WITH_DECIMAL:
          case State::GRPC_JSON_STATE_VALUE_NUMBER_ZERO:
          case State::GRPC_JSON_STATE_VALUE_NUMBER_EPM:
            success = static_cast<uint32_t>(SetNumber());
            if (!success) return Status::GRPC_JSON_PARSE_ERROR;
            StringClear();
            state_ = State::GRPC_JSON_STATE_VALUE_END;
            break;

          default:
            return Status::GRPC_JSON_PARSE_ERROR;
        }
        break;

      /* Value, object or array terminations. */
      case ',':
      case '}':
      case ']':
        switch (state_) {
          case State::GRPC_JSON_STATE_OBJECT_KEY_STRING:
          case State::GRPC_JSON_STATE_VALUE_STRING:
            if (unicode_high_surrogate_ != 0) {
              return Status::GRPC_JSON_PARSE_ERROR;
            }
            StringAddChar(c);
            break;

          case State::GRPC_JSON_STATE_VALUE_NUMBER:
          case State::GRPC_JSON_STATE_VALUE_NUMBER_WITH_DECIMAL:
          case State::GRPC_JSON_STATE_VALUE_NUMBER_ZERO:
          case State::GRPC_JSON_STATE_VALUE_NUMBER_EPM:
            if (depth_ == 0) {
              return Status::GRPC_JSON_PARSE_ERROR;
            } else if ((c == '}') && !in_object_) {
              return Status::GRPC_JSON_PARSE_ERROR;
            } else if ((c == ']') && !in_array_) {
              return Status::GRPC_JSON_PARSE_ERROR;
            }
            success = static_cast<uint32_t>(SetNumber());
            if (!success) return Status::GRPC_JSON_PARSE_ERROR;
            StringClear();
            state_ = State::GRPC_JSON_STATE_VALUE_END;
            /* The missing break here is intentional. */
            /* fallthrough */

          case State::GRPC_JSON_STATE_VALUE_END:
          case State::GRPC_JSON_STATE_OBJECT_KEY_BEGIN:
          case State::GRPC_JSON_STATE_VALUE_BEGIN:
            if (c == ',') {
              if (state_ != State::GRPC_JSON_STATE_VALUE_END) {
                return Status::GRPC_JSON_PARSE_ERROR;
              }
              if (in_object_) {
                state_ = State::GRPC_JSON_STATE_OBJECT_KEY_BEGIN;
              } else if (in_array_) {
                state_ = State::GRPC_JSON_STATE_VALUE_BEGIN;
              } else {
                return Status::GRPC_JSON_PARSE_ERROR;
              }
            } else {
              if (depth_-- == 0) return Status::GRPC_JSON_PARSE_ERROR;
              if ((c == '}') && !in_object_) {
                return Status::GRPC_JSON_PARSE_ERROR;
              }
              if ((c == '}') &&
                  (state_ == State::GRPC_JSON_STATE_OBJECT_KEY_BEGIN) &&
                  !container_just_begun_) {
                return Status::GRPC_JSON_PARSE_ERROR;
              }
              if ((c == ']') && !in_array_)
                return Status::GRPC_JSON_PARSE_ERROR;
              if ((c == ']') &&
                  (state_ == State::GRPC_JSON_STATE_VALUE_BEGIN) &&
                  !container_just_begun_) {
                return Status::GRPC_JSON_PARSE_ERROR;
              }
              state_ = State::GRPC_JSON_STATE_VALUE_END;
              switch (ContainerEnds()) {
                case GRPC_JSON_OBJECT:
                  in_object_ = 1;
                  in_array_ = 0;
                  break;
                case GRPC_JSON_ARRAY:
                  in_object_ = 0;
                  in_array_ = 1;
                  break;
                case GRPC_JSON_TOP_LEVEL:
                  GPR_ASSERT(depth_ == 0);
                  in_object_ = 0;
                  in_array_ = 0;
                  state_ = State::GRPC_JSON_STATE_END;
                  break;
                default:
                  GPR_UNREACHABLE_CODE(return Status::GRPC_JSON_INTERNAL_ERROR);
              }
            }
            break;

          default:
            return Status::GRPC_JSON_PARSE_ERROR;
        }
        break;

      /* In-string escaping. */
      case '\\':
        switch (state_) {
          case State::GRPC_JSON_STATE_OBJECT_KEY_STRING:
            escaped_string_was_key_ = 1;
            state_ = State::GRPC_JSON_STATE_STRING_ESCAPE;
            break;

          case State::GRPC_JSON_STATE_VALUE_STRING:
            escaped_string_was_key_ = 0;
            state_ = State::GRPC_JSON_STATE_STRING_ESCAPE;
            break;

          /* This is the \\ case. */
          case State::GRPC_JSON_STATE_STRING_ESCAPE:
            if (unicode_high_surrogate_ != 0)
              return Status::GRPC_JSON_PARSE_ERROR;
            StringAddChar('\\');
            if (escaped_string_was_key_) {
              state_ = State::GRPC_JSON_STATE_OBJECT_KEY_STRING;
            } else {
              state_ = State::GRPC_JSON_STATE_VALUE_STRING;
            }
            break;

          default:
            return Status::GRPC_JSON_PARSE_ERROR;
        }
        break;

      default:
        container_just_begun_ = 0;
        switch (state_) {
          case State::GRPC_JSON_STATE_OBJECT_KEY_BEGIN:
            if (c != '"') return Status::GRPC_JSON_PARSE_ERROR;
            state_ = State::GRPC_JSON_STATE_OBJECT_KEY_STRING;
            break;

          case State::GRPC_JSON_STATE_OBJECT_KEY_STRING:
            if (unicode_high_surrogate_ != 0) {
              return Status::GRPC_JSON_PARSE_ERROR;
            }
            if (c == '"') {
              state_ = State::GRPC_JSON_STATE_OBJECT_KEY_END;
              SetKey();
              StringClear();
            } else {
              if (c < 32) return Status::GRPC_JSON_PARSE_ERROR;
              StringAddChar(c);
            }
            break;

          case State::GRPC_JSON_STATE_VALUE_STRING:
            if (unicode_high_surrogate_ != 0) {
              return Status::GRPC_JSON_PARSE_ERROR;
            }
            if (c == '"') {
              state_ = State::GRPC_JSON_STATE_VALUE_END;
              SetString();
              StringClear();
            } else {
              if (c < 32) return Status::GRPC_JSON_PARSE_ERROR;
              StringAddChar(c);
            }
            break;

          case State::GRPC_JSON_STATE_OBJECT_KEY_END:
            if (c != ':') return Status::GRPC_JSON_PARSE_ERROR;
            state_ = State::GRPC_JSON_STATE_VALUE_BEGIN;
            break;

          case State::GRPC_JSON_STATE_VALUE_BEGIN:
            switch (c) {
              case 't':
                state_ = State::GRPC_JSON_STATE_VALUE_TRUE_R;
                break;

              case 'f':
                state_ = State::GRPC_JSON_STATE_VALUE_FALSE_A;
                break;

              case 'n':
                state_ = State::GRPC_JSON_STATE_VALUE_NULL_U;
                break;

              case '"':
                state_ = State::GRPC_JSON_STATE_VALUE_STRING;
                break;

              case '0':
                StringAddChar(c);
                state_ = State::GRPC_JSON_STATE_VALUE_NUMBER_ZERO;
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
                StringAddChar(c);
                state_ = State::GRPC_JSON_STATE_VALUE_NUMBER;
                break;

              case '{':
                container_just_begun_ = 1;
                ContainerBegins(GRPC_JSON_OBJECT);
                depth_++;
                state_ = State::GRPC_JSON_STATE_OBJECT_KEY_BEGIN;
                in_object_ = 1;
                in_array_ = 0;
                break;

              case '[':
                container_just_begun_ = 1;
                ContainerBegins(GRPC_JSON_ARRAY);
                depth_++;
                in_object_ = 0;
                in_array_ = 1;
                break;
              default:
                return Status::GRPC_JSON_PARSE_ERROR;
            }
            break;

          case State::GRPC_JSON_STATE_STRING_ESCAPE:
            if (escaped_string_was_key_) {
              state_ = State::GRPC_JSON_STATE_OBJECT_KEY_STRING;
            } else {
              state_ = State::GRPC_JSON_STATE_VALUE_STRING;
            }
            if (unicode_high_surrogate_ && c != 'u') {
              return Status::GRPC_JSON_PARSE_ERROR;
            }
            switch (c) {
              case '"':
              case '/':
                StringAddChar(c);
                break;
              case 'b':
                StringAddChar('\b');
                break;
              case 'f':
                StringAddChar('\f');
                break;
              case 'n':
                StringAddChar('\n');
                break;
              case 'r':
                StringAddChar('\r');
                break;
              case 't':
                StringAddChar('\t');
                break;
              case 'u':
                state_ = State::GRPC_JSON_STATE_STRING_ESCAPE_U1;
                unicode_char_ = 0;
                break;
              default:
                return Status::GRPC_JSON_PARSE_ERROR;
            }
            break;

          case State::GRPC_JSON_STATE_STRING_ESCAPE_U1:
          case State::GRPC_JSON_STATE_STRING_ESCAPE_U2:
          case State::GRPC_JSON_STATE_STRING_ESCAPE_U3:
          case State::GRPC_JSON_STATE_STRING_ESCAPE_U4:
            if ((c >= '0') && (c <= '9')) {
              c -= '0';
            } else if ((c >= 'A') && (c <= 'F')) {
              c -= 'A' - 10;
            } else if ((c >= 'a') && (c <= 'f')) {
              c -= 'a' - 10;
            } else {
              return Status::GRPC_JSON_PARSE_ERROR;
            }
            unicode_char_ = static_cast<uint16_t>(unicode_char_ << 4);
            unicode_char_ = static_cast<uint16_t>(unicode_char_ | c);

            switch (state_) {
              case State::GRPC_JSON_STATE_STRING_ESCAPE_U1:
                state_ = State::GRPC_JSON_STATE_STRING_ESCAPE_U2;
                break;
              case State::GRPC_JSON_STATE_STRING_ESCAPE_U2:
                state_ = State::GRPC_JSON_STATE_STRING_ESCAPE_U3;
                break;
              case State::GRPC_JSON_STATE_STRING_ESCAPE_U3:
                state_ = State::GRPC_JSON_STATE_STRING_ESCAPE_U4;
                break;
              case State::GRPC_JSON_STATE_STRING_ESCAPE_U4:
                /* See grpc_json_writer_escape_string to have a description
                 * of what's going on here.
                 */
                if ((unicode_char_ & 0xfc00) == 0xd800) {
                  /* high surrogate utf-16 */
                  if (unicode_high_surrogate_ != 0)
                    return Status::GRPC_JSON_PARSE_ERROR;
                  unicode_high_surrogate_ = unicode_char_;
                } else if ((unicode_char_ & 0xfc00) == 0xdc00) {
                  /* low surrogate utf-16 */
                  uint32_t utf32;
                  if (unicode_high_surrogate_ == 0)
                    return Status::GRPC_JSON_PARSE_ERROR;
                  utf32 = 0x10000;
                  utf32 += static_cast<uint32_t>(
                      (unicode_high_surrogate_ - 0xd800) * 0x400);
                  utf32 += static_cast<uint32_t>(unicode_char_ - 0xdc00);
                  StringAddUtf32(utf32);
                  unicode_high_surrogate_ = 0;
                } else {
                  /* anything else */
                  if (unicode_high_surrogate_ != 0)
                    return Status::GRPC_JSON_PARSE_ERROR;
                  StringAddUtf32(unicode_char_);
                }
                if (escaped_string_was_key_) {
                  state_ = State::GRPC_JSON_STATE_OBJECT_KEY_STRING;
                } else {
                  state_ = State::GRPC_JSON_STATE_VALUE_STRING;
                }
                break;
              default:
                GPR_UNREACHABLE_CODE(return Status::GRPC_JSON_INTERNAL_ERROR);
            }
            break;

          case State::GRPC_JSON_STATE_VALUE_NUMBER:
            StringAddChar(c);
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
                state_ = State::GRPC_JSON_STATE_VALUE_NUMBER_E;
                break;
              case '.':
                state_ = State::GRPC_JSON_STATE_VALUE_NUMBER_DOT;
                break;
              default:
                return Status::GRPC_JSON_PARSE_ERROR;
            }
            break;

          case State::GRPC_JSON_STATE_VALUE_NUMBER_WITH_DECIMAL:
            StringAddChar(c);
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
                state_ = State::GRPC_JSON_STATE_VALUE_NUMBER_E;
                break;
              default:
                return Status::GRPC_JSON_PARSE_ERROR;
            }
            break;

          case State::GRPC_JSON_STATE_VALUE_NUMBER_ZERO:
            if (c != '.') return Status::GRPC_JSON_PARSE_ERROR;
            StringAddChar(c);
            state_ = State::GRPC_JSON_STATE_VALUE_NUMBER_DOT;
            break;

          case State::GRPC_JSON_STATE_VALUE_NUMBER_DOT:
            StringAddChar(c);
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
                state_ = State::GRPC_JSON_STATE_VALUE_NUMBER_WITH_DECIMAL;
                break;
              default:
                return Status::GRPC_JSON_PARSE_ERROR;
            }
            break;

          case State::GRPC_JSON_STATE_VALUE_NUMBER_E:
            StringAddChar(c);
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
                state_ = State::GRPC_JSON_STATE_VALUE_NUMBER_EPM;
                break;
              default:
                return Status::GRPC_JSON_PARSE_ERROR;
            }
            break;

          case State::GRPC_JSON_STATE_VALUE_NUMBER_EPM:
            StringAddChar(c);
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
                return Status::GRPC_JSON_PARSE_ERROR;
            }
            break;

          case State::GRPC_JSON_STATE_VALUE_TRUE_R:
            if (c != 'r') return Status::GRPC_JSON_PARSE_ERROR;
            state_ = State::GRPC_JSON_STATE_VALUE_TRUE_U;
            break;

          case State::GRPC_JSON_STATE_VALUE_TRUE_U:
            if (c != 'u') return Status::GRPC_JSON_PARSE_ERROR;
            state_ = State::GRPC_JSON_STATE_VALUE_TRUE_E;
            break;

          case State::GRPC_JSON_STATE_VALUE_TRUE_E:
            if (c != 'e') return Status::GRPC_JSON_PARSE_ERROR;
            SetTrue();
            state_ = State::GRPC_JSON_STATE_VALUE_END;
            break;

          case State::GRPC_JSON_STATE_VALUE_FALSE_A:
            if (c != 'a') return Status::GRPC_JSON_PARSE_ERROR;
            state_ = State::GRPC_JSON_STATE_VALUE_FALSE_L;
            break;

          case State::GRPC_JSON_STATE_VALUE_FALSE_L:
            if (c != 'l') return Status::GRPC_JSON_PARSE_ERROR;
            state_ = State::GRPC_JSON_STATE_VALUE_FALSE_S;
            break;

          case State::GRPC_JSON_STATE_VALUE_FALSE_S:
            if (c != 's') return Status::GRPC_JSON_PARSE_ERROR;
            state_ = State::GRPC_JSON_STATE_VALUE_FALSE_E;
            break;

          case State::GRPC_JSON_STATE_VALUE_FALSE_E:
            if (c != 'e') return Status::GRPC_JSON_PARSE_ERROR;
            SetFalse();
            state_ = State::GRPC_JSON_STATE_VALUE_END;
            break;

          case State::GRPC_JSON_STATE_VALUE_NULL_U:
            if (c != 'u') return Status::GRPC_JSON_PARSE_ERROR;
            state_ = State::GRPC_JSON_STATE_VALUE_NULL_L1;
            break;

          case State::GRPC_JSON_STATE_VALUE_NULL_L1:
            if (c != 'l') return Status::GRPC_JSON_PARSE_ERROR;
            state_ = State::GRPC_JSON_STATE_VALUE_NULL_L2;
            break;

          case State::GRPC_JSON_STATE_VALUE_NULL_L2:
            if (c != 'l') return Status::GRPC_JSON_PARSE_ERROR;
            SetNull();
            state_ = State::GRPC_JSON_STATE_VALUE_END;
            break;

          /* All of the VALUE_END cases are handled in the specialized case
           * above. */
          case State::GRPC_JSON_STATE_VALUE_END:
            switch (c) {
              case ',':
              case '}':
              case ']':
                GPR_UNREACHABLE_CODE(return Status::GRPC_JSON_INTERNAL_ERROR);
                break;

              default:
                return Status::GRPC_JSON_PARSE_ERROR;
            }
            break;

          case State::GRPC_JSON_STATE_END:
            return Status::GRPC_JSON_PARSE_ERROR;
        }
    }
  }

  GPR_UNREACHABLE_CODE(return Status::GRPC_JSON_INTERNAL_ERROR);
}

grpc_json* JsonReader::Parse(char* input, size_t size) {
  JsonReader reader(input, size);
  Status status = reader.Run();
  grpc_json* json = reader.top_;
  if ((status != Status::GRPC_JSON_DONE) && json != nullptr) {
    grpc_json_destroy(json);
    json = nullptr;
  }
  return json;
}

}  // namespace grpc_core

/* And finally, let's define our public API. */
grpc_json* grpc_json_parse_string_with_len(char* input, size_t size) {
  if (input == nullptr) return nullptr;
  return grpc_core::JsonReader::Parse(input, size);
}

#define UNBOUND_JSON_STRING_LENGTH 0x7fffffff

grpc_json* grpc_json_parse_string(char* input) {
  return grpc_json_parse_string_with_len(input, UNBOUND_JSON_STRING_LENGTH);
}

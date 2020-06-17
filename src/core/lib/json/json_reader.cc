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
#include <grpc/support/string_util.h>

#include "src/core/lib/json/json.h"

#define GRPC_JSON_MAX_DEPTH 255
#define GRPC_JSON_MAX_ERRORS 16

namespace grpc_core {

namespace {

class JsonReader {
 public:
  static grpc_error* Parse(absl::string_view input, Json* output);

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

  explicit JsonReader(absl::string_view input)
      : original_input_(reinterpret_cast<const uint8_t*>(input.data())),
        input_(original_input_),
        remaining_input_(input.size()) {}

  Status Run();
  uint32_t ReadChar();
  bool IsComplete();

  size_t CurrentIndex() const { return input_ - original_input_ - 1; }

  void StringAddChar(uint32_t c);
  void StringAddUtf32(uint32_t c);

  Json* CreateAndLinkValue();
  bool StartContainer(Json::Type type);
  void EndContainer();
  void SetKey();
  void SetString();
  bool SetNumber();
  void SetTrue();
  void SetFalse();
  void SetNull();

  const uint8_t* original_input_;
  const uint8_t* input_;
  size_t remaining_input_;

  State state_ = State::GRPC_JSON_STATE_VALUE_BEGIN;
  bool escaped_string_was_key_ = false;
  bool container_just_begun_ = false;
  uint16_t unicode_char_ = 0;
  uint16_t unicode_high_surrogate_ = 0;
  std::vector<grpc_error*> errors_;
  bool truncated_errors_ = false;

  Json root_value_;
  std::vector<Json*> stack_;

  std::string key_;
  std::string string_;
};

void JsonReader::StringAddChar(uint32_t c) {
  string_.push_back(static_cast<uint8_t>(c));
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
  const uint32_t r = *input_++;
  --remaining_input_;
  if (r == 0) {
    remaining_input_ = 0;
    return GRPC_JSON_READ_CHAR_EOF;
  }
  return r;
}

Json* JsonReader::CreateAndLinkValue() {
  Json* value;
  if (stack_.empty()) {
    value = &root_value_;
  } else {
    Json* parent = stack_.back();
    if (parent->type() == Json::Type::OBJECT) {
      if (parent->object_value().find(key_) != parent->object_value().end()) {
        if (errors_.size() == GRPC_JSON_MAX_ERRORS) {
          truncated_errors_ = true;
        } else {
          char* msg;
          gpr_asprintf(&msg, "duplicate key \"%s\" at index %" PRIuPTR,
                       key_.c_str(), CurrentIndex());
          errors_.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg));
          gpr_free(msg);
        }
      }
      value = &(*parent->mutable_object())[std::move(key_)];
    } else {
      GPR_ASSERT(parent->type() == Json::Type::ARRAY);
      parent->mutable_array()->emplace_back();
      value = &parent->mutable_array()->back();
    }
  }
  return value;
}

bool JsonReader::StartContainer(Json::Type type) {
  if (stack_.size() == GRPC_JSON_MAX_DEPTH) {
    if (errors_.size() == GRPC_JSON_MAX_ERRORS) {
      truncated_errors_ = true;
    } else {
      char* msg;
      gpr_asprintf(&msg, "exceeded max stack depth (%d) at index %" PRIuPTR,
                   GRPC_JSON_MAX_DEPTH, CurrentIndex());
      errors_.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg));
      gpr_free(msg);
    }
    return false;
  }
  Json* value = CreateAndLinkValue();
  if (type == Json::Type::OBJECT) {
    *value = Json::Object();
  } else {
    GPR_ASSERT(type == Json::Type::ARRAY);
    *value = Json::Array();
  }
  stack_.push_back(value);
  return true;
}

void JsonReader::EndContainer() {
  GPR_ASSERT(!stack_.empty());
  stack_.pop_back();
}

void JsonReader::SetKey() {
  key_ = std::move(string_);
  string_.clear();
}

void JsonReader::SetString() {
  Json* value = CreateAndLinkValue();
  *value = std::move(string_);
  string_.clear();
}

bool JsonReader::SetNumber() {
  Json* value = CreateAndLinkValue();
  *value = Json(string_, /*is_number=*/true);
  string_.clear();
  return true;
}

void JsonReader::SetTrue() {
  Json* value = CreateAndLinkValue();
  *value = true;
  string_.clear();
}

void JsonReader::SetFalse() {
  Json* value = CreateAndLinkValue();
  *value = false;
  string_.clear();
}

void JsonReader::SetNull() { CreateAndLinkValue(); }

bool JsonReader::IsComplete() {
  return (stack_.empty() && (state_ == State::GRPC_JSON_STATE_END ||
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
  uint32_t c;

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
            if (unicode_high_surrogate_ != 0) {
              return Status::GRPC_JSON_PARSE_ERROR;
            }
            StringAddChar(c);
            break;

          case State::GRPC_JSON_STATE_VALUE_NUMBER:
          case State::GRPC_JSON_STATE_VALUE_NUMBER_WITH_DECIMAL:
          case State::GRPC_JSON_STATE_VALUE_NUMBER_ZERO:
          case State::GRPC_JSON_STATE_VALUE_NUMBER_EPM:
            if (!SetNumber()) return Status::GRPC_JSON_PARSE_ERROR;
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
            if (stack_.empty()) {
              return Status::GRPC_JSON_PARSE_ERROR;
            } else if (c == '}' &&
                       stack_.back()->type() != Json::Type::OBJECT) {
              return Status::GRPC_JSON_PARSE_ERROR;
              return Status::GRPC_JSON_PARSE_ERROR;
            } else if (c == ']' && stack_.back()->type() != Json::Type::ARRAY) {
              return Status::GRPC_JSON_PARSE_ERROR;
            }
            if (!SetNumber()) return Status::GRPC_JSON_PARSE_ERROR;
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
              if (!stack_.empty() &&
                  stack_.back()->type() == Json::Type::OBJECT) {
                state_ = State::GRPC_JSON_STATE_OBJECT_KEY_BEGIN;
              } else if (!stack_.empty() &&
                         stack_.back()->type() == Json::Type::ARRAY) {
                state_ = State::GRPC_JSON_STATE_VALUE_BEGIN;
              } else {
                return Status::GRPC_JSON_PARSE_ERROR;
              }
            } else {
              if (stack_.empty()) {
                return Status::GRPC_JSON_PARSE_ERROR;
              }
              if (c == '}' && stack_.back()->type() != Json::Type::OBJECT) {
                return Status::GRPC_JSON_PARSE_ERROR;
              }
              if (c == '}' &&
                  state_ == State::GRPC_JSON_STATE_OBJECT_KEY_BEGIN &&
                  !container_just_begun_) {
                return Status::GRPC_JSON_PARSE_ERROR;
              }
              if (c == ']' && stack_.back()->type() != Json::Type::ARRAY) {
                return Status::GRPC_JSON_PARSE_ERROR;
              }
              if (c == ']' && state_ == State::GRPC_JSON_STATE_VALUE_BEGIN &&
                  !container_just_begun_) {
                return Status::GRPC_JSON_PARSE_ERROR;
              }
              state_ = State::GRPC_JSON_STATE_VALUE_END;
              EndContainer();
              if (stack_.empty()) {
                state_ = State::GRPC_JSON_STATE_END;
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
            escaped_string_was_key_ = true;
            state_ = State::GRPC_JSON_STATE_STRING_ESCAPE;
            break;

          case State::GRPC_JSON_STATE_VALUE_STRING:
            escaped_string_was_key_ = false;
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
        container_just_begun_ = false;
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
                container_just_begun_ = true;
                if (!StartContainer(Json::Type::OBJECT)) {
                  return Status::GRPC_JSON_PARSE_ERROR;
                }
                state_ = State::GRPC_JSON_STATE_OBJECT_KEY_BEGIN;
                break;

              case '[':
                container_just_begun_ = true;
                if (!StartContainer(Json::Type::ARRAY)) {
                  return Status::GRPC_JSON_PARSE_ERROR;
                }
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

grpc_error* JsonReader::Parse(absl::string_view input, Json* output) {
  JsonReader reader(input);
  Status status = reader.Run();
  if (reader.truncated_errors_) {
    reader.errors_.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "too many errors encountered during JSON parsing -- fix reported "
        "errors and try again to see additional errors"));
  }
  if (status == Status::GRPC_JSON_INTERNAL_ERROR) {
    char* msg;
    gpr_asprintf(&msg, "internal error in JSON parser at index %" PRIuPTR,
                 reader.CurrentIndex());
    reader.errors_.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg));
    gpr_free(msg);
  } else if (status == Status::GRPC_JSON_PARSE_ERROR) {
    char* msg;
    gpr_asprintf(&msg, "JSON parse error at index %" PRIuPTR,
                 reader.CurrentIndex());
    reader.errors_.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg));
    gpr_free(msg);
  }
  if (!reader.errors_.empty()) {
    return GRPC_ERROR_CREATE_FROM_VECTOR("JSON parsing failed",
                                         &reader.errors_);
  }
  *output = std::move(reader.root_value_);
  return GRPC_ERROR_NONE;
}

}  // namespace

Json Json::Parse(absl::string_view json_str, grpc_error** error) {
  Json value;
  *error = JsonReader::Parse(json_str, &value);
  return value;
}

}  // namespace grpc_core

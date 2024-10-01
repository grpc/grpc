//
// Copyright 2015-2016 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include <grpc/support/json.h>
#include <grpc/support/port_platform.h>
#include <inttypes.h>
#include <stdlib.h>

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/types/variant.h"
#include "src/core/util/json/json.h"
#include "src/core/util/match.h"

#define GRPC_JSON_MAX_DEPTH 255
#define GRPC_JSON_MAX_ERRORS 16

namespace grpc_core {

namespace {

class JsonReader {
 public:
  static absl::StatusOr<Json> Parse(absl::string_view input);

 private:
  enum class Status {
    GRPC_JSON_DONE,           // The parser finished successfully.
    GRPC_JSON_PARSE_ERROR,    // The parser found an error in the json stream.
    GRPC_JSON_INTERNAL_ERROR  // The parser got an internal error.
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

  // The first non-unicode value is 0x110000. But let's pick
  // a value high enough to start our error codes from. These
  // values are safe to return from the read_char function.
  //
  static constexpr uint32_t GRPC_JSON_READ_CHAR_EOF = 0x7ffffff0;

  struct Scope {
    std::string parent_object_key;
    absl::variant<Json::Object, Json::Array> data;

    Json::Type type() const {
      return Match(
          data, [](const Json::Object&) { return Json::Type::kObject; },
          [](const Json::Array&) { return Json::Type::kArray; });
    }

    Json TakeAsJson() {
      return MatchMutable(
          &data,
          [&](Json::Object* object) {
            return Json::FromObject(std::move(*object));
          },
          [&](Json::Array* array) {
            return Json::FromArray(std::move(*array));
          });
    }
  };

  explicit JsonReader(absl::string_view input)
      : original_input_(reinterpret_cast<const uint8_t*>(input.data())),
        input_(original_input_),
        remaining_input_(input.size()) {}

  Status Run();
  uint32_t ReadChar();
  bool IsComplete();

  size_t CurrentIndex() const { return input_ - original_input_ - 1; }

  GRPC_MUST_USE_RESULT bool StringAddChar(uint32_t c);
  GRPC_MUST_USE_RESULT bool StringAddUtf32(uint32_t c);

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
  std::vector<std::string> errors_;
  bool truncated_errors_ = false;
  uint8_t utf8_bytes_remaining_ = 0;
  uint8_t utf8_first_byte_ = 0;

  Json root_value_;
  std::vector<Scope> stack_;

  std::string key_;
  std::string string_;
};

bool JsonReader::StringAddChar(uint32_t c) {
  if (utf8_bytes_remaining_ == 0) {
    if ((c & 0x80) == 0) {
      utf8_bytes_remaining_ = 0;
    } else if ((c & 0xe0) == 0xc0 && c >= 0xc2) {
      /// For the UTF-8 characters with length of 2 bytes, the range of the
      /// first byte is [0xc2, 0xdf]. Reference: Table 3-7 in
      /// https://www.unicode.org/versions/Unicode14.0.0/ch03.pdf
      utf8_bytes_remaining_ = 1;
    } else if ((c & 0xf0) == 0xe0) {
      utf8_bytes_remaining_ = 2;
    } else if ((c & 0xf8) == 0xf0 && c <= 0xf4) {
      /// For the UTF-8 characters with length of 4 bytes, the range of the
      /// first byte is [0xf0, 0xf4]. Reference: Table 3-7 in
      /// https://www.unicode.org/versions/Unicode14.0.0/ch03.pdf
      utf8_bytes_remaining_ = 3;
    } else {
      return false;
    }
    utf8_first_byte_ = c;
  } else if (utf8_bytes_remaining_ == 1) {
    if ((c & 0xc0) != 0x80) {
      return false;
    }
    --utf8_bytes_remaining_;
  } else if (utf8_bytes_remaining_ == 2) {
    /// For UTF-8 characters starting with 0xe0, their length is 3 bytes, and
    /// the range of the second byte is [0xa0, 0xbf]. For UTF-8 characters
    /// starting with 0xed, their length is 3 bytes, and the range of the second
    /// byte is [0x80, 0x9f]. Reference: Table 3-7 in
    /// https://www.unicode.org/versions/Unicode14.0.0/ch03.pdf
    if (((c & 0xc0) != 0x80) || (utf8_first_byte_ == 0xe0 && c < 0xa0) ||
        (utf8_first_byte_ == 0xed && c > 0x9f)) {
      return false;
    }
    --utf8_bytes_remaining_;
  } else if (utf8_bytes_remaining_ == 3) {
    /// For UTF-8 characters starting with 0xf0, their length is 4 bytes, and
    /// the range of the second byte is [0x90, 0xbf]. For UTF-8 characters
    /// starting with 0xf4, their length is 4 bytes, and the range of the second
    /// byte is [0x80, 0x8f]. Reference: Table 3-7 in
    /// https://www.unicode.org/versions/Unicode14.0.0/ch03.pdf
    if (((c & 0xc0) != 0x80) || (utf8_first_byte_ == 0xf0 && c < 0x90) ||
        (utf8_first_byte_ == 0xf4 && c > 0x8f)) {
      return false;
    }
    --utf8_bytes_remaining_;
  } else {
    abort();
  }

  string_.push_back(static_cast<uint8_t>(c));
  return true;
}

bool JsonReader::StringAddUtf32(uint32_t c) {
  if (c <= 0x7f) {
    return StringAddChar(c);
  } else if (c <= 0x7ff) {
    uint32_t b1 = 0xc0 | ((c >> 6) & 0x1f);
    uint32_t b2 = 0x80 | (c & 0x3f);
    return StringAddChar(b1) && StringAddChar(b2);
  } else if (c <= 0xffff) {
    uint32_t b1 = 0xe0 | ((c >> 12) & 0x0f);
    uint32_t b2 = 0x80 | ((c >> 6) & 0x3f);
    uint32_t b3 = 0x80 | (c & 0x3f);
    return StringAddChar(b1) && StringAddChar(b2) && StringAddChar(b3);
  } else if (c <= 0x1fffff) {
    uint32_t b1 = 0xf0 | ((c >> 18) & 0x07);
    uint32_t b2 = 0x80 | ((c >> 12) & 0x3f);
    uint32_t b3 = 0x80 | ((c >> 6) & 0x3f);
    uint32_t b4 = 0x80 | (c & 0x3f);
    return StringAddChar(b1) && StringAddChar(b2) && StringAddChar(b3) &&
           StringAddChar(b4);
  } else {
    return false;
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
  if (stack_.empty()) return &root_value_;
  return MatchMutable(
      &stack_.back().data,
      [&](Json::Object* object) { return &(*object)[std::move(key_)]; },
      [&](Json::Array* array) {
        array->emplace_back();
        return &array->back();
      });
}

bool JsonReader::StartContainer(Json::Type type) {
  if (stack_.size() == GRPC_JSON_MAX_DEPTH) {
    if (errors_.size() == GRPC_JSON_MAX_ERRORS) {
      truncated_errors_ = true;
    } else {
      errors_.push_back(
          absl::StrFormat("exceeded max stack depth (%d) at index %" PRIuPTR,
                          GRPC_JSON_MAX_DEPTH, CurrentIndex()));
    }
    return false;
  }
  stack_.emplace_back();
  Scope& scope = stack_.back();
  scope.parent_object_key = std::move(key_);
  if (type == Json::Type::kObject) {
    scope.data = Json::Object();
  } else {
    CHECK(type == Json::Type::kArray);
    scope.data = Json::Array();
  }
  return true;
}

void JsonReader::EndContainer() {
  CHECK(!stack_.empty());
  Scope scope = std::move(stack_.back());
  stack_.pop_back();
  key_ = std::move(scope.parent_object_key);
  Json* value = CreateAndLinkValue();
  *value = scope.TakeAsJson();
}

void JsonReader::SetKey() {
  key_ = std::move(string_);
  string_.clear();
  const Json::Object& object = absl::get<Json::Object>(stack_.back().data);
  if (object.find(key_) != object.end()) {
    if (errors_.size() == GRPC_JSON_MAX_ERRORS) {
      truncated_errors_ = true;
    } else {
      errors_.push_back(
          absl::StrFormat("duplicate key \"%s\" at index %" PRIuPTR, key_,
                          CurrentIndex() - key_.size() - 2));
    }
  }
}

void JsonReader::SetString() {
  Json* value = CreateAndLinkValue();
  *value = Json::FromString(std::move(string_));
  string_.clear();
}

bool JsonReader::SetNumber() {
  Json* value = CreateAndLinkValue();
  *value = Json::FromNumber(std::move(string_));
  string_.clear();
  return true;
}

void JsonReader::SetTrue() {
  Json* value = CreateAndLinkValue();
  *value = Json::FromBool(true);
  string_.clear();
}

void JsonReader::SetFalse() {
  Json* value = CreateAndLinkValue();
  *value = Json::FromBool(false);
  string_.clear();
}

void JsonReader::SetNull() { CreateAndLinkValue(); }

bool JsonReader::IsComplete() {
  return (stack_.empty() && (state_ == State::GRPC_JSON_STATE_END ||
                             state_ == State::GRPC_JSON_STATE_VALUE_END));
}

// Call this function to start parsing the input. It will return the following:
//    . GRPC_JSON_DONE if the input got eof, and the parsing finished
//      successfully.
//    . GRPC_JSON_PARSE_ERROR if the input was somehow invalid.
//    . GRPC_JSON_INTERNAL_ERROR if the parser somehow ended into an invalid
//      internal state.
//
JsonReader::Status JsonReader::Run() {
  uint32_t c;

  // This state-machine is a strict implementation of ECMA-404
  while (true) {
    c = ReadChar();
    switch (c) {
      // Let's process the error case first.
      case GRPC_JSON_READ_CHAR_EOF:
        switch (state_) {
          case State::GRPC_JSON_STATE_VALUE_NUMBER:
          case State::GRPC_JSON_STATE_VALUE_NUMBER_WITH_DECIMAL:
          case State::GRPC_JSON_STATE_VALUE_NUMBER_ZERO:
          case State::GRPC_JSON_STATE_VALUE_NUMBER_EPM:
            if (!SetNumber()) return Status::GRPC_JSON_PARSE_ERROR;
            state_ = State::GRPC_JSON_STATE_VALUE_END;
            break;

          default:
            break;
        }
        if (IsComplete()) {
          return Status::GRPC_JSON_DONE;
        }
        return Status::GRPC_JSON_PARSE_ERROR;

      // Processing whitespaces.
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
            if (!StringAddChar(c)) return Status::GRPC_JSON_PARSE_ERROR;
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

      // Value, object or array terminations.
      case ',':
      case '}':
      case ']':
        switch (state_) {
          case State::GRPC_JSON_STATE_OBJECT_KEY_STRING:
          case State::GRPC_JSON_STATE_VALUE_STRING:
            if (unicode_high_surrogate_ != 0) {
              return Status::GRPC_JSON_PARSE_ERROR;
            }
            if (!StringAddChar(c)) return Status::GRPC_JSON_PARSE_ERROR;
            break;

          case State::GRPC_JSON_STATE_VALUE_NUMBER:
          case State::GRPC_JSON_STATE_VALUE_NUMBER_WITH_DECIMAL:
          case State::GRPC_JSON_STATE_VALUE_NUMBER_ZERO:
          case State::GRPC_JSON_STATE_VALUE_NUMBER_EPM:
            if (stack_.empty()) {
              return Status::GRPC_JSON_PARSE_ERROR;
            } else if (c == '}' &&
                       stack_.back().type() != Json::Type::kObject) {
              return Status::GRPC_JSON_PARSE_ERROR;
            } else if (c == ']' && stack_.back().type() != Json::Type::kArray) {
              return Status::GRPC_JSON_PARSE_ERROR;
            }
            if (!SetNumber()) return Status::GRPC_JSON_PARSE_ERROR;
            state_ = State::GRPC_JSON_STATE_VALUE_END;
            ABSL_FALLTHROUGH_INTENDED;

          case State::GRPC_JSON_STATE_VALUE_END:
          case State::GRPC_JSON_STATE_OBJECT_KEY_BEGIN:
          case State::GRPC_JSON_STATE_VALUE_BEGIN:
            if (c == ',') {
              if (state_ != State::GRPC_JSON_STATE_VALUE_END) {
                return Status::GRPC_JSON_PARSE_ERROR;
              }
              if (!stack_.empty() &&
                  stack_.back().type() == Json::Type::kObject) {
                state_ = State::GRPC_JSON_STATE_OBJECT_KEY_BEGIN;
              } else if (!stack_.empty() &&
                         stack_.back().type() == Json::Type::kArray) {
                state_ = State::GRPC_JSON_STATE_VALUE_BEGIN;
              } else {
                return Status::GRPC_JSON_PARSE_ERROR;
              }
            } else {
              if (stack_.empty()) {
                return Status::GRPC_JSON_PARSE_ERROR;
              }
              if (c == '}' && stack_.back().type() != Json::Type::kObject) {
                return Status::GRPC_JSON_PARSE_ERROR;
              }
              if (c == '}' &&
                  state_ == State::GRPC_JSON_STATE_OBJECT_KEY_BEGIN &&
                  !container_just_begun_) {
                return Status::GRPC_JSON_PARSE_ERROR;
              }
              if (c == ']' && stack_.back().type() != Json::Type::kArray) {
                return Status::GRPC_JSON_PARSE_ERROR;
              }
              if (c == ']' && state_ == State::GRPC_JSON_STATE_VALUE_BEGIN &&
                  !container_just_begun_) {
                return Status::GRPC_JSON_PARSE_ERROR;
              }
              state_ = State::GRPC_JSON_STATE_VALUE_END;
              container_just_begun_ = false;
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

      // In-string escaping.
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

          // This is the \\ case.
          case State::GRPC_JSON_STATE_STRING_ESCAPE:
            if (unicode_high_surrogate_ != 0) {
              return Status::GRPC_JSON_PARSE_ERROR;
            }
            if (!StringAddChar('\\')) return Status::GRPC_JSON_PARSE_ERROR;
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
              // Once the key is parsed, there should no un-matched utf8
              // encoded bytes.
              if (utf8_bytes_remaining_ != 0) {
                return Status::GRPC_JSON_PARSE_ERROR;
              }
              SetKey();
            } else {
              if (c < 32) return Status::GRPC_JSON_PARSE_ERROR;
              if (!StringAddChar(c)) return Status::GRPC_JSON_PARSE_ERROR;
            }
            break;

          case State::GRPC_JSON_STATE_VALUE_STRING:
            if (unicode_high_surrogate_ != 0) {
              return Status::GRPC_JSON_PARSE_ERROR;
            }
            if (c == '"') {
              state_ = State::GRPC_JSON_STATE_VALUE_END;
              // Once the value is parsed, there should no un-matched utf8
              // encoded bytes.
              if (utf8_bytes_remaining_ != 0) {
                return Status::GRPC_JSON_PARSE_ERROR;
              }
              SetString();
            } else {
              if (c < 32) return Status::GRPC_JSON_PARSE_ERROR;
              if (!StringAddChar(c)) return Status::GRPC_JSON_PARSE_ERROR;
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
                if (!StringAddChar(c)) return Status::GRPC_JSON_PARSE_ERROR;
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
                if (!StringAddChar(c)) return Status::GRPC_JSON_PARSE_ERROR;
                state_ = State::GRPC_JSON_STATE_VALUE_NUMBER;
                break;

              case '{':
                container_just_begun_ = true;
                if (!StartContainer(Json::Type::kObject)) {
                  return Status::GRPC_JSON_PARSE_ERROR;
                }
                state_ = State::GRPC_JSON_STATE_OBJECT_KEY_BEGIN;
                break;

              case '[':
                container_just_begun_ = true;
                if (!StartContainer(Json::Type::kArray)) {
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
                if (!StringAddChar(c)) return Status::GRPC_JSON_PARSE_ERROR;
                break;
              case 'b':
                if (!StringAddChar('\b')) return Status::GRPC_JSON_PARSE_ERROR;
                break;
              case 'f':
                if (!StringAddChar('\f')) return Status::GRPC_JSON_PARSE_ERROR;
                break;
              case 'n':
                if (!StringAddChar('\n')) return Status::GRPC_JSON_PARSE_ERROR;
                break;
              case 'r':
                if (!StringAddChar('\r')) return Status::GRPC_JSON_PARSE_ERROR;
                break;
              case 't':
                if (!StringAddChar('\t')) return Status::GRPC_JSON_PARSE_ERROR;
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
                // See grpc_json_writer_escape_string to have a description
                // of what's going on here.
                //
                if ((unicode_char_ & 0xfc00) == 0xd800) {
                  // high surrogate utf-16
                  if (unicode_high_surrogate_ != 0) {
                    return Status::GRPC_JSON_PARSE_ERROR;
                  }
                  unicode_high_surrogate_ = unicode_char_;
                } else if ((unicode_char_ & 0xfc00) == 0xdc00) {
                  // low surrogate utf-16
                  uint32_t utf32;
                  if (unicode_high_surrogate_ == 0) {
                    return Status::GRPC_JSON_PARSE_ERROR;
                  }
                  utf32 = 0x10000;
                  utf32 += static_cast<uint32_t>(
                      (unicode_high_surrogate_ - 0xd800) * 0x400);
                  utf32 += static_cast<uint32_t>(unicode_char_ - 0xdc00);
                  if (!StringAddUtf32(utf32)) {
                    return Status::GRPC_JSON_PARSE_ERROR;
                  }
                  unicode_high_surrogate_ = 0;
                } else {
                  // anything else
                  if (unicode_high_surrogate_ != 0) {
                    return Status::GRPC_JSON_PARSE_ERROR;
                  }
                  if (!StringAddUtf32(unicode_char_)) {
                    return Status::GRPC_JSON_PARSE_ERROR;
                  }
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
            if (!StringAddChar(c)) return Status::GRPC_JSON_PARSE_ERROR;
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
            if (!StringAddChar(c)) return Status::GRPC_JSON_PARSE_ERROR;
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
            if (!StringAddChar(c)) return Status::GRPC_JSON_PARSE_ERROR;
            state_ = State::GRPC_JSON_STATE_VALUE_NUMBER_DOT;
            break;

          case State::GRPC_JSON_STATE_VALUE_NUMBER_DOT:
            if (!StringAddChar(c)) return Status::GRPC_JSON_PARSE_ERROR;
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
            if (!StringAddChar(c)) return Status::GRPC_JSON_PARSE_ERROR;
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
            if (!StringAddChar(c)) return Status::GRPC_JSON_PARSE_ERROR;
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

          // All of the VALUE_END cases are handled in the specialized case
          // above.
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

absl::StatusOr<Json> JsonReader::Parse(absl::string_view input) {
  JsonReader reader(input);
  Status status = reader.Run();
  if (reader.truncated_errors_) {
    reader.errors_.push_back(
        "too many errors encountered during JSON parsing -- fix reported "
        "errors and try again to see additional errors");
  }
  if (status == Status::GRPC_JSON_INTERNAL_ERROR) {
    reader.errors_.push_back(absl::StrCat(
        "internal error in JSON parser at index ", reader.CurrentIndex()));
  } else if (status == Status::GRPC_JSON_PARSE_ERROR) {
    reader.errors_.push_back(
        absl::StrCat("JSON parse error at index ", reader.CurrentIndex()));
  }
  if (!reader.errors_.empty()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "JSON parsing failed: [", absl::StrJoin(reader.errors_, "; "), "]"));
  }
  return std::move(reader.root_value_);
}

}  // namespace

absl::StatusOr<Json> JsonParse(absl::string_view json_str) {
  return JsonReader::Parse(json_str);
}

}  // namespace grpc_core

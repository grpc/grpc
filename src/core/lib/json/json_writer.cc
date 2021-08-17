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

#include <grpc/support/port_platform.h>

#include <stdlib.h>
#include <string.h>

#include "absl/strings/string_view.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/json/json.h"

namespace grpc_core {

namespace {

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
class JsonWriter {
 public:
  static std::string Dump(const Json& value, int indent);

 private:
  explicit JsonWriter(int indent) : indent_(indent) {}

  void OutputCheck(size_t needed);
  void OutputChar(char c);
  void OutputString(const absl::string_view str);
  void OutputIndent();
  void ValueEnd();
  void EscapeUtf16(uint16_t utf16);
  void EscapeString(const std::string& string);
  void ContainerBegins(Json::Type type);
  void ContainerEnds(Json::Type type);
  void ObjectKey(const std::string& string);
  void ValueRaw(const std::string& string);
  void ValueString(const std::string& string);

  void DumpObject(const Json::Object& object);
  void DumpArray(const Json::Array& array);
  void DumpValue(const Json& value);

  int indent_;
  int depth_ = 0;
  bool container_empty_ = true;
  bool got_key_ = false;
  std::string output_;
};

/* This function checks if there's enough space left in the output buffer,
 * and will enlarge it if necessary. We're only allocating chunks of 256
 * bytes at a time (or multiples thereof).
 */
void JsonWriter::OutputCheck(size_t needed) {
  size_t free_space = output_.capacity() - output_.size();
  if (free_space >= needed) return;
  needed -= free_space;
  /* Round up by 256 bytes. */
  needed = (needed + 0xff) & ~0xffU;
  output_.reserve(output_.capacity() + needed);
}

void JsonWriter::OutputChar(char c) {
  OutputCheck(1);
  output_.push_back(c);
}

void JsonWriter::OutputString(const absl::string_view str) {
  OutputCheck(str.size());
  output_.append(str.data(), str.size());
}

void JsonWriter::OutputIndent() {
  static const char spacesstr[] =
      "                "
      "                "
      "                "
      "                ";
  unsigned spaces = static_cast<unsigned>(depth_ * indent_);
  if (indent_ == 0) return;
  if (got_key_) {
    OutputChar(' ');
    return;
  }
  while (spaces >= (sizeof(spacesstr) - 1)) {
    OutputString(absl::string_view(spacesstr, sizeof(spacesstr) - 1));
    spaces -= static_cast<unsigned>(sizeof(spacesstr) - 1);
  }
  if (spaces == 0) return;
  OutputString(
      absl::string_view(spacesstr + sizeof(spacesstr) - 1 - spaces, spaces));
}

void JsonWriter::ValueEnd() {
  if (container_empty_) {
    container_empty_ = false;
    if (indent_ == 0 || depth_ == 0) return;
    OutputChar('\n');
  } else {
    OutputChar(',');
    if (indent_ == 0) return;
    OutputChar('\n');
  }
}

void JsonWriter::EscapeUtf16(uint16_t utf16) {
  static const char hex[] = "0123456789abcdef";
  OutputString(absl::string_view("\\u", 2));
  OutputChar(hex[(utf16 >> 12) & 0x0f]);
  OutputChar(hex[(utf16 >> 8) & 0x0f]);
  OutputChar(hex[(utf16 >> 4) & 0x0f]);
  OutputChar(hex[(utf16)&0x0f]);
}

void JsonWriter::EscapeString(const std::string& string) {
  OutputChar('"');
  for (size_t idx = 0; idx < string.size(); ++idx) {
    uint8_t c = static_cast<uint8_t>(string[idx]);
    if (c == 0) {
      break;
    } else if (c >= 32 && c <= 126) {
      if (c == '\\' || c == '"') OutputChar('\\');
      OutputChar(static_cast<char>(c));
    } else if (c < 32 || c == 127) {
      switch (c) {
        case '\b':
          OutputString(absl::string_view("\\b", 2));
          break;
        case '\f':
          OutputString(absl::string_view("\\f", 2));
          break;
        case '\n':
          OutputString(absl::string_view("\\n", 2));
          break;
        case '\r':
          OutputString(absl::string_view("\\r", 2));
          break;
        case '\t':
          OutputString(absl::string_view("\\t", 2));
          break;
        default:
          EscapeUtf16(c);
          break;
      }
    } else {
      uint32_t utf32 = 0;
      int extra = 0;
      int i;
      int valid = 1;
      if ((c & 0xe0) == 0xc0) {
        utf32 = c & 0x1f;
        extra = 1;
      } else if ((c & 0xf0) == 0xe0) {
        utf32 = c & 0x0f;
        extra = 2;
      } else if ((c & 0xf8) == 0xf0) {
        utf32 = c & 0x07;
        extra = 3;
      } else {
        break;
      }
      for (i = 0; i < extra; i++) {
        utf32 <<= 6;
        ++idx;
        /* Breaks out and bail if we hit the end of the string. */
        if (idx == string.size()) {
          valid = 0;
          break;
        }
        c = static_cast<uint8_t>(string[idx]);
        /* Breaks out and bail on any invalid UTF-8 sequence, including \0. */
        if ((c & 0xc0) != 0x80) {
          valid = 0;
          break;
        }
        utf32 |= c & 0x3f;
      }
      if (!valid) break;
      /* The range 0xd800 - 0xdfff is reserved by the surrogates ad vitam.
       * Any other range is technically reserved for future usage, so if we
       * don't want the software to break in the future, we have to allow
       * anything else. The first non-unicode character is 0x110000. */
      if (((utf32 >= 0xd800) && (utf32 <= 0xdfff)) || (utf32 >= 0x110000)) {
        break;
      }
      if (utf32 >= 0x10000) {
        /* If utf32 contains a character that is above 0xffff, it needs to be
         * broken down into a utf-16 surrogate pair. A surrogate pair is first
         * a high surrogate, followed by a low surrogate. Each surrogate holds
         * 10 bits of usable data, thus allowing a total of 20 bits of data.
         * The high surrogate marker is 0xd800, while the low surrogate marker
         * is 0xdc00. The low 10 bits of each will be the usable data.
         *
         * After re-combining the 20 bits of data, one has to add 0x10000 to
         * the resulting value, in order to obtain the original character.
         * This is obviously because the range 0x0000 - 0xffff can be written
         * without any special trick.
         *
         * Since 0x10ffff is the highest allowed character, we're working in
         * the range 0x00000 - 0xfffff after we decrement it by 0x10000.
         * That range is exactly 20 bits.
         */
        utf32 -= 0x10000;
        EscapeUtf16(static_cast<uint16_t>(0xd800 | (utf32 >> 10)));
        EscapeUtf16(static_cast<uint16_t>(0xdc00 | (utf32 & 0x3ff)));
      } else {
        EscapeUtf16(static_cast<uint16_t>(utf32));
      }
    }
  }
  OutputChar('"');
}

void JsonWriter::ContainerBegins(Json::Type type) {
  if (!got_key_) ValueEnd();
  OutputIndent();
  OutputChar(type == Json::Type::OBJECT ? '{' : '[');
  container_empty_ = true;
  got_key_ = false;
  depth_++;
}

void JsonWriter::ContainerEnds(Json::Type type) {
  if (indent_ && !container_empty_) OutputChar('\n');
  depth_--;
  if (!container_empty_) OutputIndent();
  OutputChar(type == Json::Type::OBJECT ? '}' : ']');
  container_empty_ = false;
  got_key_ = false;
}

void JsonWriter::ObjectKey(const std::string& string) {
  ValueEnd();
  OutputIndent();
  EscapeString(string);
  OutputChar(':');
  got_key_ = true;
}

void JsonWriter::ValueRaw(const std::string& string) {
  if (!got_key_) ValueEnd();
  OutputIndent();
  OutputString(string);
  got_key_ = false;
}

void JsonWriter::ValueString(const std::string& string) {
  if (!got_key_) ValueEnd();
  OutputIndent();
  EscapeString(string);
  got_key_ = false;
}

void JsonWriter::DumpObject(const Json::Object& object) {
  ContainerBegins(Json::Type::OBJECT);
  for (const auto& p : object) {
    ObjectKey(p.first.data());
    DumpValue(p.second);
  }
  ContainerEnds(Json::Type::OBJECT);
}

void JsonWriter::DumpArray(const Json::Array& array) {
  ContainerBegins(Json::Type::ARRAY);
  for (const auto& v : array) {
    DumpValue(v);
  }
  ContainerEnds(Json::Type::ARRAY);
}

void JsonWriter::DumpValue(const Json& value) {
  switch (value.type()) {
    case Json::Type::OBJECT:
      DumpObject(value.object_value());
      break;
    case Json::Type::ARRAY:
      DumpArray(value.array_value());
      break;
    case Json::Type::STRING:
      ValueString(value.string_value());
      break;
    case Json::Type::NUMBER:
      ValueRaw(value.string_value());
      break;
    case Json::Type::JSON_TRUE:
      ValueRaw(std::string("true", 4));
      break;
    case Json::Type::JSON_FALSE:
      ValueRaw(std::string("false", 5));
      break;
    case Json::Type::JSON_NULL:
      ValueRaw(std::string("null", 4));
      break;
    default:
      GPR_UNREACHABLE_CODE(abort());
  }
}

std::string JsonWriter::Dump(const Json& value, int indent) {
  JsonWriter writer(indent);
  writer.DumpValue(value);
  return std::move(writer.output_);
}

}  // namespace

std::string Json::Dump(int indent) const {
  return JsonWriter::Dump(*this, indent);
}

}  // namespace grpc_core

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

#include <string.h>

#include "src/core/lib/json/json_writer.h"

static void json_writer_output_char(grpc_json_writer* writer, char c) {
  writer->vtable->output_char(writer->userdata, c);
}

static void json_writer_output_string(grpc_json_writer* writer,
                                      const char* str) {
  writer->vtable->output_string(writer->userdata, str);
}

static void json_writer_output_string_with_len(grpc_json_writer* writer,
                                               const char* str, size_t len) {
  writer->vtable->output_string_with_len(writer->userdata, str, len);
}

void grpc_json_writer_init(grpc_json_writer* writer, int indent,
                           grpc_json_writer_vtable* vtable, void* userdata) {
  memset(writer, 0, sizeof(*writer));
  writer->container_empty = 1;
  writer->indent = indent;
  writer->vtable = vtable;
  writer->userdata = userdata;
}

static void json_writer_output_indent(grpc_json_writer* writer) {
  static const char spacesstr[] =
      "                "
      "                "
      "                "
      "                ";

  unsigned spaces = static_cast<unsigned>(writer->depth * writer->indent);

  if (writer->indent == 0) return;

  if (writer->got_key) {
    json_writer_output_char(writer, ' ');
    return;
  }

  while (spaces >= (sizeof(spacesstr) - 1)) {
    json_writer_output_string_with_len(writer, spacesstr,
                                       sizeof(spacesstr) - 1);
    spaces -= static_cast<unsigned>(sizeof(spacesstr) - 1);
  }

  if (spaces == 0) return;

  json_writer_output_string_with_len(
      writer, spacesstr + sizeof(spacesstr) - 1 - spaces, spaces);
}

static void json_writer_value_end(grpc_json_writer* writer) {
  if (writer->container_empty) {
    writer->container_empty = 0;
    if ((writer->indent == 0) || (writer->depth == 0)) return;
    json_writer_output_char(writer, '\n');
  } else {
    json_writer_output_char(writer, ',');
    if (writer->indent == 0) return;
    json_writer_output_char(writer, '\n');
  }
}

static void json_writer_escape_utf16(grpc_json_writer* writer, uint16_t utf16) {
  static const char hex[] = "0123456789abcdef";

  json_writer_output_string_with_len(writer, "\\u", 2);
  json_writer_output_char(writer, hex[(utf16 >> 12) & 0x0f]);
  json_writer_output_char(writer, hex[(utf16 >> 8) & 0x0f]);
  json_writer_output_char(writer, hex[(utf16 >> 4) & 0x0f]);
  json_writer_output_char(writer, hex[(utf16)&0x0f]);
}

static void json_writer_escape_string(grpc_json_writer* writer,
                                      const char* string) {
  json_writer_output_char(writer, '"');

  for (;;) {
    uint8_t c = static_cast<uint8_t>(*string++);
    if (c == 0) {
      break;
    } else if ((c >= 32) && (c <= 126)) {
      if ((c == '\\') || (c == '"')) json_writer_output_char(writer, '\\');
      json_writer_output_char(writer, static_cast<char>(c));
    } else if ((c < 32) || (c == 127)) {
      switch (c) {
        case '\b':
          json_writer_output_string_with_len(writer, "\\b", 2);
          break;
        case '\f':
          json_writer_output_string_with_len(writer, "\\f", 2);
          break;
        case '\n':
          json_writer_output_string_with_len(writer, "\\n", 2);
          break;
        case '\r':
          json_writer_output_string_with_len(writer, "\\r", 2);
          break;
        case '\t':
          json_writer_output_string_with_len(writer, "\\t", 2);
          break;
        default:
          json_writer_escape_utf16(writer, c);
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
        c = static_cast<uint8_t>(*string++);
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
      if (((utf32 >= 0xd800) && (utf32 <= 0xdfff)) || (utf32 >= 0x110000))
        break;
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
        json_writer_escape_utf16(writer,
                                 static_cast<uint16_t>(0xd800 | (utf32 >> 10)));
        json_writer_escape_utf16(
            writer, static_cast<uint16_t>(0xdc00 | (utf32 & 0x3ff)));
      } else {
        json_writer_escape_utf16(writer, static_cast<uint16_t>(utf32));
      }
    }
  }

  json_writer_output_char(writer, '"');
}

void grpc_json_writer_container_begins(grpc_json_writer* writer,
                                       grpc_json_type type) {
  if (!writer->got_key) json_writer_value_end(writer);
  json_writer_output_indent(writer);
  json_writer_output_char(writer, type == GRPC_JSON_OBJECT ? '{' : '[');
  writer->container_empty = 1;
  writer->got_key = 0;
  writer->depth++;
}

void grpc_json_writer_container_ends(grpc_json_writer* writer,
                                     grpc_json_type type) {
  if (writer->indent && !writer->container_empty)
    json_writer_output_char(writer, '\n');
  writer->depth--;
  if (!writer->container_empty) json_writer_output_indent(writer);
  json_writer_output_char(writer, type == GRPC_JSON_OBJECT ? '}' : ']');
  writer->container_empty = 0;
  writer->got_key = 0;
}

void grpc_json_writer_object_key(grpc_json_writer* writer, const char* string) {
  json_writer_value_end(writer);
  json_writer_output_indent(writer);
  json_writer_escape_string(writer, string);
  json_writer_output_char(writer, ':');
  writer->got_key = 1;
}

void grpc_json_writer_value_raw(grpc_json_writer* writer, const char* string) {
  if (!writer->got_key) json_writer_value_end(writer);
  json_writer_output_indent(writer);
  json_writer_output_string(writer, string);
  writer->got_key = 0;
}

void grpc_json_writer_value_raw_with_len(grpc_json_writer* writer,
                                         const char* string, size_t len) {
  if (!writer->got_key) json_writer_value_end(writer);
  json_writer_output_indent(writer);
  json_writer_output_string_with_len(writer, string, len);
  writer->got_key = 0;
}

void grpc_json_writer_value_string(grpc_json_writer* writer,
                                   const char* string) {
  if (!writer->got_key) json_writer_value_end(writer);
  json_writer_output_indent(writer);
  json_writer_escape_string(writer, string);
  writer->got_key = 0;
}

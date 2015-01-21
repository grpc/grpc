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


/* The idea of the writer is basically symmetrical of the reader. While the
 * reader emits various calls to your code, the writer takes basically the
 * same calls and emit json out of it. It doesn't try to make any check on
 * the order of the calls you do on it.
 *
 * Also, unlike the reader, the writer expects UTF-8 encoded input strings.
 *
 * The following need to be defined:
 *
 * // Adds a character to the output stream.
 * void grpc_json_writer_output_char(struct grpc_json_writer_t *, char);
 * // Adds a zero-terminated string to the output stream.
 * void grpc_json_writer_output_string(
 *   struct grpc_json_writer_t *writer, const char *str);
 * // Adds a fixed-length string to the output stream.
 * void grpc_json_writer_output_string_with_len(
 *   struct grpc_json_writer_t *writer, const char *str, size_t len);

 */

/* Call this function to initialize the writer structure. */
grpc_json_static_inline void grpc_json_writer_init(
    struct grpc_json_writer_t* writer, int indent) {
  writer->depth = 0;
  writer->container_empty = 1;
  writer->got_key = 0;
  writer->indent = indent;
}

/* This function is fully private. */
grpc_json_static_inline void grpc_json_writer_output_indent(
    struct grpc_json_writer_t* writer) {
  static const char spacesstr[] =
      "                "
      "                "
      "                "
      "                ";

  int spaces = writer->depth * writer->indent;

  if (writer->got_key) {
    grpc_json_writer_output_char(writer, ' ');
    return;
  }

  while (spaces >= (sizeof(spacesstr) - 1)) {
    grpc_json_writer_output_string_with_len(writer, spacesstr,
                                            sizeof(spacesstr) - 1);
    spaces -= (sizeof(spacesstr) - 1);
  }

  if (!spaces) return;

  grpc_json_writer_output_string_with_len(
      writer, spacesstr + sizeof(spacesstr) - 1 - spaces, spaces);
}

/* This function is fully private. */
grpc_json_static_inline void grpc_json_writer_value_end(
    struct grpc_json_writer_t* writer) {
  if (writer->container_empty) {
    writer->container_empty = 0;
    if (!writer->indent || !writer->depth) return;
    grpc_json_writer_output_char(writer, '\n');
  } else {
    grpc_json_writer_output_char(writer, ',');
    if (!writer->indent) return;
    grpc_json_writer_output_char(writer, '\n');
  }
}

/* This function is fully private. */
grpc_json_static_inline void grpc_json_writer_escape_string(
    struct grpc_json_writer_t* writer, const char* string) {
  static const char hex[] = "0123456789abcdef";
  grpc_json_writer_output_char(writer, '"');

  for (;;) {
    unsigned char c = (unsigned char)*string++;
    if (!c) {
      break;
    } else if ((c >= 32) && (c <= 127)) {
      if ((c == '\\') || (c == '"')) grpc_json_writer_output_char(writer, '\\');
      grpc_json_writer_output_char(writer, c);
    } else if (c < 32) {
      grpc_json_writer_output_char(writer, '\\');
      switch (c) {
        case '\b':
          grpc_json_writer_output_char(writer, 'b');
          break;
        case '\f':
          grpc_json_writer_output_char(writer, 'f');
          break;
        case '\n':
          grpc_json_writer_output_char(writer, 'n');
          break;
        case '\r':
          grpc_json_writer_output_char(writer, 'r');
          break;
        case '\t':
          grpc_json_writer_output_char(writer, 't');
          break;
        default:
          grpc_json_writer_output_string_with_len(writer, "u00", 3);
          grpc_json_writer_output_char(writer, c >= 16 ? '1' : '0');
          grpc_json_writer_output_char(writer, hex[c & 15]);
          break;
      }
    } else {
      unsigned unicode = 0;
      if ((c & 0xe0) == 0xc0) {
        unicode = c & 0x1f;
        unicode <<= 6;
        c = *string++;
        if ((c & 0xc0) != 0x80) break;
        unicode |= c & 0x3f;
      } else if ((c & 0xf0) == 0xe0) {
        unicode = c & 0x0f;
        unicode <<= 6;
        c = *string++;
        if ((c & 0xc0) != 0x80) break;
        unicode |= c & 0x3f;
        unicode <<= 6;
        c = *string++;
        if ((c & 0xc0) != 0x80) break;
        unicode |= c & 0x3f;
      } else {
        break;
      }
      grpc_json_writer_output_string_with_len(writer, "\\u", 2);
      grpc_json_writer_output_char(writer, hex[(unicode >> 12) & 0x0f]);
      grpc_json_writer_output_char(writer, hex[(unicode >> 8) & 0x0f]);
      grpc_json_writer_output_char(writer, hex[(unicode >> 4) & 0x0f]);
      grpc_json_writer_output_char(writer, hex[(unicode) & 0x0f]);
    }
  }

  grpc_json_writer_output_char(writer, '"');
}

/* Call that function to start a new json container. */
grpc_json_static_inline void grpc_json_writer_container_begins(
    struct grpc_json_writer_t* writer, enum grpc_json_type_t type) {
  if (!writer->got_key) grpc_json_writer_value_end(writer);
  grpc_json_writer_output_indent(writer);
  grpc_json_writer_output_char(writer, type == GRPC_JSON_OBJECT ? '{' : '[');
  writer->container_empty = 1;
  writer->got_key = 0;
  writer->depth++;
}

/* Call that function to end the current json container. */
grpc_json_static_inline void grpc_json_writer_container_ends(
    struct grpc_json_writer_t* writer, enum grpc_json_type_t type) {
  if (writer->indent && !writer->container_empty)
    grpc_json_writer_output_char(writer, '\n');
  writer->depth--;
  if (!writer->container_empty) grpc_json_writer_output_indent(writer);
  grpc_json_writer_output_char(writer, type == GRPC_JSON_OBJECT ? '}' : ']');
  writer->container_empty = 0;
  writer->got_key = 0;
}

/* If you are in a GRPC_JSON_OBJECT container, call this to set up a key. */
grpc_json_static_inline void grpc_json_writer_object_key(
    struct grpc_json_writer_t* writer, const char* string) {
  grpc_json_writer_value_end(writer);
  grpc_json_writer_output_indent(writer);
  grpc_json_writer_escape_string(writer, string);
  grpc_json_writer_output_char(writer, ':');
  writer->got_key = 1;
}

/* Sets a raw value - use it for numbers. */
grpc_json_static_inline void grpc_json_writer_value_raw(
    struct grpc_json_writer_t* writer, const char* string) {
  if (!writer->got_key) grpc_json_writer_value_end(writer);
  grpc_json_writer_output_indent(writer);
  grpc_json_writer_output_string(writer, string);
  writer->got_key = 0;
}

/* Sets a raw value with a known length - use it for true, false and null. */
grpc_json_static_inline void grpc_json_writer_value_raw_with_len(
    struct grpc_json_writer_t* writer, const char* string, unsigned len) {
  if (!writer->got_key) grpc_json_writer_value_end(writer);
  grpc_json_writer_output_indent(writer);
  grpc_json_writer_output_string_with_len(writer, string, len);
  writer->got_key = 0;
}

/* Outputs a string value. This will add double quotes, and escape it. */
grpc_json_static_inline void grpc_json_writer_value_string(
    struct grpc_json_writer_t* writer, const char* string) {
  if (!writer->got_key) grpc_json_writer_value_end(writer);
  grpc_json_writer_output_indent(writer);
  grpc_json_writer_escape_string(writer, string);
  writer->got_key = 0;
}

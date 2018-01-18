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

#include "src/core/ext/filters/client_channel/uri_parser.h"

#include <string.h>

#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/gpr/string.h"
#include "src/core/lib/slice/percent_encoding.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"

/** a size_t default value... maps to all 1's */
#define NOT_SET (~(size_t)0)

static grpc_uri* bad_uri(const char* uri_text, size_t pos, const char* section,
                         bool suppress_errors) {
  char* line_prefix;
  size_t pfx_len;

  if (!suppress_errors) {
    gpr_asprintf(&line_prefix, "bad uri.%s: '", section);
    pfx_len = strlen(line_prefix) + pos;
    gpr_log(GPR_ERROR, "%s%s'", line_prefix, uri_text);
    gpr_free(line_prefix);

    line_prefix = (char*)gpr_malloc(pfx_len + 1);
    memset(line_prefix, ' ', pfx_len);
    line_prefix[pfx_len] = 0;
    gpr_log(GPR_ERROR, "%s^ here", line_prefix);
    gpr_free(line_prefix);
  }

  return nullptr;
}

/** Returns a copy of percent decoded \a src[begin, end) */
static char* decode_and_copy_component(const char* src, size_t begin,
                                       size_t end) {
  grpc_slice component =
      (begin == NOT_SET || end == NOT_SET)
          ? grpc_empty_slice()
          : grpc_slice_from_copied_buffer(src + begin, end - begin);
  grpc_slice decoded_component =
      grpc_permissive_percent_decode_slice(component);
  char* out = grpc_dump_slice(decoded_component, GPR_DUMP_ASCII);
  grpc_slice_unref_internal(component);
  grpc_slice_unref_internal(decoded_component);
  return out;
}

static bool valid_hex(char c) {
  return ((c >= 'a') && (c <= 'f')) || ((c >= 'A') && (c <= 'F')) ||
         ((c >= '0') && (c <= '9'));
}

/** Returns how many chars to advance if \a uri_text[i] begins a valid \a pchar
 * production. If \a uri_text[i] introduces an invalid \a pchar (such as percent
 * sign not followed by two hex digits), NOT_SET is returned. */
static size_t parse_pchar(const char* uri_text, size_t i) {
  /* pchar = unreserved / pct-encoded / sub-delims / ":" / "@"
   * unreserved = ALPHA / DIGIT / "-" / "." / "_" / "~"
   * pct-encoded = "%" HEXDIG HEXDIG
   * sub-delims = "!" / "$" / "&" / "'" / "(" / ")"
   / "*" / "+" / "," / ";" / "=" */
  char c = uri_text[i];
  switch (c) {
    default:
      if (((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z')) ||
          ((c >= '0') && (c <= '9'))) {
        return 1;
      }
      break;
    case ':':
    case '@':
    case '-':
    case '.':
    case '_':
    case '~':
    case '!':
    case '$':
    case '&':
    case '\'':
    case '(':
    case ')':
    case '*':
    case '+':
    case ',':
    case ';':
    case '=':
      return 1;
    case '%': /* pct-encoded */
      if (valid_hex(uri_text[i + 1]) && valid_hex(uri_text[i + 2])) {
        return 2;
      }
      return NOT_SET;
  }
  return 0;
}

/* *( pchar / "?" / "/" ) */
static int parse_fragment_or_query(const char* uri_text, size_t* i) {
  char c;
  while ((c = uri_text[*i]) != 0) {
    const size_t advance = parse_pchar(uri_text, *i); /* pchar */
    switch (advance) {
      case 0: /* uri_text[i] isn't in pchar */
        /* maybe it's ? or / */
        if (uri_text[*i] == '?' || uri_text[*i] == '/') {
          (*i)++;
          break;
        } else {
          return 1;
        }
        GPR_UNREACHABLE_CODE(return 0);
      default:
        (*i) += advance;
        break;
      case NOT_SET: /* uri_text[i] introduces an invalid URI */
        return 0;
    }
  }
  /* *i is the first uri_text position past the \a query production, maybe \0 */
  return 1;
}

static void parse_query_parts(grpc_uri* uri) {
  static const char* QUERY_PARTS_SEPARATOR = "&";
  static const char* QUERY_PARTS_VALUE_SEPARATOR = "=";
  GPR_ASSERT(uri->query != nullptr);
  if (uri->query[0] == '\0') {
    uri->query_parts = nullptr;
    uri->query_parts_values = nullptr;
    uri->num_query_parts = 0;
    return;
  }

  gpr_string_split(uri->query, QUERY_PARTS_SEPARATOR, &uri->query_parts,
                   &uri->num_query_parts);
  uri->query_parts_values =
      (char**)gpr_malloc(uri->num_query_parts * sizeof(char**));
  for (size_t i = 0; i < uri->num_query_parts; i++) {
    char** query_param_parts;
    size_t num_query_param_parts;
    char* full = uri->query_parts[i];
    gpr_string_split(full, QUERY_PARTS_VALUE_SEPARATOR, &query_param_parts,
                     &num_query_param_parts);
    GPR_ASSERT(num_query_param_parts > 0);
    uri->query_parts[i] = query_param_parts[0];
    if (num_query_param_parts > 1) {
      /* TODO(dgq): only the first value after the separator is considered.
       * Perhaps all chars after the first separator for the query part should
       * be included, even if they include the separator. */
      uri->query_parts_values[i] = query_param_parts[1];
    } else {
      uri->query_parts_values[i] = nullptr;
    }
    for (size_t j = 2; j < num_query_param_parts; j++) {
      gpr_free(query_param_parts[j]);
    }
    gpr_free(query_param_parts);
    gpr_free(full);
  }
}

grpc_uri* grpc_uri_parse(const char* uri_text, bool suppress_errors) {
  grpc_uri* uri;
  size_t scheme_begin = 0;
  size_t scheme_end = NOT_SET;
  size_t authority_begin = NOT_SET;
  size_t authority_end = NOT_SET;
  size_t path_begin = NOT_SET;
  size_t path_end = NOT_SET;
  size_t query_begin = NOT_SET;
  size_t query_end = NOT_SET;
  size_t fragment_begin = NOT_SET;
  size_t fragment_end = NOT_SET;
  size_t i;

  for (i = scheme_begin; uri_text[i] != 0; i++) {
    if (uri_text[i] == ':') {
      scheme_end = i;
      break;
    }
    if (uri_text[i] >= 'a' && uri_text[i] <= 'z') continue;
    if (uri_text[i] >= 'A' && uri_text[i] <= 'Z') continue;
    if (i != scheme_begin) {
      if (uri_text[i] >= '0' && uri_text[i] <= '9') continue;
      if (uri_text[i] == '+') continue;
      if (uri_text[i] == '-') continue;
      if (uri_text[i] == '.') continue;
    }
    break;
  }
  if (scheme_end == NOT_SET) {
    return bad_uri(uri_text, i, "scheme", suppress_errors);
  }

  if (uri_text[scheme_end + 1] == '/' && uri_text[scheme_end + 2] == '/') {
    authority_begin = scheme_end + 3;
    for (i = authority_begin; uri_text[i] != 0 && authority_end == NOT_SET;
         i++) {
      if (uri_text[i] == '/' || uri_text[i] == '?' || uri_text[i] == '#') {
        authority_end = i;
      }
    }
    if (authority_end == NOT_SET && uri_text[i] == 0) {
      authority_end = i;
    }
    if (authority_end == NOT_SET) {
      return bad_uri(uri_text, i, "authority", suppress_errors);
    }
    /* TODO(ctiller): parse the authority correctly */
    path_begin = authority_end;
  } else {
    path_begin = scheme_end + 1;
  }

  for (i = path_begin; uri_text[i] != 0; i++) {
    if (uri_text[i] == '?' || uri_text[i] == '#') {
      path_end = i;
      break;
    }
  }
  if (path_end == NOT_SET && uri_text[i] == 0) {
    path_end = i;
  }
  if (path_end == NOT_SET) {
    return bad_uri(uri_text, i, "path", suppress_errors);
  }

  if (uri_text[i] == '?') {
    query_begin = ++i;
    if (!parse_fragment_or_query(uri_text, &i)) {
      return bad_uri(uri_text, i, "query", suppress_errors);
    } else if (uri_text[i] != 0 && uri_text[i] != '#') {
      /* We must be at the end or at the beginning of a fragment */
      return bad_uri(uri_text, i, "query", suppress_errors);
    }
    query_end = i;
  }
  if (uri_text[i] == '#') {
    fragment_begin = ++i;
    if (!parse_fragment_or_query(uri_text, &i)) {
      return bad_uri(uri_text, i - fragment_end, "fragment", suppress_errors);
    } else if (uri_text[i] != 0) {
      /* We must be at the end */
      return bad_uri(uri_text, i, "fragment", suppress_errors);
    }
    fragment_end = i;
  }

  uri = (grpc_uri*)gpr_zalloc(sizeof(*uri));
  uri->scheme = decode_and_copy_component(uri_text, scheme_begin, scheme_end);
  uri->authority =
      decode_and_copy_component(uri_text, authority_begin, authority_end);
  uri->path = decode_and_copy_component(uri_text, path_begin, path_end);
  uri->query = decode_and_copy_component(uri_text, query_begin, query_end);
  uri->fragment =
      decode_and_copy_component(uri_text, fragment_begin, fragment_end);
  parse_query_parts(uri);

  return uri;
}

const char* grpc_uri_get_query_arg(const grpc_uri* uri, const char* key) {
  GPR_ASSERT(key != nullptr);
  if (key[0] == '\0') return nullptr;

  for (size_t i = 0; i < uri->num_query_parts; ++i) {
    if (0 == strcmp(key, uri->query_parts[i])) {
      return uri->query_parts_values[i];
    }
  }
  return nullptr;
}

void grpc_uri_destroy(grpc_uri* uri) {
  if (!uri) return;
  gpr_free(uri->scheme);
  gpr_free(uri->authority);
  gpr_free(uri->path);
  gpr_free(uri->query);
  for (size_t i = 0; i < uri->num_query_parts; ++i) {
    gpr_free(uri->query_parts[i]);
    gpr_free(uri->query_parts_values[i]);
  }
  gpr_free(uri->query_parts);
  gpr_free(uri->query_parts_values);
  gpr_free(uri->fragment);
  gpr_free(uri);
}

/*
 *
 * Copyright 2015, Google Inc.
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

#include "src/core/ext/client_config/uri_parser.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/slice.h>
#include <grpc/support/slice_buffer.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/support/string.h"

/** a size_t default value... maps to all 1's */
#define NOT_SET (~(size_t)0)

static grpc_uri *bad_uri(const char *uri_text, size_t pos, const char *section,
                         int suppress_errors) {
  char *line_prefix;
  size_t pfx_len;

  if (!suppress_errors) {
    gpr_asprintf(&line_prefix, "bad uri.%s: '", section);
    pfx_len = strlen(line_prefix) + pos;
    gpr_log(GPR_ERROR, "%s%s'", line_prefix, uri_text);
    gpr_free(line_prefix);

    line_prefix = gpr_malloc(pfx_len + 1);
    memset(line_prefix, ' ', pfx_len);
    line_prefix[pfx_len] = 0;
    gpr_log(GPR_ERROR, "%s^ here", line_prefix);
    gpr_free(line_prefix);
  }

  return NULL;
}

/** Returns a copy of \a src[begin, end) */
static char *copy_component(const char *src, size_t begin, size_t end) {
  char *out = gpr_malloc(end - begin + 1);
  memcpy(out, src + begin, end - begin);
  out[end - begin] = 0;
  return out;
}

/** Returns how many chars to advance if \a uri_text[i] begins a valid \a pchar
 * production. If \a uri_text[i] introduces an invalid \a pchar (such as percent
 * sign not followed by two hex digits), NOT_SET is returned. */
static size_t parse_pchar(const char *uri_text, size_t i) {
  /* pchar = unreserved / pct-encoded / sub-delims / ":" / "@"
   * unreserved = ALPHA / DIGIT / "-" / "." / "_" / "~"
   * pct-encoded = "%" HEXDIG HEXDIG
   * sub-delims = "!" / "$" / "&" / "'" / "(" / ")"
   / "*" / "+" / "," / ";" / "=" */
  char c = uri_text[i];
  if (((c >= 'A') && (c <= 'Z')) || ((c >= 'a') && (c <= 'z')) ||
      ((c >= '0') && (c <= '9')) ||
      (c == '-' || c == '.' || c == '_' || c == '~') || /* unreserved */
      (c == '!' || c == '$' || c == '&' || c == '\'' || c == '$' || c == '&' ||
       c == '(' || c == ')' || c == '*' || c == '+' || c == ',' || c == ';' ||
       c == '=') /* sub-delims */) {
    return 1;
  }
  if (c == '%') { /* pct-encoded */
    size_t j;
    if (uri_text[i + 1] == 0 || uri_text[i + 2] == 0) {
      return NOT_SET;
    }
    for (j = i + 1; j < 2; j++) {
      c = uri_text[j];
      if (!(((c >= '0') && (c <= '9')) || ((c >= 'a') && (c <= 'f')) ||
            ((c >= 'A') && (c <= 'F')))) {
        return NOT_SET;
      }
    }
    return 2;
  }
  return 0;
}

/* *( pchar / "?" / "/" ) */
static int parse_fragment_or_query(const char *uri_text, size_t *i) {
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

static void do_nothing(void *ignored) {}
static void parse_query_parts(grpc_uri *uri) {
  static const char *QUERY_PARTS_SEPARATOR = "&";
  static const char *QUERY_PARTS_VALUE_SEPARATOR = "=";
  GPR_ASSERT(uri->query != NULL);
  if (uri->query[0] == '\0') {
    uri->query_parts = NULL;
    uri->query_parts_values = NULL;
    uri->num_query_parts = 0;
    return;
  }
  gpr_slice query_slice =
      gpr_slice_new(uri->query, strlen(uri->query), do_nothing);
  gpr_slice_buffer query_parts; /* the &-separated elements of the query */
  gpr_slice_buffer query_param_parts; /* the =-separated subelements */

  gpr_slice_buffer_init(&query_parts);
  gpr_slice_buffer_init(&query_param_parts);

  gpr_slice_split(query_slice, QUERY_PARTS_SEPARATOR, &query_parts);
  uri->query_parts = gpr_malloc(query_parts.count * sizeof(char *));
  uri->query_parts_values = gpr_malloc(query_parts.count * sizeof(char *));
  uri->num_query_parts = query_parts.count;
  for (size_t i = 0; i < query_parts.count; i++) {
    gpr_slice_split(query_parts.slices[i], QUERY_PARTS_VALUE_SEPARATOR,
                    &query_param_parts);
    GPR_ASSERT(query_param_parts.count > 0);
    uri->query_parts[i] =
        gpr_dump_slice(query_param_parts.slices[0], GPR_DUMP_ASCII);
    if (query_param_parts.count > 1) {
      /* TODO(dgq): only the first value after the separator is considered.
       * Perhaps all chars after the first separator for the query part should
       * be included, even if they include the separator. */
      uri->query_parts_values[i] =
          gpr_dump_slice(query_param_parts.slices[1], GPR_DUMP_ASCII);
    } else {
      uri->query_parts_values[i] = NULL;
    }
    gpr_slice_buffer_reset_and_unref(&query_param_parts);
  }
  gpr_slice_buffer_destroy(&query_parts);
  gpr_slice_buffer_destroy(&query_param_parts);
  gpr_slice_unref(query_slice);
}

grpc_uri *grpc_uri_parse(const char *uri_text, int suppress_errors) {
  grpc_uri *uri;
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

  uri = gpr_malloc(sizeof(*uri));
  memset(uri, 0, sizeof(*uri));
  uri->scheme = copy_component(uri_text, scheme_begin, scheme_end);
  uri->authority = copy_component(uri_text, authority_begin, authority_end);
  uri->path = copy_component(uri_text, path_begin, path_end);
  uri->query = copy_component(uri_text, query_begin, query_end);
  uri->fragment = copy_component(uri_text, fragment_begin, fragment_end);
  parse_query_parts(uri);

  return uri;
}

const char *grpc_uri_get_query_arg(const grpc_uri *uri, const char *key) {
  GPR_ASSERT(key != NULL);
  if (key[0] == '\0') return NULL;

  for (size_t i = 0; i < uri->num_query_parts; ++i) {
    if (0 == strcmp(key, uri->query_parts[i])) {
      return uri->query_parts_values[i];
    }
  }
  return NULL;
}

void grpc_uri_destroy(grpc_uri *uri) {
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

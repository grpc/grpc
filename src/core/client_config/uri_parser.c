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

#include "src/core/client_config/uri_parser.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

static grpc_uri *bad_uri(const char *uri_text, int pos, const char *section,
                         int suppress_errors) {
  char *line_prefix;
  int pfx_len;

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

static char *copy_fragment(const char *src, int begin, int end) {
  char *out = gpr_malloc(end - begin + 1);
  memcpy(out, src + begin, end - begin);
  out[end - begin] = 0;
  return out;
}

grpc_uri *grpc_uri_parse(const char *uri_text, int suppress_errors) {
  grpc_uri *uri;
  int scheme_begin = 0;
  int scheme_end = -1;
  int authority_begin = -1;
  int authority_end = -1;
  int path_begin = -1;
  int path_end = -1;
  int i;

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
  if (scheme_end == -1) {
    return bad_uri(uri_text, i, "scheme", suppress_errors);
  }

  if (uri_text[scheme_end + 1] == '/' && uri_text[scheme_end + 2] == '/') {
    authority_begin = scheme_end + 3;
    for (i = authority_begin; uri_text[i] != 0 && authority_end == -1; i++) {
      if (uri_text[i] == '/') {
        authority_end = i;
      }
      if (uri_text[i] == '?') {
        return bad_uri(uri_text, i, "query_not_supported", suppress_errors);
      }
      if (uri_text[i] == '#') {
        return bad_uri(uri_text, i, "fragment_not_supported", suppress_errors);
      }
    }
    if (authority_end == -1 && uri_text[i] == 0) {
      authority_end = i;
    }
    if (authority_end == -1) {
      return bad_uri(uri_text, i, "authority", suppress_errors);
    }
    /* TODO(ctiller): parse the authority correctly */
    path_begin = authority_end;
  } else {
    path_begin = scheme_end + 1;
  }

  for (i = path_begin; uri_text[i] != 0; i++) {
    if (uri_text[i] == '?') {
      return bad_uri(uri_text, i, "query_not_supported", suppress_errors);
    }
    if (uri_text[i] == '#') {
      return bad_uri(uri_text, i, "fragment_not_supported", suppress_errors);
    }
  }
  path_end = i;

  uri = gpr_malloc(sizeof(*uri));
  memset(uri, 0, sizeof(*uri));
  uri->scheme = copy_fragment(uri_text, scheme_begin, scheme_end);
  uri->authority = copy_fragment(uri_text, authority_begin, authority_end);
  uri->path = copy_fragment(uri_text, path_begin, path_end);

  return uri;
}

void grpc_uri_destroy(grpc_uri *uri) {
  if (!uri) return;
  gpr_free(uri->scheme);
  gpr_free(uri->authority);
  gpr_free(uri->path);
  gpr_free(uri);
}

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

#include "src/core/lib/uri/uri_parser.h"

#include <string.h>

#include <string>

#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"

#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/gpr/string.h"
#include "src/core/lib/slice/percent_encoding.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"

/** a size_t default value... maps to all 1's */
#define NOT_SET (~(size_t)0)

namespace {

void LogBadUri(absl::string_view uri_text, size_t pos, const char* section,
               bool suppress_errors) {
  if (!suppress_errors) {
    std::string line_prefix = absl::StrFormat("bad uri.%s: '", section);
    gpr_log(GPR_ERROR, "%s%s'", line_prefix.c_str(),
            std::string(uri_text).c_str());
    size_t pfx_len = line_prefix.size() + pos;
    gpr_log(GPR_ERROR, "%s^ here", std::string(pfx_len, ' ').c_str());
  }
}

/** Returns a copy of percent decoded \a src[begin, end) */
char* DecodeAndCopyComponent(absl::string_view src, size_t begin, size_t end) {
  grpc_slice component =
      (begin == NOT_SET || end == NOT_SET)
          ? grpc_empty_slice()
          : grpc_slice_from_copied_buffer(src.data() + begin, end - begin);
  grpc_slice decoded_component =
      grpc_permissive_percent_decode_slice(component);
  char* out = grpc_dump_slice(decoded_component, GPR_DUMP_ASCII);
  grpc_slice_unref_internal(component);
  grpc_slice_unref_internal(decoded_component);
  return out;
}

bool IsValidHex(char c) {
  return ((c >= 'a') && (c <= 'f')) || ((c >= 'A') && (c <= 'F')) ||
         ((c >= '0') && (c <= '9'));
}

/** Returns how many chars to advance if \a uri_text[i] begins a valid \a pchar
 * production. If \a uri_text[i] introduces an invalid \a pchar (such as percent
 * sign not followed by two hex digits), NOT_SET is returned. */
size_t ParsePChar(absl::string_view uri_text, size_t i) {
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
      if (uri_text.size() > i + 2 && IsValidHex(uri_text[i + 1]) &&
          IsValidHex(uri_text[i + 2])) {
        return 2;
      }
      return NOT_SET;
  }
  return 0;
}

/* *( pchar / "?" / "/" ) */
int ParseFragmentOrQuery(absl::string_view uri_text, size_t* i) {
  while (uri_text.size() > *i) {
    const size_t advance = ParsePChar(uri_text, *i); /* pchar */
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
}   // namespace

namespace grpc {

std::unique_ptr<GrpcURI> GrpcURI::Parse(absl::string_view uri_text,
                                        bool suppress_errors) {
  std::string scheme;
  std::string authority;
  std::string path;
  std::string query;
  std::string fragment;
  absl::flat_hash_map<std::string, std::string> query_params;

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

  // Parse scheme
  for (i = scheme_begin; i < uri_text.size(); ++i) {
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
    LogBadUri(uri_text, i, "scheme", suppress_errors);
    return nullptr;
  }
  scheme = DecodeAndCopyComponent(uri_text, scheme_begin, scheme_end);

  // Parse authority
  if (uri_text.size() > scheme_end + 2 && uri_text[scheme_end + 1] == '/' &&
      uri_text[scheme_end + 2] == '/') {
    authority_begin = scheme_end + 3;
    for (i = authority_begin; uri_text.size() > i && authority_end == NOT_SET;
         i++) {
      if (uri_text[i] == '/' || uri_text[i] == '?' || uri_text[i] == '#') {
        authority_end = i;
      }
    }
    if (authority_end == NOT_SET && uri_text.size() == i) {
      authority_end = i;
    }
    if (authority_end == NOT_SET) {
      LogBadUri(uri_text, i, "authority", suppress_errors);
      return nullptr;
    }
    /* TODO(ctiller): parse the authority correctly */
    path_begin = authority_end;
    authority =
        DecodeAndCopyComponent(uri_text, authority_begin, authority_end);
  } else {
    path_begin = scheme_end + 1;
  }

  // Parse path
  for (i = path_begin; i < uri_text.size(); ++i) {
    if (uri_text[i] == '?' || uri_text[i] == '#') {
      path_end = i;
      break;
    }
  }
  if (path_end == NOT_SET && uri_text.size() == i) {
    path_end = i;
  }
  if (path_end == NOT_SET) {
    LogBadUri(uri_text, i, "path", suppress_errors);
    return nullptr;
  }
  path = DecodeAndCopyComponent(uri_text, path_begin, path_end);

  // Parse query
  if (uri_text.size() > i && uri_text[i] == '?') {
    query_begin = ++i;
    if (!ParseFragmentOrQuery(uri_text, &i)) {
      LogBadUri(uri_text, i, "query", suppress_errors);
      return nullptr;
    } else if (uri_text.size() > i && uri_text[i] != '#') {
      /* We must be at the end or at the beginning of a fragment */
      LogBadUri(uri_text, i, "query", suppress_errors);
      return nullptr;
    }
    query_end = i;
    query = DecodeAndCopyComponent(uri_text, query_begin, query_end);
    for (absl::string_view query_param : absl::StrSplit(query, '&')) {
      const std::vector<absl::string_view> possible_kv =
          absl::StrSplit(query_param, '=');
      if (possible_kv[0].empty()) continue;
      if (possible_kv.size() > 1) {
        query_params[possible_kv[0]] = std::string(possible_kv[1]);
      } else {
        query_params[possible_kv[0]] = "";
      }
    }
  }

  // Parse fragment
  if (uri_text.size() > i && uri_text[i] == '#') {
    fragment_begin = ++i;
    if (!ParseFragmentOrQuery(uri_text, &i)) {
      LogBadUri(uri_text, i - fragment_end, "fragment", suppress_errors);
      return nullptr;
    } else if (uri_text.size() > i) {
      /* We must be at the end */
      LogBadUri(uri_text, i, "fragment", suppress_errors);
      return nullptr;
    }
    fragment_end = i;
    fragment = DecodeAndCopyComponent(uri_text, fragment_begin, fragment_end);
  }

  return absl::make_unique<GrpcURI>((std::move(scheme), std::move(authority),
                                      std::move(path), std::move(query_params),
                                      std::move(fragment)));
}

GrpcURI::GrpcURI(std::string scheme, std::string authority, std::string path,
                 absl::flat_hash_map<std::string, std::string> query_parts,
                 std::string fragment)
    : scheme_(std::move(scheme)),
      authority_(std::move(authority)),
      path_(std::move(path)),
      query_parts_(std::move(query_parts)),
      fragment_(std::move(fragment)) {}

}  // namespace grpc

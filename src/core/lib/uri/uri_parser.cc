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

#include "absl/strings/escaping.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"
#include "re2/re2.h"

#include <grpc/support/log.h>

#include "src/core/lib/gpr/string.h"

namespace grpc_core {
namespace {

// Similar to `grpc_permissive_percent_decode_slice`, this %-decodes all valid
// triplets, and passes through the rest verbatim.
absl::StatusOr<std::string> PercentDecode(absl::string_view str) {
  if (str.empty() || !absl::StrContains(str, "%")) {
    return std::string(str);
  }
  std::string out;
  std::string unescaped;
  out.reserve(str.size());
  for (size_t i = 0; i < str.length(); i++) {
    if (str[i] != '%') {
      out += str[i];
      continue;
    }
    if (i + 3 >= str.length() ||
        !absl::CUnescape(absl::StrCat("\\x", str.substr(i + 1, 2)),
                         &unescaped)) {
      out += str[i];
    } else {
      out += unescaped[0];
      i += 2;
    }
  }
  return out;
}

// Checks if this string is made up of pchars, '/', '?', and '%' exclusively.
// See https://tools.ietf.org/html/rfc3986#section-3.4
absl::Status IsPCharString(absl::string_view fragment) {
  if (!RE2::FullMatch(std::string(fragment),
                      "[[:alnum:]?/:@\\-._~!$&'()*+,;=%]*")) {
    return absl::InvalidArgumentError(
        absl::StrFormat("'%s' contains invalid characters", fragment));
  }
  return absl::OkStatus();
}

absl::Status MakeInvalidURIStatus(absl::string_view part_name,
                                  absl::string_view uri,
                                  absl::Status previous_error) {
  return absl::InvalidArgumentError(
      absl::StrFormat("Could not parse '%s' from uri '%s'. Error: %s",
                      part_name, uri, previous_error.ToString()));
}
}  // namespace

absl::StatusOr<URI> URI::Parse(absl::string_view uri_text) {
  std::string scheme;
  std::string authority;
  std::string path;
  std::string query;
  std::string fragment;
  absl::flat_hash_map<std::string, std::string> query_params;
  std::string remaining;
  absl::StatusOr<std::string> decoded;
  std::pair<absl::string_view, absl::string_view> uri_split;
  // parse fragment
  uri_split = absl::StrSplit(uri_text, absl::MaxSplits('#', 1));
  if (!uri_split.second.empty()) {
    absl::Status is_pchar = IsPCharString(uri_split.second);
    if (!is_pchar.ok()) {
      return MakeInvalidURIStatus("fragment", uri_text, is_pchar);
    }
    decoded = PercentDecode(uri_split.second);
    if (!decoded.ok()) {
      return MakeInvalidURIStatus("fragment", uri_text, decoded.status());
    }
    fragment = decoded.value();
  }
  // parse query
  uri_split = absl::StrSplit(uri_split.first, absl::MaxSplits('?', 1));
  if (!uri_split.second.empty()) {
    absl::Status is_pchar = IsPCharString(uri_split.second);
    if (!is_pchar.ok()) {
      return MakeInvalidURIStatus("query string", uri_text, is_pchar);
    }
    decoded = PercentDecode(uri_split.second);
    if (!decoded.ok()) {
      return MakeInvalidURIStatus("query string", uri_text, decoded.status());
    }
    // extract query parameters from the *encoded* query string, then decode
    // each value in turn.
    for (absl::string_view query_param :
         absl::StrSplit(uri_split.second, '&')) {
      const std::pair<absl::string_view, absl::string_view> possible_kv =
          absl::StrSplit(query_param, absl::MaxSplits('=', 1));
      if (possible_kv.first.empty()) continue;
      absl::StatusOr<std::string> key = PercentDecode(possible_kv.first);
      if (!key.ok()) {
        return MakeInvalidURIStatus("query part", uri_text, key.status());
      }
      absl::StatusOr<std::string> val = PercentDecode(possible_kv.second);
      if (!val.ok()) {
        return MakeInvalidURIStatus("query part", uri_text, val.status());
      }
      query_params[key.value()] = val.value();
    }
    query = decoded.value();
  }
  // parse scheme
  if (!absl::StrContains(uri_split.first, ":")) {
    return MakeInvalidURIStatus(
        "scheme", uri_text,
        absl::InvalidArgumentError("A valid scheme is required."));
  }
  uri_split = absl::StrSplit(uri_split.first, absl::MaxSplits(':', 1));
  scheme = std::string(uri_split.first);
  if (!RE2::FullMatch(scheme, "[[:alnum:]+\\-.]+")) {
    return MakeInvalidURIStatus(
        "scheme", uri_text,
        absl::InvalidArgumentError("Scheme contains invalid characters."));
  }
  remaining = std::string(uri_split.second);
  // parse authority
  if (absl::StrContains(uri_split.second, "//")) {
    absl::string_view unslashed_remaining = absl::StripPrefix(remaining, "//");
    uri_split = absl::StrSplit(unslashed_remaining, absl::MaxSplits('/', 1));
    decoded = PercentDecode(uri_split.first);
    if (!decoded.ok()) {
      return MakeInvalidURIStatus("authority", uri_text, decoded.status());
    }
    authority = decoded.value();
    remaining =
        absl::StrCat(absl::StrContains(unslashed_remaining, "/") ? "/" : "",
                     uri_split.second);
  }
  // parse path
  if (!remaining.empty()) {
    decoded = PercentDecode(remaining);
    if (!decoded.ok()) {
      return MakeInvalidURIStatus("path", uri_text, decoded.status());
    }
    path = decoded.value();
  }

  return URI(std::move(scheme), std::move(authority), std::move(path),
             std::move(query_params), std::move(fragment));
}

URI::URI(std::string scheme, std::string authority, std::string path,
         absl::flat_hash_map<std::string, std::string> query_parts,
         std::string fragment)
    : scheme_(std::move(scheme)),
      authority_(std::move(authority)),
      path_(std::move(path)),
      query_parts_(std::move(query_parts)),
      fragment_(std::move(fragment)) {}

}  // namespace grpc_core

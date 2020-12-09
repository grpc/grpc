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

#include <map>
#include <string>

#include "absl/strings/escaping.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"

#include <grpc/support/log.h>

#include "src/core/lib/gpr/string.h"

namespace grpc_core {
namespace {

// Similar to `grpc_permissive_percent_decode_slice`, this %-decodes all valid
// triplets, and passes through the rest verbatim.
std::string PercentDecode(absl::string_view str) {
  if (str.empty() || !absl::StrContains(str, "%")) {
    return std::string(str);
  }
  std::string out;
  std::string unescaped;
  out.reserve(str.size());
  for (size_t i = 0; i < str.length(); i++) {
    unescaped = "";
    if (str[i] != '%') {
      out += str[i];
      continue;
    }
    if (i + 3 >= str.length() ||
        !absl::CUnescape(absl::StrCat("\\x", str.substr(i + 1, 2)),
                         &unescaped) ||
        unescaped.length() > 1) {
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
bool IsPCharString(absl::string_view str) {
  return (str.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                "abcdefghijklmnopqrstuvwxyz"
                                "0123456789"
                                "?/:@\\-._~!$&'()*+,;=%") ==
          absl::string_view::npos);
}

absl::Status MakeInvalidURIStatus(absl::string_view part_name,
                                  absl::string_view uri,
                                  absl::string_view extra) {
  return absl::InvalidArgumentError(absl::StrFormat(
      "Could not parse '%s' from uri '%s'. %s", part_name, uri, extra));
}
}  // namespace

absl::StatusOr<URI> URI::Parse(absl::string_view uri_text) {
  absl::StatusOr<std::string> decoded;
  absl::string_view remaining = uri_text;
  // parse scheme
  size_t idx = remaining.find(':');
  if (idx == remaining.npos || idx == 0) {
    return MakeInvalidURIStatus("scheme", uri_text, "Scheme not found.");
  }
  std::string scheme(remaining.substr(0, idx));
  if (scheme.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                               "abcdefghijklmnopqrstuvwxyz"
                               "0123456789+-.") != std::string::npos) {
    return MakeInvalidURIStatus("scheme", uri_text,
                                "Scheme contains invalid characters.");
  }
  if (!isalpha(scheme[0])) {
    return MakeInvalidURIStatus(
        "scheme", uri_text,
        "Scheme must begin with an alpha character [A-Za-z].");
  }
  remaining.remove_prefix(scheme.length() + 1);
  // parse authority
  std::string authority;
  if (absl::StartsWith(remaining, "//")) {
    remaining.remove_prefix(2);
    authority =
        PercentDecode(remaining.substr(0, remaining.find_first_of("/?#")));
    remaining.remove_prefix(authority.length());
  }
  // parse path
  std::string path;
  if (!remaining.empty()) {
    path = PercentDecode(remaining.substr(0, remaining.find_first_of("?#")));
    remaining.remove_prefix(path.length());
  }
  // parse query
  std::vector<QueryParam> query_param_pairs;
  if (!remaining.empty() && remaining[0] == '?') {
    remaining.remove_prefix(1);
    absl::string_view tmp_query = remaining.substr(0, remaining.find('#'));
    if (tmp_query.empty()) {
      return MakeInvalidURIStatus("query", uri_text, "Invalid query string.");
    }
    if (!IsPCharString(tmp_query)) {
      return MakeInvalidURIStatus("query string", uri_text,
                                  "Query string contains invalid characters.");
    }
    for (absl::string_view query_param : absl::StrSplit(tmp_query, '&')) {
      const std::pair<absl::string_view, absl::string_view> possible_kv =
          absl::StrSplit(query_param, absl::MaxSplits('=', 1));
      if (possible_kv.first.empty()) continue;
      query_param_pairs.push_back({PercentDecode(possible_kv.first),
                                   PercentDecode(possible_kv.second)});
    }
    remaining.remove_prefix(tmp_query.length());
  }
  std::string fragment;
  if (!remaining.empty() && remaining[0] == '#') {
    remaining.remove_prefix(1);
    if (!IsPCharString(remaining)) {
      return MakeInvalidURIStatus("fragment", uri_text,
                                  "Fragment contains invalid characters.");
    }
    fragment = PercentDecode(remaining);
  }
  return URI(std::move(scheme), std::move(authority), std::move(path),
             std::move(query_param_pairs), std::move(fragment));
}

URI::URI(std::string scheme, std::string authority, std::string path,
         std::vector<QueryParam> query_parameter_pairs, std::string fragment)
    : scheme_(std::move(scheme)),
      authority_(std::move(authority)),
      path_(std::move(path)),
      query_parameter_pairs_(std::move(query_parameter_pairs)),
      fragment_(std::move(fragment)) {
  for (const auto& kv : query_parameter_pairs_) {
    query_parameter_map_[kv.key] = kv.value;
  }
}

URI::URI(const URI& other)
    : scheme_(other.scheme_),
      authority_(other.authority_),
      path_(other.path_),
      query_parameter_pairs_(other.query_parameter_pairs_),
      fragment_(other.fragment_) {
  for (const auto& kv : query_parameter_pairs_) {
    query_parameter_map_[kv.key] = kv.value;
  }
}

URI& URI::operator=(const URI& other) {
  if (this == &other) {
    return *this;
  }
  scheme_ = other.scheme_;
  authority_ = other.authority_;
  path_ = other.path_;
  query_parameter_pairs_ = other.query_parameter_pairs_;
  fragment_ = other.fragment_;
  for (const auto& kv : query_parameter_pairs_) {
    query_parameter_map_[kv.key] = kv.value;
  }
  return *this;
}
}  // namespace grpc_core

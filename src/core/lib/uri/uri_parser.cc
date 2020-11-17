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
  if (fragment.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                 "abcdefghijklmnopqrstuvwxyz"
                                 "0123456789"
                                 "?/:@\\-._~!$&'()*+,;=%]*") !=
      absl::string_view::npos) {
    return absl::InvalidArgumentError(
        absl::StrFormat("'%s' contains invalid characters", fragment));
  }
  return absl::OkStatus();
}

absl::Status MakeInvalidURIStatus(absl::string_view part_name,
                                  absl::string_view uri) {
  return absl::InvalidArgumentError(
      absl::StrFormat("Could not parse '%s' from uri '%s'.", part_name, uri));
}
absl::Status MakeInvalidURIStatus(absl::string_view part_name,
                                  absl::string_view uri,
                                  absl::Status previous_error) {
  return absl::InvalidArgumentError(absl::StrFormat(
      "%s Error: %s", MakeInvalidURIStatus(part_name, uri).ToString(),
      previous_error.ToString()));
}
}  // namespace

absl::StatusOr<URI> URI::Parse(absl::string_view uri_text) {
  absl::StatusOr<std::string> decoded;
  absl::string_view remaining = uri_text;
  // parse scheme
  std::string scheme = std::string(remaining.substr(0, remaining.find(':')));
  if (scheme == uri_text || scheme.empty() ||
      scheme.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                               "abcdefghijklmnopqrstuvwxyz"
                               "0123456789+\\-.]") != std::string::npos) {
    return MakeInvalidURIStatus("scheme", uri_text);
  }
  remaining.remove_prefix(scheme.length() + 1);
  // parse authority
  std::string authority;
  if (absl::StartsWith(remaining, "//")) {
    remaining.remove_prefix(2);
    decoded =
        PercentDecode(remaining.substr(0, remaining.find_first_of("/?#")));
    if (!decoded.ok()) {
      return MakeInvalidURIStatus("authority", uri_text, decoded.status());
    }
    if (!decoded.value().empty()) {
      authority = decoded.value();
    }
    remaining.remove_prefix(authority.length());
  }
  // parse path
  std::string path;
  if (!remaining.empty()) {
    decoded = PercentDecode(remaining.substr(0, remaining.find_first_of("?#")));
    if (!decoded.ok()) {
      return MakeInvalidURIStatus("authority", uri_text, decoded.status());
    }
    path = decoded.value();
    remaining.remove_prefix(path.length());
  }
  // parse query
  std::string query;
  std::vector<QueryParam> query_param_pairs;
  if (!remaining.empty() && remaining[0] == '?') {
    remaining.remove_prefix(1);
    absl::string_view tmp_query = remaining.substr(0, remaining.find('#'));
    if (tmp_query.empty()) {
      return MakeInvalidURIStatus("query", uri_text);
    }
    absl::Status is_pchar = IsPCharString(tmp_query);
    if (!is_pchar.ok()) {
      return MakeInvalidURIStatus("query string", uri_text, is_pchar);
    }
    for (absl::string_view query_param : absl::StrSplit(tmp_query, '&')) {
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
      query_param_pairs.push_back({key.value(), val.value()});
    }
    remaining.remove_prefix(tmp_query.length());
  }
  std::string fragment;
  if (!remaining.empty() && remaining[0] == '#') {
    remaining.remove_prefix(1);
    absl::Status is_pchar = IsPCharString(remaining);
    if (!is_pchar.ok()) {
      return MakeInvalidURIStatus("fragment", uri_text, is_pchar);
    }
    decoded = PercentDecode(remaining);
    if (!decoded.ok()) {
      return MakeInvalidURIStatus("fragment", uri_text, decoded.status());
    }
    fragment = decoded.value();
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

}  // namespace grpc_core

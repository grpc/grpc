//
//
// Copyright 2020 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#include <grpc/support/port_platform.h>

#include "src/core/lib/security/credentials/tls/tls_utils.h"

#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"

namespace grpc_core {

// Based on
// https://github.com/grpc/grpc-java/blob/ca12e7a339add0ef48202fb72434b9dc0df41756/xds/src/main/java/io/grpc/xds/internal/sds/trust/SdsX509TrustManager.java#L62
bool VerifySubjectAlternativeName(absl::string_view subject_alternative_name,
                                  const std::string& matcher) {
  if (subject_alternative_name.empty() ||
      absl::StartsWith(subject_alternative_name, ".")) {
    // Illegal pattern/domain name
    return false;
  }
  if (matcher.empty() || absl::StartsWith(matcher, ".")) {
    // Illegal domain name
    return false;
  }
  // Normalize \a subject_alternative_name and \a matcher by turning them into
  // absolute domain names if they are not yet absolute. This is needed because
  // server certificates do not normally contain absolute names or patterns, but
  // they should be treated as absolute. At the same time, any
  // subject_alternative_name presented to this method should also be treated as
  // absolute for the purposes of matching to the server certificate.
  std::string normalized_san =
      absl::EndsWith(subject_alternative_name, ".")
          ? std::string(subject_alternative_name)
          : absl::StrCat(subject_alternative_name, ".");
  std::string normalized_matcher =
      absl::EndsWith(matcher, ".") ? matcher : absl::StrCat(matcher, ".");
  absl::AsciiStrToLower(&normalized_san);
  absl::AsciiStrToLower(&normalized_matcher);
  if (!absl::StrContains(normalized_san, "*")) {
    return normalized_san == normalized_matcher;
  }
  // WILDCARD PATTERN RULES:
  // 1. Asterisk (*) is only permitted in the left-most domain name label and
  //    must be the only character in that label (i.e., must match the whole
  //    left-most label). For example, *.example.com is permitted, while
  //    *a.example.com, a*.example.com, a*b.example.com, a.*.example.com are
  //    not permitted.
  // 2. Asterisk (*) cannot match across domain name labels.
  //    For example, *.example.com matches test.example.com but does not match
  //    sub.test.example.com.
  // 3. Wildcard patterns for single-label domain names are not permitted.
  if (!absl::StartsWith(normalized_san, "*.")) {
    // Asterisk (*) is only permitted in the left-most domain name label and
    // must be the only character in that label
    return false;
  }
  if (normalized_san == "*.") {
    // Wildcard pattern for single-label domain name -- not permitted.
    return false;
  }
  absl::string_view suffix = absl::string_view(normalized_san).substr(1);
  if (absl::StrContains(suffix, "*")) {
    // Asterisk (*) is not permitted in the suffix
    return false;
  }
  if (!absl::EndsWith(normalized_matcher, suffix)) return false;
  int suffix_start_index = normalized_matcher.length() - suffix.length();
  // Asterisk matching across domain labels is not permitted.
  return suffix_start_index <= 0 /* should not happen */ ||
         normalized_matcher.find_last_of('.', suffix_start_index - 1) ==
             std::string::npos;
}

}  // namespace grpc_core

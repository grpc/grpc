//
//
// Copyright 2021 gRPC authors.
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

#include "src/core/ext/xds/xds_routing.h"

namespace grpc_core {

bool XdsRouting::IsValidDomainPattern(absl::string_view domain_pattern) {
  return DomainPatternMatchType(domain_pattern) != XdsRouting::INVALID_MATCH;
}

// Returns true if match succeeds.
bool XdsRouting::DomainMatch(MatchType match_type,
                             absl::string_view domain_pattern_in,
                             absl::string_view expected_host_name_in) {
  // Normalize the args to lower-case. Domain matching is case-insensitive.
  std::string domain_pattern = std::string(domain_pattern_in);
  std::string expected_host_name = std::string(expected_host_name_in);
  std::transform(domain_pattern.begin(), domain_pattern.end(),
                 domain_pattern.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  std::transform(expected_host_name.begin(), expected_host_name.end(),
                 expected_host_name.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  if (match_type == EXACT_MATCH) {
    return domain_pattern == expected_host_name;
  } else if (match_type == SUFFIX_MATCH) {
    // Asterisk must match at least one char.
    if (expected_host_name.size() < domain_pattern.size()) return false;
    absl::string_view pattern_suffix(domain_pattern.c_str() + 1);
    absl::string_view host_suffix(expected_host_name.c_str() +
                                  expected_host_name.size() -
                                  pattern_suffix.size());
    return pattern_suffix == host_suffix;
  } else if (match_type == PREFIX_MATCH) {
    // Asterisk must match at least one char.
    if (expected_host_name.size() < domain_pattern.size()) return false;
    absl::string_view pattern_prefix(domain_pattern.c_str(),
                                     domain_pattern.size() - 1);
    absl::string_view host_prefix(expected_host_name.c_str(),
                                  pattern_prefix.size());
    return pattern_prefix == host_prefix;
  } else {
    return match_type == UNIVERSE_MATCH;
  }
}

XdsRouting::MatchType XdsRouting::DomainPatternMatchType(
    absl::string_view domain_pattern) {
  if (domain_pattern.empty()) return INVALID_MATCH;
  if (domain_pattern.find('*') == std::string::npos) return EXACT_MATCH;
  if (domain_pattern == "*") return UNIVERSE_MATCH;
  if (domain_pattern[0] == '*') return SUFFIX_MATCH;
  if (domain_pattern[domain_pattern.size() - 1] == '*') return PREFIX_MATCH;
  return INVALID_MATCH;
}

absl::optional<absl::string_view> XdsRouting::GetHeaderValue(
    grpc_metadata_batch* initial_metadata, absl::string_view header_name,
    std::string* concatenated_value) {
  // Note: If we ever allow binary headers here, we still need to
  // special-case ignore "grpc-tags-bin" and "grpc-trace-bin", since
  // they are not visible to the LB policy in grpc-go.
  if (absl::EndsWith(header_name, "-bin")) {
    return absl::nullopt;
  } else if (header_name == "content-type") {
    return "application/grpc";
  }
  return grpc_metadata_batch_get_value(initial_metadata, header_name,
                                       concatenated_value);
}

bool XdsRouting::HeadersMatch(const std::vector<HeaderMatcher>& header_matchers,
                              grpc_metadata_batch* initial_metadata) {
  for (const auto& header_matcher : header_matchers) {
    std::string concatenated_value;
    if (!header_matcher.Match(GetHeaderValue(
            initial_metadata, header_matcher.name(), &concatenated_value))) {
      return false;
    }
  }
  return true;
}

bool XdsRouting::UnderFraction(const uint32_t fraction_per_million) {
  // Generate a random number in [0, 1000000).
  const uint32_t random_number = rand() % 1000000;
  return random_number < fraction_per_million;
}

}  // namespace grpc_core

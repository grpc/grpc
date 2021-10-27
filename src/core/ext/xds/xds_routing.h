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

#ifndef GRPC_CORE_EXT_XDS_XDS_ROUTING_H
#define GRPC_CORE_EXT_XDS_XDS_ROUTING_H

#include <grpc/support/port_platform.h>

#include <vector>

#include "absl/strings/string_view.h"

#include <grpc/support/log.h>

#include "src/core/lib/matchers/matchers.h"
#include "src/core/lib/transport/metadata_batch.h"

namespace grpc_core {

class XdsRouting {
 public:
  template <typename T>
  static T* FindVirtualHostForDomain(std::vector<T>* virtual_hosts,
                                     absl::string_view domain) {
    // Find the best matched virtual host.
    // The search order for 4 groups of domain patterns:
    //   1. Exact match.
    //   2. Suffix match (e.g., "*ABC").
    //   3. Prefix match (e.g., "ABC*").
    //   4. Universe match (i.e., "*").
    // Within each group, longest match wins.
    // If the same best matched domain pattern appears in multiple virtual
    // hosts, the first matched virtual host wins.
    T* target_vhost = nullptr;
    MatchType best_match_type = INVALID_MATCH;
    size_t longest_match = 0;
    // Check each domain pattern in each virtual host to determine the best
    // matched virtual host.
    for (T& vhost : *virtual_hosts) {
      for (const std::string& domain_pattern : vhost.domains) {
        // Check the match type first. Skip the pattern if it's not better
        // than current match.
        const MatchType match_type = DomainPatternMatchType(domain_pattern);
        // This should be caught by RouteConfigParse().
        GPR_ASSERT(match_type != INVALID_MATCH);
        if (match_type > best_match_type) continue;
        if (match_type == best_match_type &&
            domain_pattern.size() <= longest_match) {
          continue;
        }
        // Skip if match fails.
        if (!DomainMatch(match_type, domain_pattern, domain)) continue;
        // Choose this match.
        target_vhost = &vhost;
        best_match_type = match_type;
        longest_match = domain_pattern.size();
        if (best_match_type == EXACT_MATCH) break;
      }
      if (best_match_type == EXACT_MATCH) break;
    }
    return target_vhost;
  }

  // Returns true if \a domain_pattern is a valid domain pattern, false
  // otherwise.
  static bool IsValidDomainPattern(absl::string_view domain_pattern);

  // Returns the metadata value(s) for the specified key.
  // As special cases, binary headers return a value of absl::nullopt, and
  // "content-type" header returns "application/grpc".
  static absl::optional<absl::string_view> GetHeaderValue(
      grpc_metadata_batch* initial_metadata, absl::string_view header_name,
      std::string* concatenated_value);

  // Returns true if the headers match, false otherwise.
  static bool HeadersMatch(const std::vector<HeaderMatcher>& header_matchers,
                           grpc_metadata_batch* initial_metadata);

  // Returns true if the random number generated is less than \a
  // fraction_per_million, false otherwise.
  static bool UnderFraction(const uint32_t fraction_per_million);

 private:
  enum MatchType {
    EXACT_MATCH,
    SUFFIX_MATCH,
    PREFIX_MATCH,
    UNIVERSE_MATCH,
    INVALID_MATCH,
  };
  // Returns true if match succeeds.
  static bool DomainMatch(MatchType match_type,
                          absl::string_view domain_pattern_in,
                          absl::string_view expected_host_name_in);
  static MatchType DomainPatternMatchType(absl::string_view domain_pattern);
};

}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_XDS_XDS_ROUTING_H

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

#ifndef GRPC_CORE_LIB_URI_URI_PARSER_H
#define GRPC_CORE_LIB_URI_URI_PARSER_H

#include <grpc/support/port_platform.h>

#include <stddef.h>

#include <map>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

class URI {
 public:
  struct QueryParam {
    std::string key;
    std::string value;
    bool operator==(const QueryParam& other) const {
      return key == other.key && value == other.value;
    }
  };

  // Creates an instance of GrpcURI by parsing an rfc3986 URI string. Returns
  // an IllegalArgumentError on failure.
  static absl::StatusOr<URI> Parse(absl::string_view uri_text);
  // Explicit construction by individual URI components
  URI(std::string scheme, std::string authority, std::string path,
      std::vector<QueryParam> query_parameter_pairs, std::string fragment_);
  URI() = default;
  // Copy construction and assignment
  URI(const URI& other);
  URI& operator=(const URI& other);
  // Move construction and assignment
  URI(URI&&) = default;
  URI& operator=(URI&&) = default;

  const std::string& scheme() const { return scheme_; }
  const std::string& authority() const { return authority_; }
  const std::string& path() const { return path_; }
  // Stores the *last* value appearing for each repeated key in the query
  // string. If you need to capture repeated query parameters, use
  // `query_parameter_pairs`.
  const std::map<absl::string_view, absl::string_view>& query_parameter_map()
      const {
    return query_parameter_map_;
  }
  // A vector of key:value query parameter pairs, kept in order of appearance
  // within the URI search string. Repeated keys are represented as separate
  // key:value elements.
  const std::vector<QueryParam>& query_parameter_pairs() const {
    return query_parameter_pairs_;
  }
  const std::string& fragment() const { return fragment_; }

 private:
  std::string scheme_;
  std::string authority_;
  std::string path_;
  std::map<absl::string_view, absl::string_view> query_parameter_map_;
  std::vector<QueryParam> query_parameter_pairs_;
  std::string fragment_;
};
}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_URI_URI_PARSER_H */

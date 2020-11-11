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

#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

class URI {
 public:
  URI() = default;

  URI(std::string scheme, std::string authority, std::string path,
      absl::flat_hash_map<std::string, std::string> query_parts,
      std::string fragment_);

  // Creates an instance of GrpcURI by parsing an rfc3986 URI string. Returns
  // an IllegalArgumentError on failure.
  static absl::StatusOr<URI> Parse(absl::string_view uri_text);

  const std::string& scheme() const { return scheme_; }
  const std::string& authority() const { return authority_; }
  const std::string& path() const { return path_; }
  const absl::flat_hash_map<std::string, std::string>& query_parameters()
      const {
    return query_parts_;
  }
  const std::string& fragment() const { return fragment_; }

 private:
  std::string scheme_;
  std::string authority_;
  std::string path_;
  absl::flat_hash_map<std::string, std::string> query_parts_;
  std::string fragment_;
};
}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_URI_URI_PARSER_H */

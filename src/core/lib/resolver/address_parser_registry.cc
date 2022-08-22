// Copyright 2022 gRPC authors.
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

#include "src/core/lib/resolver/address_parser_registry.h"

#include "absl/strings/strip.h"

#include <grpc/support/log.h>

namespace grpc_core {

void AddressParserRegistry::Builder::AddScheme(absl::string_view scheme,
                                               AddressParser parser) {
  std::string prefix = absl::StrCat(scheme, ":");
  for (const auto& parser : parsers_) {
    GPR_ASSERT(parser.prefix != prefix);
  }
  parsers_.emplace_back(Parser{std::move(prefix), std::move(parser)});
}

AddressParserRegistry AddressParserRegistry::Builder::Build() {
  return AddressParserRegistry(std::move(parsers_));
}

absl::StatusOr<std::vector<grpc_resolved_address>> AddressParserRegistry::Parse(
    absl::string_view uri) const {
  for (const auto& parser : parsers_) {
    if (absl::ConsumePrefix(&uri, parser.prefix)) {
      return parser.parser(uri);
    }
  }
  return absl::InvalidArgumentError(
      absl::StrCat("Unsupported URI scheme for: ", uri));
}

}  // namespace grpc_core

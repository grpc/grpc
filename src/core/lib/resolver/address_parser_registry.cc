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

#include <grpc/support/port_platform.h>

#include "src/core/lib/resolver/address_parser_registry.h"

#include <algorithm>
#include <string>

#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"

#include <grpc/support/log.h>

namespace grpc_core {

void AddressParserRegistry::Builder::AddScheme(absl::string_view scheme,
                                               AddressParser parser) {
  for (const auto& parser : parsers_) {
    GPR_ASSERT(parser.scheme != scheme);
  }
  parsers_.emplace_back(Parser{scheme, std::move(parser)});
}

AddressParserRegistry AddressParserRegistry::Builder::Build() {
  return AddressParserRegistry(std::move(parsers_));
}

absl::StatusOr<grpc_resolved_address> AddressParserRegistry::Parse(
    const URI& uri) const {
  auto parser = GetParser(uri);
  if (!parser.ok()) return parser.status();
  if (absl::StrContains(uri.path(), ',')) {
    return absl::InvalidArgumentError(
        "AddressParserRegistry::ParseSingleAddress() does not support "
        "multiple addresses in a single URI.");
  }
  return (**parser)(uri.path());
}

absl::StatusOr<const AddressParser*> AddressParserRegistry::GetParser(
    const URI& uri) const {
  if (!uri.authority().empty()) {
    gpr_log(GPR_ERROR, "authority-based URIs not supported by the %s scheme",
            uri.scheme().c_str());
    return absl::InvalidArgumentError(absl::StrCat(
        "authority-based URIs not supported by the ", uri.scheme(), " scheme"));
  }
  for (const auto& parser : parsers_) {
    if (parser.scheme != uri.scheme()) continue;
    return &parser.parser;
  }
  return absl::InvalidArgumentError(
      absl::StrCat("Unsupported URI scheme: ", uri.scheme()));
}

bool AddressParserRegistry::HasScheme(absl::string_view scheme) const {
  for (const auto& parser : parsers_) {
    if (parser.scheme == scheme) return true;
  }
  return false;
}

}  // namespace grpc_core

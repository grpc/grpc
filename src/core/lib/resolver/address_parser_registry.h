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

#ifndef ADDRESS_PARSER_REGISTRY_H
#define ADDRESS_PARSER_REGISTRY_H

#include <functional>
#include <string>

#include "absl/status/statusor.h"

#include "src/core/lib/iomgr/resolved_address.h"

namespace grpc_core {

using AddressParser =
    std::function<absl::StatusOr<std::vector<grpc_resolved_address>>(
        absl::string_view)>;

class AddressParserRegistry {
 private:
  struct Parser {
    std::string prefix;
    AddressParser parser;
  };

 public:
  class Builder {
   public:
    void AddScheme(absl::string_view scheme, AddressParser parser);

    AddressParserRegistry Build();

   private:
    std::vector<Parser> parsers_;
  };

  AddressParserRegistry(AddressParserRegistry&&) = default;
  AddressParserRegistry& operator=(AddressParserRegistry&&) = default;

  absl::StatusOr<std::vector<grpc_resolved_address>> Parse(
      absl::string_view uri) const;

 private:
  AddressParserRegistry() = delete;
  explicit AddressParserRegistry(std::vector<Parser> parsers)
      : parsers_(std::move(parsers)) {}
  AddressParserRegistry(const AddressParserRegistry&) = delete;
  AddressParserRegistry& operator=(const AddressParserRegistry&) = delete;

  std::vector<Parser> parsers_;
};

}  // namespace grpc_core

#endif

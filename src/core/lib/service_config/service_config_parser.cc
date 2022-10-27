//
// Copyright 2015 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/lib/service_config/service_config_parser.h"

#include <stdlib.h>

#include <string>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"

#include <grpc/support/log.h>

namespace grpc_core {

ServiceConfigParser ServiceConfigParser::Builder::Build() {
  return ServiceConfigParser(std::move(registered_parsers_));
}

void ServiceConfigParser::Builder::RegisterParser(
    std::unique_ptr<Parser> parser) {
  for (const auto& registered_parser : registered_parsers_) {
    if (registered_parser->name() == parser->name()) {
      gpr_log(GPR_ERROR, "%s",
              absl::StrCat("Parser with name '", parser->name(),
                           "' already registered")
                  .c_str());
      // We'll otherwise crash later.
      abort();
    }
  }
  registered_parsers_.emplace_back(std::move(parser));
}

absl::StatusOr<ServiceConfigParser::ParsedConfigVector>
ServiceConfigParser::ParseGlobalParameters(const ChannelArgs& args,
                                           const Json& json) const {
  ParsedConfigVector parsed_global_configs;
  std::vector<std::string> errors;
  for (size_t i = 0; i < registered_parsers_.size(); i++) {
    auto parsed_config = registered_parsers_[i]->ParseGlobalParams(args, json);
    if (!parsed_config.ok()) {
      errors.emplace_back(parsed_config.status().message());
    } else {
      parsed_global_configs.push_back(std::move(*parsed_config));
    }
  }
  if (!errors.empty()) {
    return absl::InvalidArgumentError(absl::StrJoin(errors, "; "));
  }
  return std::move(parsed_global_configs);
}

absl::StatusOr<ServiceConfigParser::ParsedConfigVector>
ServiceConfigParser::ParsePerMethodParameters(const ChannelArgs& args,
                                              const Json& json) const {
  ParsedConfigVector parsed_method_configs;
  std::vector<std::string> errors;
  for (size_t i = 0; i < registered_parsers_.size(); ++i) {
    auto parsed_config =
        registered_parsers_[i]->ParsePerMethodParams(args, json);
    if (!parsed_config.ok()) {
      errors.emplace_back(parsed_config.status().message());
    } else {
      parsed_method_configs.push_back(std::move(*parsed_config));
    }
  }
  if (!errors.empty()) {
    return absl::InvalidArgumentError(absl::StrJoin(errors, "; "));
  }
  return std::move(parsed_method_configs);
}

size_t ServiceConfigParser::GetParserIndex(absl::string_view name) const {
  for (size_t i = 0; i < registered_parsers_.size(); ++i) {
    if (registered_parsers_[i]->name() == name) return i;
  }
  return -1;
}

}  // namespace grpc_core

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

#include "src/core/service_config/service_config_parser.h"

#include <stdlib.h>

#include <string>

#include "absl/log/log.h"

#include <grpc/support/port_platform.h>

namespace grpc_core {

ServiceConfigParser ServiceConfigParser::Builder::Build() {
  return ServiceConfigParser(std::move(registered_parsers_));
}

void ServiceConfigParser::Builder::RegisterParser(
    std::unique_ptr<Parser> parser) {
  for (const auto& registered_parser : registered_parsers_) {
    if (registered_parser->name() == parser->name()) {
      LOG(ERROR) << "Parser with name '" << parser->name()
                 << "' already registered";
      // We'll otherwise crash later.
      abort();
    }
  }
  registered_parsers_.emplace_back(std::move(parser));
}

ServiceConfigParser::ParsedConfigVector
ServiceConfigParser::ParseGlobalParameters(const ChannelArgs& args,
                                           const Json& json,
                                           ValidationErrors* errors) const {
  ParsedConfigVector parsed_global_configs;
  for (auto& parser : registered_parsers_) {
    parsed_global_configs.push_back(
        parser->ParseGlobalParams(args, json, errors));
  }
  return parsed_global_configs;
}

ServiceConfigParser::ParsedConfigVector
ServiceConfigParser::ParsePerMethodParameters(const ChannelArgs& args,
                                              const Json& json,
                                              ValidationErrors* errors) const {
  ParsedConfigVector parsed_method_configs;
  for (auto& parser : registered_parsers_) {
    parsed_method_configs.push_back(
        parser->ParsePerMethodParams(args, json, errors));
  }
  return parsed_method_configs;
}

size_t ServiceConfigParser::GetParserIndex(absl::string_view name) const {
  for (size_t i = 0; i < registered_parsers_.size(); ++i) {
    if (registered_parsers_[i]->name() == name) return i;
  }
  return -1;
}

}  // namespace grpc_core

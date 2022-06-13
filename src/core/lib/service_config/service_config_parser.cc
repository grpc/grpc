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

#include "absl/strings/str_cat.h"

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

ServiceConfigParser::ParsedConfigVector
ServiceConfigParser::ParseGlobalParameters(const grpc_channel_args* args,
                                           const Json& json,
                                           grpc_error_handle* error) const {
  ParsedConfigVector parsed_global_configs;
  std::vector<grpc_error_handle> error_list;
  for (size_t i = 0; i < registered_parsers_.size(); i++) {
    grpc_error_handle parser_error = GRPC_ERROR_NONE;
    auto parsed_config =
        registered_parsers_[i]->ParseGlobalParams(args, json, &parser_error);
    if (!GRPC_ERROR_IS_NONE(parser_error)) {
      error_list.push_back(parser_error);
    }
    parsed_global_configs.push_back(std::move(parsed_config));
  }
  if (!error_list.empty()) {
    *error = GRPC_ERROR_CREATE_FROM_VECTOR("Global Params", &error_list);
  }
  return parsed_global_configs;
}

ServiceConfigParser::ParsedConfigVector
ServiceConfigParser::ParsePerMethodParameters(const grpc_channel_args* args,
                                              const Json& json,
                                              grpc_error_handle* error) const {
  ParsedConfigVector parsed_method_configs;
  std::vector<grpc_error_handle> error_list;
  for (size_t i = 0; i < registered_parsers_.size(); ++i) {
    grpc_error_handle parser_error = GRPC_ERROR_NONE;
    auto parsed_config =
        registered_parsers_[i]->ParsePerMethodParams(args, json, &parser_error);
    if (!GRPC_ERROR_IS_NONE(parser_error)) {
      error_list.push_back(parser_error);
    }
    parsed_method_configs.push_back(std::move(parsed_config));
  }
  if (!error_list.empty()) {
    *error = GRPC_ERROR_CREATE_FROM_VECTOR("methodConfig", &error_list);
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

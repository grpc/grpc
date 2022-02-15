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

#include <grpc/support/log.h>

namespace grpc_core {

size_t ServiceConfigParser::Builder::RegisterParser(
    std::unique_ptr<Parser> parser) {
  registered_parsers_.push_back(std::move(parser));
  return registered_parsers_.size() - 1;
}

ServiceConfigParser::ParsedConfigVector
ServiceConfigParser::ParseGlobalParameters(const grpc_channel_args* args,
                                           const Json& json,
                                           grpc_error_handle* error) {
  ParsedConfigVector parsed_global_configs;
  std::vector<grpc_error_handle> error_list;
  for (size_t i = 0; i < registered_parsers_.size(); i++) {
    grpc_error_handle parser_error = GRPC_ERROR_NONE;
    auto parsed_config =
        registered_parsers_[i]->ParseGlobalParams(args, json, &parser_error);
    if (parser_error != GRPC_ERROR_NONE) {
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
                                              grpc_error_handle* error) {
  ParsedConfigVector parsed_method_configs;
  std::vector<grpc_error_handle> error_list;
  for (size_t i = 0; i < registered_parsers_.size(); i++) {
    grpc_error_handle parser_error = GRPC_ERROR_NONE;
    auto parsed_config =
        registered_parsers_[i]->ParsePerMethodParams(args, json, &parser_error);
    if (parser_error != GRPC_ERROR_NONE) {
      error_list.push_back(parser_error);
    }
    parsed_method_configs.push_back(std::move(parsed_config));
  }
  if (!error_list.empty()) {
    *error = GRPC_ERROR_CREATE_FROM_VECTOR("methodConfig", &error_list);
  }
  return parsed_method_configs;
}

}  // namespace grpc_core

//
// Copyright 2016 gRPC authors.
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

#ifndef GRPC_CORE_LIB_SERVICE_CONFIG_SERVICE_CONFIG_PARSER_H
#define GRPC_CORE_LIB_SERVICE_CONFIG_SERVICE_CONFIG_PARSER_H

#include <grpc/support/port_platform.h>

#include <memory>
#include <vector>

#include <grpc/impl/codegen/grpc_types.h>

#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/json/json.h"

namespace grpc_core {

// Service config parser registry.
// See service_config.h for more information.
class ServiceConfigParser {
 public:
  /// This is the base class that all service config parsers MUST use to store
  /// parsed service config data.
  class ParsedConfig {
   public:
    virtual ~ParsedConfig() = default;
  };

  /// This is the base class that all service config parsers should derive from.
  class Parser {
   public:
    virtual ~Parser() = default;

    virtual absl::string_view name() const = 0;

    virtual std::unique_ptr<ParsedConfig> ParseGlobalParams(
        const grpc_channel_args*, const Json& /* json */,
        grpc_error_handle* error) {
      // Avoid unused parameter warning on debug-only parameter
      (void)error;
      GPR_DEBUG_ASSERT(error != nullptr);
      return nullptr;
    }

    virtual std::unique_ptr<ParsedConfig> ParsePerMethodParams(
        const grpc_channel_args*, const Json& /* json */,
        grpc_error_handle* error) {
      // Avoid unused parameter warning on debug-only parameter
      (void)error;
      GPR_DEBUG_ASSERT(error != nullptr);
      return nullptr;
    }
  };

  using ServiceConfigParserList = std::vector<std::unique_ptr<Parser>>;
  using ParsedConfigVector = std::vector<std::unique_ptr<ParsedConfig>>;

  class Builder {
   public:
    /// Globally register a service config parser. Each new service config
    /// update will go through all the registered parser. Each parser is
    /// responsible for reading the service config json and returning a parsed
    /// config.
    void RegisterParser(std::unique_ptr<Parser> parser);

    ServiceConfigParser Build();

   private:
    ServiceConfigParserList registered_parsers_;
  };

  ParsedConfigVector ParseGlobalParameters(const grpc_channel_args* args,
                                           const Json& json,
                                           grpc_error_handle* error) const;

  ParsedConfigVector ParsePerMethodParameters(const grpc_channel_args* args,
                                              const Json& json,
                                              grpc_error_handle* error) const;

  // Return the index for a given registered parser.
  // If there is an error, return -1.
  size_t GetParserIndex(absl::string_view name) const;

 private:
  explicit ServiceConfigParser(ServiceConfigParserList registered_parsers)
      : registered_parsers_(std::move(registered_parsers)) {}
  ServiceConfigParserList registered_parsers_;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_SERVICE_CONFIG_SERVICE_CONFIG_PARSER_H */

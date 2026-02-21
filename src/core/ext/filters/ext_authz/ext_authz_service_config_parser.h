//
// Copyright 2026 gRPC authors.
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

#ifndef GRPC_SRC_CORE_EXT_FILTERS_EXT_AUTHZ_EXT_AUTHZ_SERVICE_CONFIG_PARSER_H
#define GRPC_SRC_CORE_EXT_FILTERS_EXT_AUTHZ_EXT_AUTHZ_SERVICE_CONFIG_PARSER_H

#include <grpc/support/port_platform.h>

#include <memory>
#include <optional>
#include <vector>

#include "re2/re2.h"
#include "src/core/config/core_configuration.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/service_config/service_config_parser.h"
#include "src/core/util/json/json.h"
#include "src/core/util/matchers.h"
#include "src/core/util/shared_bit_gen.h"
#include "src/core/xds/grpc/xds_common_types.h"
#include "absl/random/bit_gen_ref.h"
#include "absl/random/distributions.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

struct ExtAuthz {
  XdsGrpcService xds_grpc_service;

  struct FilterEnabled {
    uint32_t numerator;
    int32_t denominator;  // 100, 10000, 1000000

    bool operator==(const FilterEnabled& other) const {
      return numerator == other.numerator && denominator == other.denominator;
    }
    static const JsonLoaderInterface* JsonLoader(const JsonArgs&);
    void JsonPostLoad(const Json&, const JsonArgs&, ValidationErrors* errors);
  };
  std::optional<FilterEnabled> filter_enabled;

  std::optional<bool> deny_at_disable = true;
  bool failure_mode_allow;
  bool failure_mode_allow_header_add;
  grpc_status_code status_on_error;

  std::vector<StringMatch> allowed_headers;
  std::vector<StringMatch> disallowed_headers;
  
  bool isHeaderPresentInAllowedHeaders(std::string key) const; 
  bool isHeaderPresentInDisallowedHeaders(std::string key) const;

  std::optional<HeaderMutationRules> decoder_header_mutation_rules;
  bool include_peer_certificate = false;

  // enum class CheckResult {
  //   kSendRequestToExtAuthzService,
  //   kPassThrough,
  //   kDeny,
  // };

  // CheckResult CheckRequestAllowed() const {
  //   if (!filter_enabled.has_value()) {
  //     return CheckResult::kSendRequestToExtAuthzService;
  //   }
  //   const auto& enabled = *filter_enabled;
  //   uint32_t denominator_val;
  //   switch (enabled.denominator) {
  //     case FilterEnabled::Denominator::Hundred:
  //       denominator_val = 100;
  //       break;
  //     case FilterEnabled::Denominator::Thousand:
  //       denominator_val = 10000;
  //       break;
  //     case FilterEnabled::Denominator::Million:
  //       denominator_val = 1000000;
  //       break;
  //   }

  //   // Logic: if filter_enabled < 100% (numerator < denominator)
  //   if (enabled.numerator < denominator_val) {
  //     // random_number = generate_random_number(0, denominator);
  //     // We use [0, denominator) range for simple < numerator check.
  //     // If user wanted 1-based [1, denominator], logic would be different.
  //     // But standard fractional percent implies P = numerator/denominator.
  //     // Uniform<uint32_t> produces [min, max).
  //     grpc_core::SharedBitGen g;
  //     uint32_t random_number =
  //         absl::Uniform<uint32_t>(absl::BitGenRef(g), 0, denominator_val);
  //     if (random_number >= enabled.numerator) {
  //       if (deny_at_disable) {
  //         return CheckResult::kDeny;
  //       } else {
  //         return CheckResult::kPassThrough;
  //       }
  //     }
  //   }
  //   return CheckResult::kSendRequestToExtAuthzService;
  // }
  static const JsonLoaderInterface* JsonLoader(const JsonArgs&);
  void JsonPostLoad(const Json&, const JsonArgs&, ValidationErrors* errors);

  bool operator==(const ExtAuthz& other) const;
};

class ExtAuthzParsedConfig : public ServiceConfigParser::ParsedConfig {
 public:
  struct Config {
    std::string filter_instance_name;
    ExtAuthz ext_authz;

    static const JsonLoaderInterface* JsonLoader(const JsonArgs&);
    void JsonPostLoad(const Json&, const JsonArgs&, ValidationErrors* errors);
  };

  const Config* GetConfig(size_t index) const {
    if (index >= configs_.size()) return nullptr;
    return &configs_[index];
  }

  static const JsonLoaderInterface* JsonLoader(const JsonArgs&);

  // Parses protos from the blobs loaded from JSON.
  // This is separate because we need ChannelArgs (XdsClient) which is not
  // available in JsonLoader.
  void ParseProtos(const ChannelArgs& args, ValidationErrors* errors);

 private:
  std::vector<Config> configs_;
};

class ExtAuthzServiceConfigParser : public ServiceConfigParser::Parser {
 public:
  absl::string_view name() const override { return parser_name(); }

  std::unique_ptr<ServiceConfigParser::ParsedConfig> ParseGlobalParams(
      const ChannelArgs& args, const Json& json,
      ValidationErrors* errors) override;

  std::unique_ptr<ServiceConfigParser::ParsedConfig> ParsePerMethodParams(
      const ChannelArgs& args, const Json& json,
      ValidationErrors* errors) override;

  static void Register(CoreConfiguration::Builder* builder);
  static size_t ParserIndex();
  static absl::string_view parser_name() { return "ext_authz"; }
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_FILTERS_EXT_AUTHZ_EXT_AUTHZ_SERVICE_CONFIG_PARSER_H

//
// Copyright 2021 gRPC authors.
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

#ifndef GRPC_CORE_EXT_FILTERS_FAULT_INJECTION_SERVICE_CONFIG_PARSER_H
#define GRPC_CORE_EXT_FILTERS_FAULT_INJECTION_SERVICE_CONFIG_PARSER_H

#include <grpc/support/port_platform.h>

#include <vector>

#include "src/core/ext/filters/client_channel/service_config.h"
#include "src/core/lib/iomgr/exec_ctx.h"

namespace grpc_core {

class FaultInjectionMethodParsedConfig
    : public ServiceConfigParser::ParsedConfig {
 public:
  struct FaultInjectionPolicy {
    grpc_status_code abort_code = GRPC_STATUS_OK;
    std::string abort_message;
    std::string abort_code_header;
    std::string abort_percentage_header;
    uint32_t abort_percentage_numerator = 0;
    uint32_t abort_percentage_denominator = 100;

    grpc_millis delay = 0;
    std::string delay_header;
    std::string delay_percentage_header;
    uint32_t delay_percentage_numerator = 0;
    uint32_t delay_percentage_denominator = 100;

    // By default, the max allowed active faults are unlimited.
    uint32_t max_faults = std::numeric_limits<uint32_t>::max();
  };

  explicit FaultInjectionMethodParsedConfig(
      std::vector<FaultInjectionPolicy> fault_injection_policies)
      : fault_injection_policies_(std::move(fault_injection_policies)) {}

  // Returns the fault injection policy at certain index.
  // There might be multiple fault injection policies functioning at the same
  // time. The order between the policies are stable, and an index is used to
  // keep track of their relative positions. The FaultInjectionFilter uses this
  // method to access the parsed fault injection policy in service config,
  // whether it came from xDS resolver or directly from service config
  const FaultInjectionPolicy* fault_injection_policy(int index) const {
    if (static_cast<size_t>(index) >= fault_injection_policies_.size()) {
      return nullptr;
    }
    return &fault_injection_policies_[index];
  }

 private:
  std::vector<FaultInjectionPolicy> fault_injection_policies_;
};

class FaultInjectionServiceConfigParser : public ServiceConfigParser::Parser {
 public:
  // Parses the per-method service config for fault injection filter.
  std::unique_ptr<ServiceConfigParser::ParsedConfig> ParsePerMethodParams(
      const grpc_channel_args* args, const Json& json,
      grpc_error_handle* error) override;
  // Returns the parser index for FaultInjectionServiceConfigParser.
  static size_t ParserIndex();
  // Registers FaultInjectionServiceConfigParser to ServiceConfigParser.
  static void Register();
};

}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_FILTERS_FAULT_INJECTION_SERVICE_CONFIG_PARSER_H

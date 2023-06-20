// Copyright 2023 gRPC authors.
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
// Automatically generated by tools/codegen/core/gen_config_vars.py
//

#include "test/core/util/fuzz_config_vars.h"

#include <string>

#include "absl/types/optional.h"

#include "test/core/util/fuzz_config_vars_helpers.h"

namespace grpc_core {

ConfigVars::Overrides OverridesFromFuzzConfigVars(
    const grpc::testing::FuzzConfigVars& vars) {
  ConfigVars::Overrides overrides;
  if (vars.has_enable_fork_support()) {
    overrides.enable_fork_support = vars.enable_fork_support();
  }
  if (vars.has_dns_resolver()) {
    overrides.dns_resolver = vars.dns_resolver();
  }
  if (vars.has_verbosity()) {
    overrides.verbosity = vars.verbosity();
  }
  if (vars.has_stacktrace_minloglevel()) {
    overrides.stacktrace_minloglevel = vars.stacktrace_minloglevel();
  }
  if (vars.has_experiments()) {
    overrides.experiments =
        ValidateExperimentsStringForFuzzing(vars.experiments());
  }
  if (vars.has_trace()) {
    overrides.trace = vars.trace();
  }
  return overrides;
}
void ApplyFuzzConfigVars(const grpc::testing::FuzzConfigVars& vars) {
  ConfigVars::SetOverrides(OverridesFromFuzzConfigVars(vars));
}

}  // namespace grpc_core

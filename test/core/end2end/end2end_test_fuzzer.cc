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

#include <inttypes.h>
#include <string.h>

#include <algorithm>
#include <atomic>
#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <regex>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "absl/types/optional.h"
#include "end2end_tests.h"
#include "gtest/gtest.h"

#include <grpc/compression.h>
#include <grpc/grpc.h>
#include <grpc/grpc_posix.h>
#include <grpc/grpc_security.h>
#include <grpc/grpc_security_constants.h>
#include <grpc/slice.h>
#include <grpc/status.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/config/config_vars.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/gprpp/env.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/gprpp/no_destruct.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/load_file.h"
#include "src/core/lib/iomgr/port.h"
#include "src/core/lib/security/credentials/fake/fake_credentials.h"
#include "src/libfuzzer/libfuzzer_macro.h"
#include "test/core/end2end/end2end_test_fuzzer.pb.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/end2end/fixtures/h2_oauth2_common.h"
#include "test/core/end2end/fixtures/h2_ssl_cred_reload_fixture.h"
#include "test/core/end2end/fixtures/h2_ssl_tls_common.h"
#include "test/core/end2end/fixtures/h2_tls_common.h"
#include "test/core/end2end/fixtures/http_proxy_fixture.h"
#include "test/core/end2end/fixtures/inproc_fixture.h"
#include "test/core/end2end/fixtures/local_util.h"
#include "test/core/end2end/fixtures/proxy.h"
#include "test/core/end2end/fixtures/secure_fixture.h"
#include "test/core/end2end/fixtures/sockpair_fixture.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

bool squelch = true;
static void dont_log(gpr_log_func_args* /*args*/) {}

DEFINE_PROTO_FUZZER(const core_end2end_test_fuzzer::Msg& msg) {
  static const auto all_tests =
      grpc_core::CoreEnd2endTestRegistry::Get().AllTests();
  static const auto tests = []() {
    std::map<absl::string_view,
             std::map<absl::string_view,
                      std::map<absl::string_view,
                               absl::AnyInvocable<std::unique_ptr<
                                   grpc_core::CoreEnd2endTest>() const>>>>
        tests;
    for (const auto& test : all_tests) {
      tests[test.suite][test.name].emplace(test.config->name, [&test]() {
        return std::unique_ptr<grpc_core::CoreEnd2endTest>(
            test.make_test(test.config));
      });
    }
    return tests;
  }();

  if (squelch && !grpc_core::GetEnv("GRPC_TRACE_FUZZER").has_value()) {
    gpr_set_log_function(dont_log);
  }

  auto suite_it = tests.find(msg.suite());
  if (suite_it == tests.end()) return;
  auto test_it = suite_it->second.find(msg.test());
  if (test_it == suite_it->second.end()) return;
  auto config_it = test_it->second.find(msg.config());
  if (config_it == test_it->second.end()) return;

  // TODO(ctiller): make this per fixture?
  grpc_core::ConfigVars::Overrides overrides;
  overrides.default_ssl_roots_file_path = CA_CERT_PATH;
  grpc_core::ConfigVars::SetOverrides(overrides);

  auto test = config_it->second();
}

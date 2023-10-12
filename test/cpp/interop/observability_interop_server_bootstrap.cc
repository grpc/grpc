//
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
//

#include <signal.h>

#include "absl/flags/flag.h"

#include <grpcpp/ext/gcp_observability.h>

#include "test/core/util/test_config.h"
#include "test/cpp/interop/server_helper.h"
#include "test/cpp/util/test_config.h"

gpr_atm grpc::testing::interop::g_got_sigint;

ABSL_FLAG(bool, enable_observability, false,
          "Whether to enable GCP Observability");

static void sigint_handler(int /*x*/) {
  gpr_atm_no_barrier_store(&grpc::testing::interop::g_got_sigint, true);
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc::testing::InitTest(&argc, &argv, true);
  signal(SIGINT, sigint_handler);

  if (absl::GetFlag(FLAGS_enable_observability)) {
    // TODO(someone): remove deprecated usage
    // NOLINTNEXTLINE(clang-diagnostic-deprecated-declarations)
    auto status = grpc::experimental::GcpObservabilityInit();
    gpr_log(GPR_DEBUG, "GcpObservabilityInit() status_code: %d", status.code());
    if (!status.ok()) {
      return 1;
    }
  }

  grpc::testing::interop::RunServer(
      grpc::testing::CreateInteropServerCredentials());

  return 0;
}

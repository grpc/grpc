//
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
//

#include <memory>
#include <unordered_map>

#include "absl/flags/flag.h"

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/ext/gcp_observability.h>

#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/crash.h"
#include "test/core/util/test_config.h"
#include "test/cpp/interop/client_helper.h"
#include "test/cpp/interop/interop_client.h"
#include "test/cpp/util/test_config.h"

ABSL_FLAG(bool, enable_observability, false,
          "Whether to enable GCP Observability");

using grpc::testing::RunClient;

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc::testing::InitTest(&argc, &argv, true);
  gpr_log(GPR_INFO, "Testing these cases: %s",
          absl::GetFlag(FLAGS_test_case).c_str());

  if (absl::GetFlag(FLAGS_enable_observability)) {
    auto status = grpc::experimental::GcpObservabilityInit();
    gpr_log(GPR_DEBUG, "GcpObservabilityInit() status_code: %d", status.code());
    if (!status.ok()) {
      return 1;
    }
  }

  int ret = RunClient();

  if (absl::GetFlag(FLAGS_enable_observability)) {
    grpc::experimental::GcpObservabilityClose();
    // TODO(stanleycheung): remove this once the observability exporter plugin
    //                      is able to gracefully flush observability data to
    //                      cloud at shutdown
    const int observability_exporter_sleep_seconds = 65;
    gpr_log(GPR_DEBUG, "Sleeping %ds before shutdown.",
            observability_exporter_sleep_seconds);
    gpr_sleep_until(
        gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                     gpr_time_from_seconds(observability_exporter_sleep_seconds,
                                           GPR_TIMESPAN)));
  }

  return ret;
}

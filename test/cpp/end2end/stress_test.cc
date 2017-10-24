/*
 *
 * Copyright 2017 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <memory>
#include <mutex>
#include <sstream>
#include <thread>

#include <grpc++/channel.h>
#include <grpc++/client_context.h>
#include <grpc++/create_channel.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/thd.h>
#include <grpc/support/time.h>

extern "C" {
#include "src/core/ext/filters/client_channel/resolver/fake/fake_resolver.h"
#include "src/core/lib/iomgr/sockaddr.h"
}

#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/cpp/end2end/grpclb_end2end_test.h"
#include "test/cpp/end2end/test_service_impl.h"

#include "src/proto/grpc/lb/v1/load_balancer.grpc.pb.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using std::chrono::system_clock;

using grpc::lb::v1::LoadBalanceRequest;
using grpc::lb::v1::LoadBalanceResponse;
using grpc::lb::v1::LoadBalancer;

namespace grpc {
namespace testing {
namespace {

const int kNumBackends = 4;
const int kNumBalancers = 2;
const int kNumClientThreads = 2;

class StressTest : public GrpclbEnd2endTest {
 public:
  StressTest()
      : GrpclbEnd2endTest(kNumBackends, kNumBalancers, 0),
        num_client_threads_(kNumClientThreads) {}

  void SetUp() override { GrpclbEnd2endTest.SetUp }

  LoadBalanceResponse BuildRandomResponseForBackends() {
    size_t num_drop_entry = rand();
    size_t num_non_drop_entry = rand();
    std::vector<int> all_backend_ports = GetBackendPorts();
    std::vector<int> random_backend_ports;

    for (size_t _ = 0; _ < num_non_drop_entry; ++_) {
      random_backend_ports.push_back(
          all_backend_ports[rand() % all_backend_ports.size()]);
    }
    return BalancerServiceImpl::BuildResponseForBackends(
        random_backend_ports, {{"load_balancing", num_drop_entry}});
  }

 private:
  size_t num_client_threads_;
  std::vector<std::thread> client_threads_;
  std::thread resolver_thread_;
};

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc_init();
  grpc_test_init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  const auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}

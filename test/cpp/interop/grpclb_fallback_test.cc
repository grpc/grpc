/*
 *
 * Copyright 2019 gRPC authors.
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

#include <arpa/inet.h>
#include <fcntl.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/support/channel_arguments.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>

#include "absl/flags/flag.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/iomgr/port.h"
#include "src/core/lib/iomgr/socket_mutator.h"
#include "src/proto/grpc/testing/empty.pb.h"
#include "src/proto/grpc/testing/messages.pb.h"
#include "src/proto/grpc/testing/test.grpc.pb.h"
#include "src/proto/grpc/testing/test.pb.h"
#include "test/cpp/util/test_config.h"
#include "test/cpp/util/test_credentials_provider.h"

ABSL_FLAG(std::string, custom_credentials_type, "",
          "User provided credentials type.");
ABSL_FLAG(std::string, server_uri, "localhost:1000", "Server URI target");
ABSL_FLAG(std::string, unroute_lb_and_backend_addrs_cmd, "exit 1",
          "Shell command used to make LB and backend addresses unroutable");
ABSL_FLAG(std::string, blackhole_lb_and_backend_addrs_cmd, "exit 1",
          "Shell command used to make LB and backend addresses blackholed");
ABSL_FLAG(
    std::string, test_case, "",
    "Test case to run. Valid options are:\n\n"
    "fast_fallback_before_startup : fallback before establishing connection to "
    "LB;\n"
    "fast_fallback_after_startup : fallback after startup due to LB/backend "
    "addresses becoming unroutable;\n"
    "slow_fallback_before_startup : fallback before startup due to LB address "
    "being blackholed;\n"
    "slow_fallback_after_startup : fallback after startup due to LB/backend "
    "addresses becoming blackholed;\n");

#ifdef LINUX_VERSION_CODE
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37)
#define SOCKET_SUPPORTS_TCP_USER_TIMEOUT
#endif
#endif

#ifdef SOCKET_SUPPORTS_TCP_USER_TIMEOUT
using grpc::testing::GrpclbRouteType;
using grpc::testing::SimpleRequest;
using grpc::testing::SimpleResponse;
using grpc::testing::TestService;

namespace {

enum RpcMode {
  FailFast,
  WaitForReady,
};

GrpclbRouteType DoRPCAndGetPath(TestService::Stub* stub, int deadline_seconds,
                                RpcMode rpc_mode) {
  gpr_log(GPR_INFO, "DoRPCAndGetPath deadline_seconds:%d rpc_mode:%d",
          deadline_seconds, rpc_mode);
  SimpleRequest request;
  SimpleResponse response;
  grpc::ClientContext context;
  if (rpc_mode == WaitForReady) {
    context.set_wait_for_ready(true);
  }
  request.set_fill_grpclb_route_type(true);
  std::chrono::system_clock::time_point deadline =
      std::chrono::system_clock::now() + std::chrono::seconds(deadline_seconds);
  context.set_deadline(deadline);
  grpc::Status s = stub->UnaryCall(&context, request, &response);
  if (!s.ok()) {
    gpr_log(GPR_INFO, "DoRPCAndGetPath failed. status-message: %s",
            s.error_message().c_str());
    return GrpclbRouteType::GRPCLB_ROUTE_TYPE_UNKNOWN;
  }
  GPR_ASSERT(response.grpclb_route_type() ==
                 GrpclbRouteType::GRPCLB_ROUTE_TYPE_BACKEND ||
             response.grpclb_route_type() ==
                 GrpclbRouteType::GRPCLB_ROUTE_TYPE_FALLBACK);
  gpr_log(GPR_INFO, "DoRPCAndGetPath done. grpclb_route_type:%d",
          response.grpclb_route_type());
  return response.grpclb_route_type();
}

GrpclbRouteType DoRPCAndGetPath(TestService::Stub* stub, int deadline_seconds) {
  return DoRPCAndGetPath(stub, deadline_seconds, FailFast);
}

GrpclbRouteType DoWaitForReadyRPCAndGetPath(TestService::Stub* stub,
                                            int deadline_seconds) {
  return DoRPCAndGetPath(stub, deadline_seconds, WaitForReady);
}

bool TcpUserTimeoutMutateFd(int fd, grpc_socket_mutator* /*mutator*/) {
  int timeout = 20000;  // 20 seconds
  gpr_log(GPR_INFO, "Setting socket option TCP_USER_TIMEOUT on fd: %d", fd);
  if (0 != setsockopt(fd, IPPROTO_TCP, TCP_USER_TIMEOUT, &timeout,
                      sizeof(timeout))) {
    gpr_log(GPR_ERROR, "Failed to set socket option TCP_USER_TIMEOUT");
    abort();
  }
  int newval;
  socklen_t len = sizeof(newval);
  if (0 != getsockopt(fd, IPPROTO_TCP, TCP_USER_TIMEOUT, &newval, &len) ||
      newval != timeout) {
    gpr_log(GPR_ERROR, "Failed to get expected socket option TCP_USER_TIMEOUT");
    abort();
  }
  return true;
}

int TcpUserTimeoutCompare(grpc_socket_mutator* /*a*/,
                          grpc_socket_mutator* /*b*/) {
  return 0;
}

void TcpUserTimeoutDestroy(grpc_socket_mutator* mutator) { gpr_free(mutator); }

const grpc_socket_mutator_vtable kTcpUserTimeoutMutatorVtable =
    grpc_socket_mutator_vtable{TcpUserTimeoutMutateFd, TcpUserTimeoutCompare,
                               TcpUserTimeoutDestroy};

std::unique_ptr<TestService::Stub> CreateFallbackTestStub() {
  grpc::ChannelArguments channel_args;
  grpc_socket_mutator* tcp_user_timeout_mutator =
      static_cast<grpc_socket_mutator*>(
          gpr_malloc(sizeof(tcp_user_timeout_mutator)));
  grpc_socket_mutator_init(tcp_user_timeout_mutator,
                           &kTcpUserTimeoutMutatorVtable);
  channel_args.SetSocketMutator(tcp_user_timeout_mutator);
  // Allow LB policy to be configured by service config
  channel_args.SetInt(GRPC_ARG_SERVICE_CONFIG_DISABLE_RESOLUTION, 0);
  std::shared_ptr<grpc::ChannelCredentials> channel_creds =
      grpc::testing::GetCredentialsProvider()->GetChannelCredentials(
          absl::GetFlag(FLAGS_custom_credentials_type), &channel_args);
  return TestService::NewStub(grpc::CreateCustomChannel(
      absl::GetFlag(FLAGS_server_uri), channel_creds, channel_args));
}

void RunCommand(const std::string& command) {
  gpr_log(GPR_INFO, "RunCommand: |%s|", command.c_str());
  int out = std::system(command.c_str());
  if (WIFEXITED(out)) {
    int code = WEXITSTATUS(out);
    if (code != 0) {
      gpr_log(GPR_ERROR, "RunCommand failed exit code:%d command:|%s|", code,
              command.c_str());
      abort();
    }
  } else {
    gpr_log(GPR_ERROR, "RunCommand failed command:|%s|", command.c_str());
    abort();
  }
}

void RunFallbackBeforeStartupTest(
    const std::string& break_lb_and_backend_conns_cmd,
    int per_rpc_deadline_seconds) {
  std::unique_ptr<TestService::Stub> stub = CreateFallbackTestStub();
  RunCommand(break_lb_and_backend_conns_cmd);
  for (size_t i = 0; i < 30; i++) {
    GrpclbRouteType grpclb_route_type =
        DoRPCAndGetPath(stub.get(), per_rpc_deadline_seconds);
    if (grpclb_route_type != GrpclbRouteType::GRPCLB_ROUTE_TYPE_FALLBACK) {
      gpr_log(GPR_ERROR, "Expected grpclb route type: FALLBACK. Got: %d",
              grpclb_route_type);
      abort();
    }
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
}

void DoFastFallbackBeforeStartup() {
  RunFallbackBeforeStartupTest(
      absl::GetFlag(FLAGS_unroute_lb_and_backend_addrs_cmd), 9);
}

void DoSlowFallbackBeforeStartup() {
  RunFallbackBeforeStartupTest(
      absl::GetFlag(FLAGS_blackhole_lb_and_backend_addrs_cmd), 20);
}

void RunFallbackAfterStartupTest(
    const std::string& break_lb_and_backend_conns_cmd) {
  std::unique_ptr<TestService::Stub> stub = CreateFallbackTestStub();
  GrpclbRouteType grpclb_route_type = DoRPCAndGetPath(stub.get(), 20);
  if (grpclb_route_type != GrpclbRouteType::GRPCLB_ROUTE_TYPE_BACKEND) {
    gpr_log(GPR_ERROR, "Expected grpclb route type: BACKEND. Got: %d",
            grpclb_route_type);
    abort();
  }
  RunCommand(break_lb_and_backend_conns_cmd);
  for (size_t i = 0; i < 40; i++) {
    GrpclbRouteType grpclb_route_type =
        DoWaitForReadyRPCAndGetPath(stub.get(), 1);
    // Backends should be unreachable by now, otherwise the test is broken.
    GPR_ASSERT(grpclb_route_type != GrpclbRouteType::GRPCLB_ROUTE_TYPE_BACKEND);
    if (grpclb_route_type == GrpclbRouteType::GRPCLB_ROUTE_TYPE_FALLBACK) {
      gpr_log(GPR_INFO,
              "Made one successul RPC to a fallback. Now expect the same for "
              "the rest.");
      break;
    } else {
      gpr_log(GPR_ERROR, "Retryable RPC failure on iteration: %" PRIdPTR, i);
    }
  }
  for (size_t i = 0; i < 30; i++) {
    GrpclbRouteType grpclb_route_type = DoRPCAndGetPath(stub.get(), 20);
    if (grpclb_route_type != GrpclbRouteType::GRPCLB_ROUTE_TYPE_FALLBACK) {
      gpr_log(GPR_ERROR, "Expected grpclb route type: FALLBACK. Got: %d",
              grpclb_route_type);
      abort();
    }
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
}

void DoFastFallbackAfterStartup() {
  RunFallbackAfterStartupTest(
      absl::GetFlag(FLAGS_unroute_lb_and_backend_addrs_cmd));
}

void DoSlowFallbackAfterStartup() {
  RunFallbackAfterStartupTest(
      absl::GetFlag(FLAGS_blackhole_lb_and_backend_addrs_cmd));
}
}  // namespace

int main(int argc, char** argv) {
  grpc::testing::InitTest(&argc, &argv, true);
  gpr_log(GPR_INFO, "Testing: %s", absl::GetFlag(FLAGS_test_case).c_str());
  if (absl::GetFlag(FLAGS_test_case) == "fast_fallback_before_startup") {
    DoFastFallbackBeforeStartup();
    gpr_log(GPR_INFO, "DoFastFallbackBeforeStartup done!");
  } else if (absl::GetFlag(FLAGS_test_case) == "slow_fallback_before_startup") {
    DoSlowFallbackBeforeStartup();
    gpr_log(GPR_INFO, "DoSlowFallbackBeforeStartup done!");
  } else if (absl::GetFlag(FLAGS_test_case) == "fast_fallback_after_startup") {
    DoFastFallbackAfterStartup();
    gpr_log(GPR_INFO, "DoFastFallbackAfterStartup done!");
  } else if (absl::GetFlag(FLAGS_test_case) == "slow_fallback_after_startup") {
    DoSlowFallbackAfterStartup();
    gpr_log(GPR_INFO, "DoSlowFallbackAfterStartup done!");
  } else {
    gpr_log(GPR_ERROR, "Invalid test case: %s",
            absl::GetFlag(FLAGS_test_case).c_str());
    abort();
  }
}
#else
int main(int argc, char** argv) {
  grpc::testing::InitTest(&argc, &argv, true);
  gpr_log(GPR_ERROR,
          "This test requires TCP_USER_TIMEOUT, which isn't available");
  abort();
}
#endif  // SOCKET_SUPPORTS_TCP_USER_TIMEOUT

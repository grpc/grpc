//
//
// Copyright 2019 gRPC authors.
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

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/time.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>

#include <mutex>
#include <thread>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "gtest/gtest.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/pollset.h"
#include "src/core/lib/iomgr/port.h"
#include "src/core/lib/iomgr/tcp_server.h"
#include "src/core/util/crash.h"
#include "src/core/util/env.h"
#include "src/core/util/host_port.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/test_util/port.h"
#include "test/core/test_util/resolve_localhost_ip46.h"
#include "test/core/test_util/test_config.h"
#include "test/core/test_util/test_tcp_server.h"
#include "test/cpp/end2end/test_service_impl.h"
#include "test/cpp/util/test_credentials_provider.h"

#ifdef GRPC_POSIX_SOCKET_TCP_SERVER

#include "src/core/lib/iomgr/tcp_posix.h"

namespace grpc {
namespace testing {
namespace {

class TestScenario {
 public:
  TestScenario(bool server_port, bool pending_data,
               const std::string& creds_type)
      : server_has_port(server_port),
        queue_pending_data(pending_data),
        credentials_type(creds_type) {}
  void Log() const;
  // server has its own port or not
  bool server_has_port;
  // whether tcp server should read some data before handoff
  bool queue_pending_data;
  const std::string credentials_type;
};

std::ostream& operator<<(std::ostream& out, const TestScenario& scenario) {
  return out << "TestScenario{server_has_port="
             << (scenario.server_has_port ? "true" : "false")
             << ", queue_pending_data="
             << (scenario.queue_pending_data ? "true" : "false")
             << ", credentials='" << scenario.credentials_type << "'}";
}

void TestScenario::Log() const {
  std::ostringstream out;
  out << *this;
  LOG(ERROR) << out.str();
}

// Set up a test tcp server which is in charge of accepting connections and
// handing off the connections as fds.
class TestTcpServer {
 public:
  TestTcpServer()
      : shutdown_(false),
        queue_data_(false),
        port_(grpc_pick_unused_port_or_die()) {
    grpc_init();  // needed by LocalIpAndPort()
    // This test does not do well with multiple connection attempts at the same
    // time to the same tcp server, so use the local IP address instead of
    // "localhost" which can result in two connections (ipv4 and ipv6).
    address_ = grpc_core::LocalIpAndPort(port_);
    test_tcp_server_init(&tcp_server_, &TestTcpServer::OnConnect, this);
    GRPC_CLOSURE_INIT(&on_fd_released_, &TestTcpServer::OnFdReleased, this,
                      grpc_schedule_on_exec_ctx);
  }

  ~TestTcpServer() {
    running_thread_.join();
    test_tcp_server_destroy(&tcp_server_);
    grpc_recycle_unused_port(port_);
    grpc_shutdown();
  }

  // Read some data before handing off the connection.
  void SetQueueData() { queue_data_ = true; }

  void Start() {
    test_tcp_server_start(&tcp_server_, port_);
    LOG(INFO) << "Test TCP server started at " << address_;
  }

  const std::string& address() { return address_; }

  void SetAcceptor(
      std::unique_ptr<experimental::ExternalConnectionAcceptor> acceptor) {
    connection_acceptor_ = std::move(acceptor);
  }

  void Run() {
    running_thread_ = std::thread([this]() {
      while (true) {
        {
          std::lock_guard<std::mutex> lock(mu_);
          if (shutdown_) {
            return;
          }
        }
        test_tcp_server_poll(&tcp_server_, 1);
      }
    });
  }

  void Shutdown() {
    std::lock_guard<std::mutex> lock(mu_);
    shutdown_ = true;
  }

  static void OnConnect(void* arg, grpc_endpoint* tcp,
                        grpc_pollset* accepting_pollset,
                        grpc_tcp_server_acceptor* acceptor) {
    auto* self = static_cast<TestTcpServer*>(arg);
    self->OnConnect(tcp, accepting_pollset, acceptor);
  }

  static void OnFdReleased(void* arg, grpc_error_handle err) {
    auto* self = static_cast<TestTcpServer*>(arg);
    self->OnFdReleased(err);
  }

 private:
  void OnConnect(grpc_endpoint* tcp, grpc_pollset* /*accepting_pollset*/,
                 grpc_tcp_server_acceptor* acceptor) {
    std::string peer(grpc_endpoint_get_peer(tcp));
    LOG(INFO) << "Got incoming connection! from " << peer;
    EXPECT_FALSE(acceptor->external_connection);
    listener_fd_ = grpc_tcp_server_port_fd(
        acceptor->from_server, acceptor->port_index, acceptor->fd_index);
    gpr_free(acceptor);
    grpc_tcp_destroy_and_release_fd(tcp, &fd_, &on_fd_released_);
  }

  void OnFdReleased(const absl::Status& err) {
    EXPECT_EQ(absl::OkStatus(), err);
    experimental::ExternalConnectionAcceptor::NewConnectionParameters p;
    p.listener_fd = listener_fd_;
    p.fd = fd_;
    if (queue_data_) {
      char buf[1024];
      ssize_t read_bytes = 0;
      while (read_bytes <= 0) {
        read_bytes = read(fd_, buf, 1024);
      }
      Slice data(buf, read_bytes);
      p.read_buffer = ByteBuffer(&data, 1);
    }
    LOG(INFO) << "Handing off fd " << fd_ << " with data size "
              << static_cast<int>(p.read_buffer.Length())
              << " from listener fd " << listener_fd_;
    connection_acceptor_->HandleNewConnection(&p);
  }

  std::mutex mu_;
  bool shutdown_;

  int listener_fd_ = -1;
  int fd_ = -1;
  bool queue_data_ = false;

  grpc_closure on_fd_released_;
  std::thread running_thread_;
  int port_ = -1;
  std::string address_;
  std::unique_ptr<experimental::ExternalConnectionAcceptor>
      connection_acceptor_;
  test_tcp_server tcp_server_;
};

class PortSharingEnd2endTest : public ::testing::TestWithParam<TestScenario> {
 protected:
  PortSharingEnd2endTest() : is_server_started_(false), first_picked_port_(0) {
    GetParam().Log();
  }

  void SetUp() override {
    if (GetParam().queue_pending_data) {
      tcp_server1_.SetQueueData();
      tcp_server2_.SetQueueData();
    }
    tcp_server1_.Start();
    tcp_server2_.Start();
    ServerBuilder builder;
    if (GetParam().server_has_port) {
      int port = grpc_pick_unused_port_or_die();
      first_picked_port_ = port;
      server_address_ << "localhost:" << port;
      auto creds = GetCredentialsProvider()->GetServerCredentials(
          GetParam().credentials_type);
      builder.AddListeningPort(server_address_.str(), creds);
      LOG(INFO) << "gRPC server listening on " << server_address_.str();
    }
    auto server_creds = GetCredentialsProvider()->GetServerCredentials(
        GetParam().credentials_type);
    auto acceptor1 = builder.experimental().AddExternalConnectionAcceptor(
        ServerBuilder::experimental_type::ExternalConnectionType::FROM_FD,
        server_creds);
    tcp_server1_.SetAcceptor(std::move(acceptor1));
    auto acceptor2 = builder.experimental().AddExternalConnectionAcceptor(
        ServerBuilder::experimental_type::ExternalConnectionType::FROM_FD,
        server_creds);
    tcp_server2_.SetAcceptor(std::move(acceptor2));
    builder.RegisterService(&service_);
    server_ = builder.BuildAndStart();
    is_server_started_ = true;

    tcp_server1_.Run();
    tcp_server2_.Run();
  }

  void TearDown() override {
    tcp_server1_.Shutdown();
    tcp_server2_.Shutdown();
    if (is_server_started_) {
      server_->Shutdown();
    }
    if (first_picked_port_ > 0) {
      grpc_recycle_unused_port(first_picked_port_);
    }
  }

  void ResetStubs() {
    EXPECT_TRUE(is_server_started_);
    ChannelArguments args;
    args.SetInt(GRPC_ARG_USE_LOCAL_SUBCHANNEL_POOL, 1);
    auto channel_creds = GetCredentialsProvider()->GetChannelCredentials(
        GetParam().credentials_type, &args);
    channel_handoff1_ =
        CreateCustomChannel(tcp_server1_.address(), channel_creds, args);
    stub_handoff1_ = EchoTestService::NewStub(channel_handoff1_);
    channel_handoff2_ =
        CreateCustomChannel(tcp_server2_.address(), channel_creds, args);
    stub_handoff2_ = EchoTestService::NewStub(channel_handoff2_);
    if (GetParam().server_has_port) {
      ChannelArguments direct_args;
      direct_args.SetInt(GRPC_ARG_USE_LOCAL_SUBCHANNEL_POOL, 1);
      auto direct_creds = GetCredentialsProvider()->GetChannelCredentials(
          GetParam().credentials_type, &direct_args);
      channel_direct_ =
          CreateCustomChannel(server_address_.str(), direct_creds, direct_args);
      stub_direct_ = EchoTestService::NewStub(channel_direct_);
    }
  }

  bool is_server_started_;
  // channel/stub to the test tcp server, the connection will be handed to the
  // grpc server.
  std::shared_ptr<Channel> channel_handoff1_;
  std::unique_ptr<EchoTestService::Stub> stub_handoff1_;
  std::shared_ptr<Channel> channel_handoff2_;
  std::unique_ptr<EchoTestService::Stub> stub_handoff2_;
  // channel/stub to talk to the grpc server directly, if applicable.
  std::shared_ptr<Channel> channel_direct_;
  std::unique_ptr<EchoTestService::Stub> stub_direct_;
  std::unique_ptr<Server> server_;
  std::ostringstream server_address_;
  TestServiceImpl service_;
  TestTcpServer tcp_server1_;
  TestTcpServer tcp_server2_;
  int first_picked_port_;
};

void SendRpc(EchoTestService::Stub* stub, int num_rpcs) {
  EchoRequest request;
  EchoResponse response;
  request.set_message("Hello hello hello hello");

  for (int i = 0; i < num_rpcs; ++i) {
    ClientContext context;
    Status s = stub->Echo(&context, request, &response);
    EXPECT_EQ(response.message(), request.message());
    EXPECT_TRUE(s.ok());
  }
}

std::vector<TestScenario> CreateTestScenarios() {
  std::vector<TestScenario> scenarios;
  std::vector<std::string> credentials_types;

#if TARGET_OS_IPHONE
  // Workaround Apple CFStream bug
  grpc_core::SetEnv("grpc_cfstream", "0");
#endif

  credentials_types = GetCredentialsProvider()->GetSecureCredentialsTypeList();
  // Only allow insecure credentials type when it is registered with the
  // provider. User may create providers that do not have insecure.
  if (GetCredentialsProvider()->GetChannelCredentials(kInsecureCredentialsType,
                                                      nullptr) != nullptr) {
    credentials_types.push_back(kInsecureCredentialsType);
  }

  CHECK(!credentials_types.empty());
  for (const auto& cred : credentials_types) {
    for (auto server_has_port : {true, false}) {
      for (auto queue_pending_data : {true, false}) {
        scenarios.emplace_back(server_has_port, queue_pending_data, cred);
      }
    }
  }
  return scenarios;
}

TEST_P(PortSharingEnd2endTest, HandoffAndDirectCalls) {
  ResetStubs();
  SendRpc(stub_handoff1_.get(), 5);
  if (GetParam().server_has_port) {
    SendRpc(stub_direct_.get(), 5);
  }
}

TEST_P(PortSharingEnd2endTest, MultipleHandoff) {
  for (int i = 0; i < 3; i++) {
    ResetStubs();
    SendRpc(stub_handoff2_.get(), 1);
  }
}

TEST_P(PortSharingEnd2endTest, TwoHandoffPorts) {
  for (int i = 0; i < 3; i++) {
    ResetStubs();
    SendRpc(stub_handoff1_.get(), 5);
    SendRpc(stub_handoff2_.get(), 5);
  }
}

INSTANTIATE_TEST_SUITE_P(PortSharingEnd2end, PortSharingEnd2endTest,
                         ::testing::ValuesIn(CreateTestScenarios()));

}  // namespace
}  // namespace testing
}  // namespace grpc

#endif  // GRPC_POSIX_SOCKET_TCP_SERVER

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

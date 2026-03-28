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

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/atm.h>
#include <grpc/support/time.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/health_check_service_interface.h>
#include <grpcpp/impl/sync.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/support/validate_service_config.h>

#include <algorithm>
#include <memory>
#include <mutex>
#include <random>
#include <set>
#include <string>
#include <thread>

#include "src/core/client_channel/backup_poller.h"
#include "src/core/client_channel/global_subchannel_pool.h"
#include "src/core/config/config_vars.h"
#include "src/core/credentials/transport/fake/fake_credentials.h"
#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/tcp_client.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/resolver/endpoint_addresses.h"
#include "src/core/resolver/fake/fake_resolver.h"
#include "src/core/service_config/service_config_impl.h"
#include "src/core/util/backoff.h"
#include "src/core/util/crash.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/cpp/server/secure_server_credentials.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/test_util/port.h"
#include "test/core/test_util/resolve_localhost_ip46.h"
#include "test/core/test_util/test_config.h"
#include "test/cpp/end2end/test_service_impl.h"
#include "test/cpp/util/credentials.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"

namespace grpc {
namespace testing {
namespace {

// Subclass of TestServiceImpl that increments a request counter for
// every call to the Echo RPC.
class MyTestServiceImpl : public TestServiceImpl {
 public:
  MyTestServiceImpl() : request_count_(0) {}

  Status Echo(ServerContext* context, const EchoRequest* request,
              EchoResponse* response) override {
    {
      grpc::internal::MutexLock lock(&mu_);
      ++request_count_;
    }
    AddClient(context->peer());
    return TestServiceImpl::Echo(context, request, response);
  }

  int request_count() {
    grpc::internal::MutexLock lock(&mu_);
    return request_count_;
  }

  void ResetCounters() {
    grpc::internal::MutexLock lock(&mu_);
    request_count_ = 0;
  }

  std::set<std::string> clients() {
    grpc::internal::MutexLock lock(&clients_mu_);
    return clients_;
  }

 private:
  void AddClient(const std::string& client) {
    grpc::internal::MutexLock lock(&clients_mu_);
    clients_.insert(client);
  }

  grpc::internal::Mutex mu_;
  int request_count_;
  grpc::internal::Mutex clients_mu_;
  std::set<std::string> clients_;
};

class ServiceConfigEnd2endTest : public ::testing::Test {
 protected:
  ServiceConfigEnd2endTest()
      : server_host_("localhost"),
        kRequestMessage_("Live long and prosper."),
        creds_(std::make_shared<FakeTransportSecurityChannelCredentials>()) {}

  static void SetUpTestSuite() {
    // Make the backup poller poll very frequently in order to pick up
    // updates from all the subchannels's FDs.
    grpc_core::ConfigVars::Overrides overrides;
    overrides.client_channel_backup_poll_interval_ms = 1;
    grpc_core::ConfigVars::SetOverrides(overrides);
  }

  void SetUp() override {
    grpc_init();
    response_generator_ =
        grpc_core::MakeRefCounted<grpc_core::FakeResolverResponseGenerator>();
  }

  void TearDown() override {
    for (size_t i = 0; i < servers_.size(); ++i) {
      servers_[i]->Shutdown();
    }
    // Explicitly destroy all the members so that we can make sure grpc_shutdown
    // has finished by the end of this function, and thus all the registered
    // LB policy factories are removed.
    stub_.reset();
    servers_.clear();
    creds_.reset();
    grpc_shutdown();
  }

  void CreateServers(size_t num_servers,
                     std::vector<int> ports = std::vector<int>()) {
    servers_.clear();
    for (size_t i = 0; i < num_servers; ++i) {
      int port = 0;
      if (ports.size() == num_servers) port = ports[i];
      servers_.emplace_back(new ServerData(port));
    }
  }

  void StartServer(size_t index) { servers_[index]->Start(server_host_); }

  void StartServers(size_t num_servers,
                    std::vector<int> ports = std::vector<int>()) {
    CreateServers(num_servers, std::move(ports));
    for (size_t i = 0; i < num_servers; ++i) {
      StartServer(i);
    }
  }

  grpc_core::Resolver::Result BuildFakeResults(const std::vector<int>& ports) {
    grpc_core::Resolver::Result result;
    result.addresses = grpc_core::EndpointAddressesList();
    for (const int& port : ports) {
      absl::StatusOr<grpc_core::URI> lb_uri =
          grpc_core::URI::Parse(grpc_core::LocalIpUri(port));
      GRPC_CHECK_OK(lb_uri);
      result.addresses->emplace_back(lb_uri->ToString(),
                                     grpc_core::ChannelArgs());
    }
    return result;
  }

  void SetNextResolutionNoServiceConfig(const std::vector<int>& ports) {
    grpc_core::ExecCtx exec_ctx;
    grpc_core::Resolver::Result result = BuildFakeResults(ports);
    response_generator_->SetResponseSynchronously(result);
  }

  void SetNextResolutionValidServiceConfig(const std::vector<int>& ports) {
    grpc_core::ExecCtx exec_ctx;
    grpc_core::Resolver::Result result = BuildFakeResults(ports);
    result.service_config =
        grpc_core::ServiceConfigImpl::Create(grpc_core::ChannelArgs(), "{}");
    ASSERT_TRUE(result.service_config.ok()) << result.service_config.status();
    response_generator_->SetResponseSynchronously(result);
  }

  void SetNextResolutionInvalidServiceConfig(const std::vector<int>& ports) {
    grpc_core::ExecCtx exec_ctx;
    grpc_core::Resolver::Result result = BuildFakeResults(ports);
    result.service_config =
        absl::InvalidArgumentError("error parsing service config");
    response_generator_->SetResponseSynchronously(result);
  }

  void SetNextResolutionWithServiceConfig(const std::vector<int>& ports,
                                          const char* svc_cfg) {
    grpc_core::ExecCtx exec_ctx;
    grpc_core::Resolver::Result result = BuildFakeResults(ports);
    result.service_config =
        grpc_core::ServiceConfigImpl::Create(grpc_core::ChannelArgs(), svc_cfg);
    response_generator_->SetResponseSynchronously(result);
  }

  std::vector<int> GetServersPorts(size_t start_index = 0) {
    std::vector<int> ports;
    for (size_t i = start_index; i < servers_.size(); ++i) {
      ports.push_back(servers_[i]->port_);
    }
    return ports;
  }

  std::unique_ptr<grpc::testing::EchoTestService::Stub> BuildStub(
      const std::shared_ptr<Channel>& channel) {
    return grpc::testing::EchoTestService::NewStub(channel);
  }

  std::shared_ptr<Channel> BuildChannel() {
    ChannelArguments args;
    args.SetPointer(GRPC_ARG_FAKE_RESOLVER_RESPONSE_GENERATOR,
                    response_generator_.get());
    return grpc::CreateCustomChannel("fake:///", creds_, args);
  }

  std::shared_ptr<Channel> BuildChannelWithDefaultServiceConfig() {
    ChannelArguments args;
    EXPECT_THAT(grpc::experimental::ValidateServiceConfigJSON(
                    ValidDefaultServiceConfig()),
                ::testing::StrEq(""));
    args.SetServiceConfigJSON(ValidDefaultServiceConfig());
    args.SetPointer(GRPC_ARG_FAKE_RESOLVER_RESPONSE_GENERATOR,
                    response_generator_.get());
    return grpc::CreateCustomChannel("fake:///", creds_, args);
  }

  std::shared_ptr<Channel> BuildChannelWithInvalidDefaultServiceConfig() {
    ChannelArguments args;
    EXPECT_THAT(grpc::experimental::ValidateServiceConfigJSON(
                    InvalidDefaultServiceConfig()),
                ::testing::HasSubstr("JSON parse error"));
    args.SetServiceConfigJSON(InvalidDefaultServiceConfig());
    args.SetPointer(GRPC_ARG_FAKE_RESOLVER_RESPONSE_GENERATOR,
                    response_generator_.get());
    return grpc::CreateCustomChannel("fake:///", creds_, args);
  }

  bool SendRpc(
      const std::unique_ptr<grpc::testing::EchoTestService::Stub>& stub,
      EchoResponse* response = nullptr, int timeout_ms = 1000,
      Status* result = nullptr, bool wait_for_ready = false) {
    const bool local_response = (response == nullptr);
    if (local_response) response = new EchoResponse;
    EchoRequest request;
    request.set_message(kRequestMessage_);
    ClientContext context;
    context.set_deadline(grpc_timeout_milliseconds_to_deadline(timeout_ms));
    if (wait_for_ready) context.set_wait_for_ready(true);
    Status status = stub->Echo(&context, request, response);
    if (result != nullptr) *result = status;
    if (local_response) delete response;
    return status.ok();
  }

  void CheckRpcSendOk(
      const std::unique_ptr<grpc::testing::EchoTestService::Stub>& stub,
      const grpc_core::DebugLocation& location, bool wait_for_ready = false) {
    EchoResponse response;
    Status status;
    const bool success =
        SendRpc(stub, &response, 2000, &status, wait_for_ready);
    ASSERT_TRUE(success) << "From " << location.file() << ":" << location.line()
                         << "\n"
                         << "Error: " << status.error_message() << " "
                         << status.error_details();
    ASSERT_EQ(response.message(), kRequestMessage_)
        << "From " << location.file() << ":" << location.line();
    if (!success) abort();
  }

  void CheckRpcSendFailure(
      const std::unique_ptr<grpc::testing::EchoTestService::Stub>& stub) {
    const bool success = SendRpc(stub);
    EXPECT_FALSE(success);
  }

  struct ServerData {
    const int port_;
    std::unique_ptr<Server> server_;
    MyTestServiceImpl service_;
    std::unique_ptr<std::thread> thread_;

    grpc::internal::Mutex mu_;
    grpc::internal::CondVar cond_;
    bool server_ready_ ABSL_GUARDED_BY(mu_) = false;
    bool started_ ABSL_GUARDED_BY(mu_) = false;

    explicit ServerData(int port = 0)
        : port_(port > 0 ? port : grpc_pick_unused_port_or_die()) {}

    void Start(const std::string& server_host) {
      LOG(INFO) << "starting server on port " << port_;
      grpc::internal::MutexLock lock(&mu_);
      started_ = true;
      thread_ = std::make_unique<std::thread>(
          std::bind(&ServerData::Serve, this, server_host));
      while (!server_ready_) {
        cond_.Wait(&mu_);
      }
      server_ready_ = false;
      LOG(INFO) << "server startup complete";
    }

    void Serve(const std::string& server_host) {
      std::ostringstream server_address;
      server_address << server_host << ":" << port_;
      ServerBuilder builder;
      std::shared_ptr<ServerCredentials> creds(new SecureServerCredentials(
          grpc_fake_transport_security_server_credentials_create()));
      builder.AddListeningPort(server_address.str(), std::move(creds));
      builder.RegisterService(&service_);
      server_ = builder.BuildAndStart();
      grpc::internal::MutexLock lock(&mu_);
      server_ready_ = true;
      cond_.Signal();
    }

    void Shutdown() {
      grpc::internal::MutexLock lock(&mu_);
      if (!started_) return;
      server_->Shutdown(grpc_timeout_milliseconds_to_deadline(0));
      thread_->join();
      started_ = false;
    }

    void SetServingStatus(const std::string& service, bool serving) {
      server_->GetHealthCheckService()->SetServingStatus(service, serving);
    }
  };

  void ResetCounters() {
    for (const auto& server : servers_) server->service_.ResetCounters();
  }

  void WaitForServer(
      const std::unique_ptr<grpc::testing::EchoTestService::Stub>& stub,
      size_t server_idx, const grpc_core::DebugLocation& location,
      bool ignore_failure = false) {
    do {
      if (ignore_failure) {
        SendRpc(stub);
      } else {
        CheckRpcSendOk(stub, location, true);
      }
    } while (servers_[server_idx]->service_.request_count() == 0);
    ResetCounters();
  }

  bool WaitForChannelNotReady(Channel* channel, int timeout_seconds = 5) {
    const gpr_timespec deadline =
        grpc_timeout_seconds_to_deadline(timeout_seconds);
    grpc_connectivity_state state;
    while ((state = channel->GetState(false /* try_to_connect */)) ==
           GRPC_CHANNEL_READY) {
      if (!channel->WaitForStateChange(state, deadline)) return false;
    }
    return true;
  }

  bool WaitForChannelReady(Channel* channel, int timeout_seconds = 5) {
    const gpr_timespec deadline =
        grpc_timeout_seconds_to_deadline(timeout_seconds);
    grpc_connectivity_state state;
    while ((state = channel->GetState(true /* try_to_connect */)) !=
           GRPC_CHANNEL_READY) {
      if (!channel->WaitForStateChange(state, deadline)) return false;
    }
    return true;
  }

  bool SeenAllServers() {
    for (const auto& server : servers_) {
      if (server->service_.request_count() == 0) return false;
    }
    return true;
  }

  // Updates \a connection_order by appending to it the index of the newly
  // connected server. Must be called after every single RPC.
  void UpdateConnectionOrder(
      const std::vector<std::unique_ptr<ServerData>>& servers,
      std::vector<int>* connection_order) {
    for (size_t i = 0; i < servers.size(); ++i) {
      if (servers[i]->service_.request_count() == 1) {
        // Was the server index known? If not, update connection_order.
        const auto it =
            std::find(connection_order->begin(), connection_order->end(), i);
        if (it == connection_order->end()) {
          connection_order->push_back(i);
          return;
        }
      }
    }
  }

  const char* ValidServiceConfigV1() { return "{\"version\": \"1\"}"; }

  const char* ValidServiceConfigV2() { return "{\"version\": \"2\"}"; }

  const char* ValidDefaultServiceConfig() {
    return "{\"version\": \"valid_default\"}";
  }

  const char* InvalidDefaultServiceConfig() {
    return "{\"version\": \"invalid_default\"";
  }

  const std::string server_host_;
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub_;
  std::vector<std::unique_ptr<ServerData>> servers_;
  grpc_core::RefCountedPtr<grpc_core::FakeResolverResponseGenerator>
      response_generator_;
  const std::string kRequestMessage_;
  std::shared_ptr<ChannelCredentials> creds_;
};

TEST_F(ServiceConfigEnd2endTest, NoServiceConfigTest) {
  StartServers(1);
  auto channel = BuildChannel();
  auto stub = BuildStub(channel);
  SetNextResolutionNoServiceConfig(GetServersPorts());
  CheckRpcSendOk(stub, DEBUG_LOCATION);
  EXPECT_STREQ("{}", channel->GetServiceConfigJSON().c_str());
}

TEST_F(ServiceConfigEnd2endTest, NoServiceConfigWithDefaultConfigTest) {
  StartServers(1);
  auto channel = BuildChannelWithDefaultServiceConfig();
  auto stub = BuildStub(channel);
  SetNextResolutionNoServiceConfig(GetServersPorts());
  CheckRpcSendOk(stub, DEBUG_LOCATION);
  EXPECT_STREQ(ValidDefaultServiceConfig(),
               channel->GetServiceConfigJSON().c_str());
}

TEST_F(ServiceConfigEnd2endTest, InvalidServiceConfigTest) {
  StartServers(1);
  auto channel = BuildChannel();
  auto stub = BuildStub(channel);
  SetNextResolutionInvalidServiceConfig(GetServersPorts());
  CheckRpcSendFailure(stub);
}

TEST_F(ServiceConfigEnd2endTest, ValidServiceConfigUpdatesTest) {
  StartServers(1);
  auto channel = BuildChannel();
  auto stub = BuildStub(channel);
  SetNextResolutionWithServiceConfig(GetServersPorts(), ValidServiceConfigV1());
  CheckRpcSendOk(stub, DEBUG_LOCATION);
  EXPECT_STREQ(ValidServiceConfigV1(), channel->GetServiceConfigJSON().c_str());
  SetNextResolutionWithServiceConfig(GetServersPorts(), ValidServiceConfigV2());
  CheckRpcSendOk(stub, DEBUG_LOCATION);
  EXPECT_STREQ(ValidServiceConfigV2(), channel->GetServiceConfigJSON().c_str());
}

TEST_F(ServiceConfigEnd2endTest,
       NoServiceConfigUpdateAfterValidServiceConfigTest) {
  StartServers(1);
  auto channel = BuildChannel();
  auto stub = BuildStub(channel);
  SetNextResolutionWithServiceConfig(GetServersPorts(), ValidServiceConfigV1());
  CheckRpcSendOk(stub, DEBUG_LOCATION);
  EXPECT_STREQ(ValidServiceConfigV1(), channel->GetServiceConfigJSON().c_str());
  SetNextResolutionNoServiceConfig(GetServersPorts());
  CheckRpcSendOk(stub, DEBUG_LOCATION);
  EXPECT_STREQ("{}", channel->GetServiceConfigJSON().c_str());
}

TEST_F(ServiceConfigEnd2endTest,
       NoServiceConfigUpdateAfterValidServiceConfigWithDefaultConfigTest) {
  StartServers(1);
  auto channel = BuildChannelWithDefaultServiceConfig();
  auto stub = BuildStub(channel);
  SetNextResolutionWithServiceConfig(GetServersPorts(), ValidServiceConfigV1());
  CheckRpcSendOk(stub, DEBUG_LOCATION);
  EXPECT_STREQ(ValidServiceConfigV1(), channel->GetServiceConfigJSON().c_str());
  SetNextResolutionNoServiceConfig(GetServersPorts());
  CheckRpcSendOk(stub, DEBUG_LOCATION);
  EXPECT_STREQ(ValidDefaultServiceConfig(),
               channel->GetServiceConfigJSON().c_str());
}

TEST_F(ServiceConfigEnd2endTest,
       InvalidServiceConfigUpdateAfterValidServiceConfigTest) {
  StartServers(1);
  auto channel = BuildChannel();
  auto stub = BuildStub(channel);
  SetNextResolutionWithServiceConfig(GetServersPorts(), ValidServiceConfigV1());
  CheckRpcSendOk(stub, DEBUG_LOCATION);
  EXPECT_STREQ(ValidServiceConfigV1(), channel->GetServiceConfigJSON().c_str());
  SetNextResolutionInvalidServiceConfig(GetServersPorts());
  CheckRpcSendOk(stub, DEBUG_LOCATION);
  EXPECT_STREQ(ValidServiceConfigV1(), channel->GetServiceConfigJSON().c_str());
}

TEST_F(ServiceConfigEnd2endTest,
       InvalidServiceConfigUpdateAfterValidServiceConfigWithDefaultConfigTest) {
  StartServers(1);
  auto channel = BuildChannelWithDefaultServiceConfig();
  auto stub = BuildStub(channel);
  SetNextResolutionWithServiceConfig(GetServersPorts(), ValidServiceConfigV1());
  CheckRpcSendOk(stub, DEBUG_LOCATION);
  EXPECT_STREQ(ValidServiceConfigV1(), channel->GetServiceConfigJSON().c_str());
  SetNextResolutionInvalidServiceConfig(GetServersPorts());
  CheckRpcSendOk(stub, DEBUG_LOCATION);
  EXPECT_STREQ(ValidServiceConfigV1(), channel->GetServiceConfigJSON().c_str());
}

TEST_F(ServiceConfigEnd2endTest,
       ValidServiceConfigAfterInvalidServiceConfigTest) {
  StartServers(1);
  auto channel = BuildChannel();
  auto stub = BuildStub(channel);
  SetNextResolutionInvalidServiceConfig(GetServersPorts());
  CheckRpcSendFailure(stub);
  SetNextResolutionValidServiceConfig(GetServersPorts());
  CheckRpcSendOk(stub, DEBUG_LOCATION);
}

TEST_F(ServiceConfigEnd2endTest, NoServiceConfigAfterInvalidServiceConfigTest) {
  StartServers(1);
  auto channel = BuildChannel();
  auto stub = BuildStub(channel);
  SetNextResolutionInvalidServiceConfig(GetServersPorts());
  CheckRpcSendFailure(stub);
  SetNextResolutionNoServiceConfig(GetServersPorts());
  CheckRpcSendOk(stub, DEBUG_LOCATION);
  EXPECT_STREQ("{}", channel->GetServiceConfigJSON().c_str());
}

TEST_F(ServiceConfigEnd2endTest,
       AnotherInvalidServiceConfigAfterInvalidServiceConfigTest) {
  StartServers(1);
  auto channel = BuildChannel();
  auto stub = BuildStub(channel);
  SetNextResolutionInvalidServiceConfig(GetServersPorts());
  CheckRpcSendFailure(stub);
  SetNextResolutionInvalidServiceConfig(GetServersPorts());
  CheckRpcSendFailure(stub);
}

TEST_F(ServiceConfigEnd2endTest, InvalidDefaultServiceConfigTest) {
  StartServers(1);
  auto channel = BuildChannelWithInvalidDefaultServiceConfig();
  auto stub = BuildStub(channel);
  // An invalid default service config results in a lame channel which fails all
  // RPCs
  CheckRpcSendFailure(stub);
}

TEST_F(ServiceConfigEnd2endTest,
       InvalidDefaultServiceConfigTestWithValidServiceConfig) {
  StartServers(1);
  auto channel = BuildChannelWithInvalidDefaultServiceConfig();
  auto stub = BuildStub(channel);
  CheckRpcSendFailure(stub);
  // An invalid default service config results in a lame channel which fails all
  // RPCs
  SetNextResolutionValidServiceConfig(GetServersPorts());
  CheckRpcSendFailure(stub);
}

TEST_F(ServiceConfigEnd2endTest,
       InvalidDefaultServiceConfigTestWithInvalidServiceConfig) {
  StartServers(1);
  auto channel = BuildChannelWithInvalidDefaultServiceConfig();
  auto stub = BuildStub(channel);
  CheckRpcSendFailure(stub);
  // An invalid default service config results in a lame channel which fails all
  // RPCs
  SetNextResolutionInvalidServiceConfig(GetServersPorts());
  CheckRpcSendFailure(stub);
}

TEST_F(ServiceConfigEnd2endTest,
       InvalidDefaultServiceConfigTestWithNoServiceConfig) {
  StartServers(1);
  auto channel = BuildChannelWithInvalidDefaultServiceConfig();
  auto stub = BuildStub(channel);
  CheckRpcSendFailure(stub);
  // An invalid default service config results in a lame channel which fails all
  // RPCs
  SetNextResolutionNoServiceConfig(GetServersPorts());
  CheckRpcSendFailure(stub);
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  const auto result = RUN_ALL_TESTS();
  return result;
}

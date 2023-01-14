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

#include <grpc/support/port_platform.h>

#include <algorithm>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <random>
#include <thread>

#include <gtest/gtest.h>

#include "absl/memory/memory.h"

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/atm.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/health_check_service_interface.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>

#include "src/core/lib/backoff/backoff.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/env.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/cpp/end2end/test_service_impl.h"
#include "test/cpp/util/test_credentials_provider.h"

#ifdef GPR_LINUX

namespace grpc {
namespace testing {
namespace {

struct TestScenario {
  TestScenario(const std::string& creds_type, const std::string& content)
      : credentials_type(creds_type), message_content(content) {}
  const std::string credentials_type;
  const std::string message_content;
};

class FlakyNetworkTest : public ::testing::TestWithParam<TestScenario> {
 protected:
  FlakyNetworkTest()
      : server_host_("grpctest"),
        interface_("lo:1"),
        ipv4_address_("10.0.0.1"),
        netmask_("/32") {}

  void InterfaceUp() {
    std::ostringstream cmd;
    // create interface_ with address ipv4_address_
    cmd << "ip addr add " << ipv4_address_ << netmask_ << " dev " << interface_;
    std::system(cmd.str().c_str());
  }

  void InterfaceDown() {
    std::ostringstream cmd;
    // remove interface_
    cmd << "ip addr del " << ipv4_address_ << netmask_ << " dev " << interface_;
    std::system(cmd.str().c_str());
  }

  void DNSUp() {
    std::ostringstream cmd;
    // Add DNS entry for server_host_ in /etc/hosts
    cmd << "echo '" << ipv4_address_ << "      " << server_host_
        << "' >> /etc/hosts";
    std::system(cmd.str().c_str());
  }

  void DNSDown() {
    std::ostringstream cmd;
    // Remove DNS entry for server_host_ from /etc/hosts
    // NOTE: we can't do this in one step with sed -i because when we are
    // running under docker, the file is mounted by docker so we can't change
    // its inode from within the container (sed -i creates a new file and
    // replaces the old file, which changes the inode)
    cmd << "sed  '/" << server_host_ << "/d' /etc/hosts > /etc/hosts.orig";
    std::system(cmd.str().c_str());

    // clear the stream
    cmd.str("");

    cmd << "cat /etc/hosts.orig > /etc/hosts";
    std::system(cmd.str().c_str());
  }

  void DropPackets() {
    std::ostringstream cmd;
    // drop packets with src IP = ipv4_address_
    cmd << "iptables -A INPUT -s " << ipv4_address_ << " -j DROP";

    std::system(cmd.str().c_str());
    // clear the stream
    cmd.str("");

    // drop packets with dst IP = ipv4_address_
    cmd << "iptables -A INPUT -d " << ipv4_address_ << " -j DROP";
  }

  void RestoreNetwork() {
    std::ostringstream cmd;
    // remove iptables rule to drop packets with src IP = ipv4_address_
    cmd << "iptables -D INPUT -s " << ipv4_address_ << " -j DROP";
    std::system(cmd.str().c_str());
    // clear the stream
    cmd.str("");
    // remove iptables rule to drop packets with dest IP = ipv4_address_
    cmd << "iptables -D INPUT -d " << ipv4_address_ << " -j DROP";
  }

  void FlakeNetwork() {
    std::ostringstream cmd;
    // Emulate a flaky network connection over interface_. Add a delay of 100ms
    // +/- 20ms, 0.1% packet loss, 1% duplicates and 0.01% corrupt packets.
    cmd << "tc qdisc replace dev " << interface_
        << " root netem delay 100ms 20ms distribution normal loss 0.1% "
           "duplicate "
           "0.1% corrupt 0.01% ";
    std::system(cmd.str().c_str());
  }

  void UnflakeNetwork() {
    // Remove simulated network flake on interface_
    std::ostringstream cmd;
    cmd << "tc qdisc del dev " << interface_ << " root netem";
    std::system(cmd.str().c_str());
  }

  void NetworkUp() {
    InterfaceUp();
    DNSUp();
  }

  void NetworkDown() {
    InterfaceDown();
    DNSDown();
  }

  void SetUp() override {
    NetworkUp();
    grpc_init();
    StartServer();
  }

  void TearDown() override {
    NetworkDown();
    StopServer();
    grpc_shutdown();
  }

  void StartServer() {
    // TODO (pjaikumar): Ideally, we should allocate the port dynamically using
    // grpc_pick_unused_port_or_die(). That doesn't work inside some docker
    // containers because port_server listens on localhost which maps to
    // ip6-looopback, but ipv6 support is not enabled by default in docker.
    port_ = SERVER_PORT;

    server_ = std::make_unique<ServerData>(port_, GetParam().credentials_type);
    server_->Start(server_host_);
  }
  void StopServer() { server_->Shutdown(); }

  std::unique_ptr<grpc::testing::EchoTestService::Stub> BuildStub(
      const std::shared_ptr<Channel>& channel) {
    return grpc::testing::EchoTestService::NewStub(channel);
  }

  std::shared_ptr<Channel> BuildChannel(
      const std::string& lb_policy_name,
      ChannelArguments args = ChannelArguments()) {
    if (!lb_policy_name.empty()) {
      args.SetLoadBalancingPolicyName(lb_policy_name);
    }  // else, default to pick first
    auto channel_creds = GetCredentialsProvider()->GetChannelCredentials(
        GetParam().credentials_type, &args);
    std::ostringstream server_address;
    server_address << server_host_ << ":" << port_;
    return CreateCustomChannel(server_address.str(), channel_creds, args);
  }

  bool SendRpc(
      const std::unique_ptr<grpc::testing::EchoTestService::Stub>& stub,
      int timeout_ms = 0, bool wait_for_ready = false) {
    auto response = std::make_unique<EchoResponse>();
    EchoRequest request;
    auto& msg = GetParam().message_content;
    request.set_message(msg);
    ClientContext context;
    if (timeout_ms > 0) {
      context.set_deadline(grpc_timeout_milliseconds_to_deadline(timeout_ms));
      // Allow an RPC to be canceled (for deadline exceeded) after it has
      // reached the server.
      request.mutable_param()->set_skip_cancelled_check(true);
    }
    // See https://github.com/grpc/grpc/blob/master/doc/wait-for-ready.md for
    // details of wait-for-ready semantics
    if (wait_for_ready) {
      context.set_wait_for_ready(true);
    }
    Status status = stub->Echo(&context, request, response.get());
    auto ok = status.ok();
    if (ok) {
      gpr_log(GPR_DEBUG, "RPC succeeded");
    } else {
      gpr_log(GPR_DEBUG, "RPC failed: %s", status.error_message().c_str());
    }
    return ok;
  }

  struct ServerData {
    int port_;
    const std::string creds_;
    std::unique_ptr<Server> server_;
    TestServiceImpl service_;
    std::unique_ptr<std::thread> thread_;
    bool server_ready_ = false;

    ServerData(int port, const std::string& creds)
        : port_(port), creds_(creds) {}

    void Start(const std::string& server_host) {
      gpr_log(GPR_INFO, "starting server on port %d", port_);
      std::mutex mu;
      std::unique_lock<std::mutex> lock(mu);
      std::condition_variable cond;
      thread_ = std::make_unique<std::thread>(
          std::bind(&ServerData::Serve, this, server_host, &mu, &cond));
      cond.wait(lock, [this] { return server_ready_; });
      server_ready_ = false;
      gpr_log(GPR_INFO, "server startup complete");
    }

    void Serve(const std::string& server_host, std::mutex* mu,
               std::condition_variable* cond) {
      std::ostringstream server_address;
      server_address << server_host << ":" << port_;
      ServerBuilder builder;
      auto server_creds =
          GetCredentialsProvider()->GetServerCredentials(creds_);
      builder.AddListeningPort(server_address.str(), server_creds);
      builder.RegisterService(&service_);
      server_ = builder.BuildAndStart();
      std::lock_guard<std::mutex> lock(*mu);
      server_ready_ = true;
      cond->notify_one();
    }

    void Shutdown() {
      server_->Shutdown(grpc_timeout_milliseconds_to_deadline(0));
      thread_->join();
    }
  };

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

 private:
  const std::string server_host_;
  const std::string interface_;
  const std::string ipv4_address_;
  const std::string netmask_;
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub_;
  std::unique_ptr<ServerData> server_;
  const int SERVER_PORT = 32750;
  int port_;
};

std::vector<TestScenario> CreateTestScenarios() {
  std::vector<TestScenario> scenarios;
  std::vector<std::string> credentials_types;
  std::vector<std::string> messages;

  credentials_types.push_back(kInsecureCredentialsType);
  auto sec_list = GetCredentialsProvider()->GetSecureCredentialsTypeList();
  for (auto sec = sec_list.begin(); sec != sec_list.end(); sec++) {
    credentials_types.push_back(*sec);
  }

  messages.push_back("🖖");
  for (size_t k = 1; k < GRPC_DEFAULT_MAX_RECV_MESSAGE_LENGTH / 1024; k *= 32) {
    std::string big_msg;
    for (size_t i = 0; i < k * 1024; ++i) {
      char c = 'a' + (i % 26);
      big_msg += c;
    }
    messages.push_back(big_msg);
  }
  for (auto cred = credentials_types.begin(); cred != credentials_types.end();
       ++cred) {
    for (auto msg = messages.begin(); msg != messages.end(); msg++) {
      scenarios.emplace_back(*cred, *msg);
    }
  }

  return scenarios;
}

INSTANTIATE_TEST_SUITE_P(FlakyNetworkTest, FlakyNetworkTest,
                         ::testing::ValuesIn(CreateTestScenarios()));

// Network interface connected to server flaps
TEST_P(FlakyNetworkTest, NetworkTransition) {
  const int kKeepAliveTimeMs = 1000;
  const int kKeepAliveTimeoutMs = 1000;
  ChannelArguments args;
  args.SetInt(GRPC_ARG_KEEPALIVE_TIME_MS, kKeepAliveTimeMs);
  args.SetInt(GRPC_ARG_KEEPALIVE_TIMEOUT_MS, kKeepAliveTimeoutMs);
  args.SetInt(GRPC_ARG_KEEPALIVE_PERMIT_WITHOUT_CALLS, 1);
  args.SetInt(GRPC_ARG_HTTP2_MAX_PINGS_WITHOUT_DATA, 0);

  auto channel = BuildChannel("pick_first", args);
  auto stub = BuildStub(channel);
  // Channel should be in READY state after we send an RPC
  EXPECT_TRUE(SendRpc(stub));
  EXPECT_EQ(channel->GetState(false), GRPC_CHANNEL_READY);

  std::atomic_bool shutdown{false};
  std::thread sender = std::thread([this, &stub, &shutdown]() {
    while (true) {
      if (shutdown.load()) {
        return;
      }
      SendRpc(stub);
      std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
  });

  // bring down network
  NetworkDown();
  EXPECT_TRUE(WaitForChannelNotReady(channel.get()));
  // bring network interface back up
  InterfaceUp();
  std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  // Restore DNS entry for server
  DNSUp();
  EXPECT_TRUE(WaitForChannelReady(channel.get()));
  EXPECT_EQ(channel->GetState(false), GRPC_CHANNEL_READY);
  shutdown.store(true);
  sender.join();
}

// Traffic to server server is blackholed temporarily with keepalives enabled
TEST_P(FlakyNetworkTest, ServerUnreachableWithKeepalive) {
  const int kKeepAliveTimeMs = 1000;
  const int kKeepAliveTimeoutMs = 1000;
  const int kReconnectBackoffMs = 1000;
  ChannelArguments args;
  args.SetInt(GRPC_ARG_KEEPALIVE_TIME_MS, kKeepAliveTimeMs);
  args.SetInt(GRPC_ARG_KEEPALIVE_TIMEOUT_MS, kKeepAliveTimeoutMs);
  args.SetInt(GRPC_ARG_KEEPALIVE_PERMIT_WITHOUT_CALLS, 1);
  args.SetInt(GRPC_ARG_HTTP2_MAX_PINGS_WITHOUT_DATA, 0);
  // max time for a connection attempt
  args.SetInt(GRPC_ARG_MIN_RECONNECT_BACKOFF_MS, kReconnectBackoffMs);
  // max time between reconnect attempts
  args.SetInt(GRPC_ARG_MAX_RECONNECT_BACKOFF_MS, kReconnectBackoffMs);

  gpr_log(GPR_DEBUG, "FlakyNetworkTest.ServerUnreachableWithKeepalive start");
  auto channel = BuildChannel("pick_first", args);
  auto stub = BuildStub(channel);
  // Channel should be in READY state after we send an RPC
  EXPECT_TRUE(SendRpc(stub));
  EXPECT_EQ(channel->GetState(false), GRPC_CHANNEL_READY);

  std::atomic_bool shutdown{false};
  std::thread sender = std::thread([this, &stub, &shutdown]() {
    while (true) {
      if (shutdown.load()) {
        return;
      }
      SendRpc(stub);
      std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
  });

  // break network connectivity
  gpr_log(GPR_DEBUG, "Adding iptables rule to drop packets");
  DropPackets();
  std::this_thread::sleep_for(std::chrono::milliseconds(10000));
  EXPECT_TRUE(WaitForChannelNotReady(channel.get()));
  // bring network interface back up
  RestoreNetwork();
  gpr_log(GPR_DEBUG, "Removed iptables rule to drop packets");
  EXPECT_TRUE(WaitForChannelReady(channel.get()));
  EXPECT_EQ(channel->GetState(false), GRPC_CHANNEL_READY);
  shutdown.store(true);
  sender.join();
  gpr_log(GPR_DEBUG, "FlakyNetworkTest.ServerUnreachableWithKeepalive end");
}

//
// Traffic to server server is blackholed temporarily with keepalives disabled
TEST_P(FlakyNetworkTest, ServerUnreachableNoKeepalive) {
  auto channel = BuildChannel("pick_first", ChannelArguments());
  auto stub = BuildStub(channel);
  // Channel should be in READY state after we send an RPC
  EXPECT_TRUE(SendRpc(stub));
  EXPECT_EQ(channel->GetState(false), GRPC_CHANNEL_READY);

  // break network connectivity
  DropPackets();

  std::thread sender = std::thread([this, &stub]() {
    // RPC with deadline should timeout
    EXPECT_FALSE(SendRpc(stub, /*timeout_ms=*/500, /*wait_for_ready=*/true));
    // RPC without deadline forever until call finishes
    EXPECT_TRUE(SendRpc(stub, /*timeout_ms=*/0, /*wait_for_ready=*/true));
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(2000));
  // bring network interface back up
  RestoreNetwork();

  // wait for RPC to finish
  sender.join();
}

// Send RPCs over a flaky network connection
TEST_P(FlakyNetworkTest, FlakyNetwork) {
  const int kKeepAliveTimeMs = 1000;
  const int kKeepAliveTimeoutMs = 1000;
  const int kMessageCount = 100;
  ChannelArguments args;
  args.SetInt(GRPC_ARG_KEEPALIVE_TIME_MS, kKeepAliveTimeMs);
  args.SetInt(GRPC_ARG_KEEPALIVE_TIMEOUT_MS, kKeepAliveTimeoutMs);
  args.SetInt(GRPC_ARG_KEEPALIVE_PERMIT_WITHOUT_CALLS, 1);
  args.SetInt(GRPC_ARG_HTTP2_MAX_PINGS_WITHOUT_DATA, 0);

  auto channel = BuildChannel("pick_first", args);
  auto stub = BuildStub(channel);
  // Channel should be in READY state after we send an RPC
  EXPECT_TRUE(SendRpc(stub));
  EXPECT_EQ(channel->GetState(false), GRPC_CHANNEL_READY);

  // simulate flaky network (packet loss, corruption and delays)
  FlakeNetwork();
  for (int i = 0; i < kMessageCount; ++i) {
    SendRpc(stub);
  }
  // remove network flakiness
  UnflakeNetwork();
  EXPECT_EQ(channel->GetState(false), GRPC_CHANNEL_READY);
}

// Server is shutdown gracefully and restarted. Client keepalives are enabled
TEST_P(FlakyNetworkTest, ServerRestartKeepaliveEnabled) {
  const int kKeepAliveTimeMs = 1000;
  const int kKeepAliveTimeoutMs = 1000;
  ChannelArguments args;
  args.SetInt(GRPC_ARG_KEEPALIVE_TIME_MS, kKeepAliveTimeMs);
  args.SetInt(GRPC_ARG_KEEPALIVE_TIMEOUT_MS, kKeepAliveTimeoutMs);
  args.SetInt(GRPC_ARG_KEEPALIVE_PERMIT_WITHOUT_CALLS, 1);
  args.SetInt(GRPC_ARG_HTTP2_MAX_PINGS_WITHOUT_DATA, 0);

  auto channel = BuildChannel("pick_first", args);
  auto stub = BuildStub(channel);
  // Channel should be in READY state after we send an RPC
  EXPECT_TRUE(SendRpc(stub));
  EXPECT_EQ(channel->GetState(false), GRPC_CHANNEL_READY);

  // server goes down, client should detect server going down and calls should
  // fail
  StopServer();
  EXPECT_TRUE(WaitForChannelNotReady(channel.get()));
  EXPECT_FALSE(SendRpc(stub));

  std::this_thread::sleep_for(std::chrono::milliseconds(1000));

  // server restarts, calls succeed
  StartServer();
  EXPECT_TRUE(WaitForChannelReady(channel.get()));
  // EXPECT_TRUE(SendRpc(stub));
}

// Server is shutdown gracefully and restarted. Client keepalives are enabled
TEST_P(FlakyNetworkTest, ServerRestartKeepaliveDisabled) {
  auto channel = BuildChannel("pick_first", ChannelArguments());
  auto stub = BuildStub(channel);
  // Channel should be in READY state after we send an RPC
  EXPECT_TRUE(SendRpc(stub));
  EXPECT_EQ(channel->GetState(false), GRPC_CHANNEL_READY);

  // server sends GOAWAY when it's shutdown, so client attempts to reconnect
  StopServer();
  std::this_thread::sleep_for(std::chrono::milliseconds(1000));

  EXPECT_TRUE(WaitForChannelNotReady(channel.get()));

  std::this_thread::sleep_for(std::chrono::milliseconds(1000));

  // server restarts, calls succeed
  StartServer();
  EXPECT_TRUE(WaitForChannelReady(channel.get()));
}

}  // namespace
}  // namespace testing
}  // namespace grpc
#endif  // GPR_LINUX

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  auto result = RUN_ALL_TESTS();
  return result;
}

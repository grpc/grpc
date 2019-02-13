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

#include <algorithm>
#include <memory>
#include <mutex>
#include <random>
#include <thread>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/atm.h>
#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/health_check_service_interface.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>

#include "src/core/lib/backoff/backoff.h"
#include "src/core/lib/gpr/env.h"

#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/cpp/end2end/test_service_impl.h"

#include <gtest/gtest.h>

#ifdef GPR_LINUX
using grpc::testing::EchoRequest;
using grpc::testing::EchoResponse;

namespace grpc {
namespace testing {
namespace {

class FlakyNetworkTest : public ::testing::Test {
 protected:
  FlakyNetworkTest()
      : server_host_("grpctest"),
        interface_("lo:1"),
        ipv4_address_("10.0.0.1"),
        netmask_("/32"),
        kRequestMessage_("🖖") {}

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
    // +/- 590ms, 3% packet loss, 1% duplicates and 0.1% corrupt packets.
    cmd << "tc qdisc replace dev " << interface_
        << " root netem delay 100ms 50ms distribution normal loss 3% duplicate "
           "1% corrupt 0.1% ";
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

    server_.reset(new ServerData(port_));
    server_->Start(server_host_);
  }
  void StopServer() { server_->Shutdown(); }

  std::unique_ptr<grpc::testing::EchoTestService::Stub> BuildStub(
      const std::shared_ptr<Channel>& channel) {
    return grpc::testing::EchoTestService::NewStub(channel);
  }

  std::shared_ptr<Channel> BuildChannel(
      const grpc::string& lb_policy_name,
      ChannelArguments args = ChannelArguments()) {
    if (lb_policy_name.size() > 0) {
      args.SetLoadBalancingPolicyName(lb_policy_name);
    }  // else, default to pick first
    std::ostringstream server_address;
    server_address << server_host_ << ":" << port_;
    return CreateCustomChannel(server_address.str(),
                               InsecureChannelCredentials(), args);
  }

  bool SendRpc(
      const std::unique_ptr<grpc::testing::EchoTestService::Stub>& stub,
      int timeout_ms = 0, bool wait_for_ready = false) {
    auto response = std::unique_ptr<EchoResponse>(new EchoResponse());
    EchoRequest request;
    request.set_message(kRequestMessage_);
    ClientContext context;
    if (timeout_ms > 0) {
      context.set_deadline(grpc_timeout_milliseconds_to_deadline(timeout_ms));
    }
    // See https://github.com/grpc/grpc/blob/master/doc/wait-for-ready.md for
    // details of wait-for-ready semantics
    if (wait_for_ready) {
      context.set_wait_for_ready(true);
    }
    Status status = stub->Echo(&context, request, response.get());
    auto ok = status.ok();
    if (ok) {
      gpr_log(GPR_DEBUG, "RPC returned %s\n", response->message().c_str());
    } else {
      gpr_log(GPR_DEBUG, "RPC failed: %s", status.error_message().c_str());
    }
    return ok;
  }

  struct ServerData {
    int port_;
    std::unique_ptr<Server> server_;
    TestServiceImpl service_;
    std::unique_ptr<std::thread> thread_;
    bool server_ready_ = false;

    explicit ServerData(int port) { port_ = port; }

    void Start(const grpc::string& server_host) {
      gpr_log(GPR_INFO, "starting server on port %d", port_);
      std::mutex mu;
      std::unique_lock<std::mutex> lock(mu);
      std::condition_variable cond;
      thread_.reset(new std::thread(
          std::bind(&ServerData::Serve, this, server_host, &mu, &cond)));
      cond.wait(lock, [this] { return server_ready_; });
      server_ready_ = false;
      gpr_log(GPR_INFO, "server startup complete");
    }

    void Serve(const grpc::string& server_host, std::mutex* mu,
               std::condition_variable* cond) {
      std::ostringstream server_address;
      server_address << server_host << ":" << port_;
      ServerBuilder builder;
      builder.AddListeningPort(server_address.str(),
                               InsecureServerCredentials());
      builder.RegisterService(&service_);
      server_ = builder.BuildAndStart();
      std::lock_guard<std::mutex> lock(*mu);
      server_ready_ = true;
      cond->notify_one();
    }

    void Shutdown(bool join = true) {
      server_->Shutdown(grpc_timeout_milliseconds_to_deadline(0));
      if (join) thread_->join();
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
  const grpc::string server_host_;
  const grpc::string interface_;
  const grpc::string ipv4_address_;
  const grpc::string netmask_;
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub_;
  std::unique_ptr<ServerData> server_;
  const int SERVER_PORT = 32750;
  int port_;
  const grpc::string kRequestMessage_;
};

// Network interface connected to server flaps
TEST_F(FlakyNetworkTest, NetworkTransition) {
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
TEST_F(FlakyNetworkTest, ServerUnreachableWithKeepalive) {
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

  // break network connectivity
  DropPackets();
  std::this_thread::sleep_for(std::chrono::milliseconds(10000));
  EXPECT_TRUE(WaitForChannelNotReady(channel.get()));
  // bring network interface back up
  RestoreNetwork();
  EXPECT_TRUE(WaitForChannelReady(channel.get()));
  EXPECT_EQ(channel->GetState(false), GRPC_CHANNEL_READY);
  shutdown.store(true);
  sender.join();
}

//
// Traffic to server server is blackholed temporarily with keepalives disabled
TEST_F(FlakyNetworkTest, ServerUnreachableNoKeepalive) {
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
TEST_F(FlakyNetworkTest, FlakyNetwork) {
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
    EXPECT_TRUE(SendRpc(stub));
  }
  // remove network flakiness
  UnflakeNetwork();
  EXPECT_EQ(channel->GetState(false), GRPC_CHANNEL_READY);
}

}  // namespace
}  // namespace testing
}  // namespace grpc
#endif  // GPR_LINUX

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc_test_init(argc, argv);
  auto result = RUN_ALL_TESTS();
  return result;
}

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

#include <atomic>
#include <memory>
#include <mutex>
#include <random>
#include <sstream>
#include <thread>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>

#include "src/core/ext/filters/client_channel/resolver/fake/fake_resolver.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/thd.h"
#include "src/core/lib/iomgr/sockaddr.h"

#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/cpp/end2end/test_service_impl.h"

#include "src/proto/grpc/lb/v1/load_balancer.grpc.pb.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"

using grpc::lb::v1::LoadBalanceRequest;
using grpc::lb::v1::LoadBalanceResponse;
using grpc::lb::v1::LoadBalancer;

namespace grpc {
namespace testing {
namespace {

const size_t kNumBackends = 10;
const size_t kNumBalancers = 5;
const size_t kNumClientThreads = 100;
const int kResolutionUpdateIntervalMs = 50;
const int kServerlistUpdateIntervalMs = 10;
const int kTestDurationSec = 30;

using BackendServiceImpl = TestServiceImpl;

class BalancerServiceImpl : public LoadBalancer::Service {
 public:
  using Stream = ServerReaderWriter<LoadBalanceResponse, LoadBalanceRequest>;

  explicit BalancerServiceImpl(const std::vector<int>& all_backend_ports)
      : all_backend_ports_(all_backend_ports) {}

  Status BalanceLoad(ServerContext* context, Stream* stream) override {
    gpr_log(GPR_INFO, "LB[%p]: Start BalanceLoad.", this);
    LoadBalanceRequest request;
    stream->Read(&request);
    while (!shutdown_) {
      stream->Write(BuildRandomResponseForBackends());
      std::this_thread::sleep_for(
          std::chrono::milliseconds(kServerlistUpdateIntervalMs));
    }
    gpr_log(GPR_INFO, "LB[%p]: Finish BalanceLoad.", this);
    return Status::OK;
  }

  void Shutdown() { shutdown_ = true; }

 private:
  grpc::string Ip4ToPackedString(const char* ip_str) {
    struct in_addr ip4;
    GPR_ASSERT(inet_pton(AF_INET, ip_str, &ip4) == 1);
    return grpc::string(reinterpret_cast<const char*>(&ip4), sizeof(ip4));
  }

  LoadBalanceResponse BuildRandomResponseForBackends() {
    // Generate a random serverlist with varying size (if N =
    // all_backend_ports_.size(), num_non_drop_entry is in [0, 2N],
    // num_drop_entry is in [0, N]), order, duplicate, and drop rate.
    size_t num_non_drop_entry =
        std::rand() % (all_backend_ports_.size() * 2 + 1);
    size_t num_drop_entry = std::rand() % (all_backend_ports_.size() + 1);
    std::vector<int> random_backend_indices;
    for (size_t i = 0; i < num_non_drop_entry; ++i) {
      random_backend_indices.push_back(std::rand() % all_backend_ports_.size());
    }
    for (size_t i = 0; i < num_drop_entry; ++i) {
      random_backend_indices.push_back(-1);
    }
    std::shuffle(random_backend_indices.begin(), random_backend_indices.end(),
                 std::mt19937(std::random_device()()));
    // Build the response according to the random list generated above.
    LoadBalanceResponse response;
    for (int index : random_backend_indices) {
      auto* server = response.mutable_server_list()->add_servers();
      if (index < 0) {
        server->set_drop(true);
        server->set_load_balance_token("load_balancing");
      } else {
        server->set_ip_address(Ip4ToPackedString("127.0.0.1"));
        server->set_port(all_backend_ports_[index]);
      }
    }
    return response;
  }

  std::atomic_bool shutdown_{false};
  const std::vector<int> all_backend_ports_;
};

class ClientChannelStressTest {
 public:
  void Run() {
    Start();
    // Keep updating resolution for the test duration.
    gpr_log(GPR_INFO, "Start updating resolution.");
    const auto wait_duration =
        std::chrono::milliseconds(kResolutionUpdateIntervalMs);
    std::vector<AddressData> addresses;
    auto start_time = std::chrono::steady_clock::now();
    while (true) {
      if (std::chrono::duration_cast<std::chrono::seconds>(
              std::chrono::steady_clock::now() - start_time)
              .count() > kTestDurationSec) {
        break;
      }
      // Generate a random subset of balancers.
      addresses.clear();
      for (const auto& balancer_server : balancer_servers_) {
        // Select each address with probability of 0.8.
        if (std::rand() % 10 < 8) {
          addresses.emplace_back(AddressData{balancer_server.port_, true, ""});
        }
      }
      std::shuffle(addresses.begin(), addresses.end(),
                   std::mt19937(std::random_device()()));
      SetNextResolution(addresses);
      std::this_thread::sleep_for(wait_duration);
    }
    gpr_log(GPR_INFO, "Finish updating resolution.");
    Shutdown();
  }

 private:
  template <typename T>
  struct ServerThread {
    explicit ServerThread(const grpc::string& type,
                          const grpc::string& server_host, T* service)
        : type_(type), service_(service) {
      std::mutex mu;
      // We need to acquire the lock here in order to prevent the notify_one
      // by ServerThread::Start from firing before the wait below is hit.
      std::unique_lock<std::mutex> lock(mu);
      port_ = grpc_pick_unused_port_or_die();
      gpr_log(GPR_INFO, "starting %s server on port %d", type_.c_str(), port_);
      std::condition_variable cond;
      thread_.reset(new std::thread(
          std::bind(&ServerThread::Start, this, server_host, &mu, &cond)));
      cond.wait(lock);
      gpr_log(GPR_INFO, "%s server startup complete", type_.c_str());
    }

    void Start(const grpc::string& server_host, std::mutex* mu,
               std::condition_variable* cond) {
      // We need to acquire the lock here in order to prevent the notify_one
      // below from firing before its corresponding wait is executed.
      std::lock_guard<std::mutex> lock(*mu);
      std::ostringstream server_address;
      server_address << server_host << ":" << port_;
      ServerBuilder builder;
      builder.AddListeningPort(server_address.str(),
                               InsecureServerCredentials());
      builder.RegisterService(service_);
      server_ = builder.BuildAndStart();
      cond->notify_one();
    }

    void Shutdown() {
      gpr_log(GPR_INFO, "%s about to shutdown", type_.c_str());
      server_->Shutdown(grpc_timeout_milliseconds_to_deadline(0));
      thread_->join();
      gpr_log(GPR_INFO, "%s shutdown completed", type_.c_str());
    }

    int port_;
    grpc::string type_;
    std::unique_ptr<Server> server_;
    T* service_;
    std::unique_ptr<std::thread> thread_;
  };

  struct AddressData {
    int port;
    bool is_balancer;
    grpc::string balancer_name;
  };

  void SetNextResolution(const std::vector<AddressData>& address_data) {
    grpc_core::ExecCtx exec_ctx;
    grpc_lb_addresses* addresses =
        grpc_lb_addresses_create(address_data.size(), nullptr);
    for (size_t i = 0; i < address_data.size(); ++i) {
      char* lb_uri_str;
      gpr_asprintf(&lb_uri_str, "ipv4:127.0.0.1:%d", address_data[i].port);
      grpc_uri* lb_uri = grpc_uri_parse(lb_uri_str, true);
      GPR_ASSERT(lb_uri != nullptr);
      grpc_lb_addresses_set_address_from_uri(
          addresses, i, lb_uri, address_data[i].is_balancer,
          address_data[i].balancer_name.c_str(), nullptr);
      grpc_uri_destroy(lb_uri);
      gpr_free(lb_uri_str);
    }
    grpc_arg fake_addresses = grpc_lb_addresses_create_channel_arg(addresses);
    grpc_channel_args fake_result = {1, &fake_addresses};
    response_generator_->SetResponse(&fake_result);
    grpc_lb_addresses_destroy(addresses);
  }

  void KeepSendingRequests() {
    gpr_log(GPR_INFO, "Start sending requests.");
    while (!shutdown_) {
      ClientContext context;
      context.set_deadline(grpc_timeout_milliseconds_to_deadline(1000));
      EchoRequest request;
      request.set_message("test");
      EchoResponse response;
      {
        std::lock_guard<std::mutex> lock(stub_mutex_);
        stub_->Echo(&context, request, &response);
      }
    }
    gpr_log(GPR_INFO, "Finish sending requests.");
  }

  void CreateStub() {
    ChannelArguments args;
    response_generator_ =
        grpc_core::MakeRefCounted<grpc_core::FakeResolverResponseGenerator>();
    args.SetPointer(GRPC_ARG_FAKE_RESOLVER_RESPONSE_GENERATOR,
                    response_generator_.get());
    std::ostringstream uri;
    uri << "fake:///servername_not_used";
    channel_ =
        CreateCustomChannel(uri.str(), InsecureChannelCredentials(), args);
    stub_ = grpc::testing::EchoTestService::NewStub(channel_);
  }

  void Start() {
    // Start the backends.
    std::vector<int> backend_ports;
    for (size_t i = 0; i < kNumBackends; ++i) {
      backends_.emplace_back(new BackendServiceImpl());
      backend_servers_.emplace_back(ServerThread<BackendServiceImpl>(
          "backend", server_host_, backends_.back().get()));
      backend_ports.push_back(backend_servers_.back().port_);
    }
    // Start the load balancers.
    for (size_t i = 0; i < kNumBalancers; ++i) {
      balancers_.emplace_back(new BalancerServiceImpl(backend_ports));
      balancer_servers_.emplace_back(ServerThread<BalancerServiceImpl>(
          "balancer", server_host_, balancers_.back().get()));
    }
    // Start sending RPCs in multiple threads.
    CreateStub();
    for (size_t i = 0; i < kNumClientThreads; ++i) {
      client_threads_.emplace_back(
          std::thread(&ClientChannelStressTest::KeepSendingRequests, this));
    }
  }

  void Shutdown() {
    shutdown_ = true;
    for (size_t i = 0; i < client_threads_.size(); ++i) {
      client_threads_[i].join();
    }
    for (size_t i = 0; i < balancers_.size(); ++i) {
      balancers_[i]->Shutdown();
      balancer_servers_[i].Shutdown();
    }
    for (size_t i = 0; i < backends_.size(); ++i) {
      backend_servers_[i].Shutdown();
    }
  }

  std::atomic_bool shutdown_{false};
  const grpc::string server_host_ = "localhost";
  std::shared_ptr<Channel> channel_;
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub_;
  std::mutex stub_mutex_;
  std::vector<std::unique_ptr<BackendServiceImpl>> backends_;
  std::vector<std::unique_ptr<BalancerServiceImpl>> balancers_;
  std::vector<ServerThread<BackendServiceImpl>> backend_servers_;
  std::vector<ServerThread<BalancerServiceImpl>> balancer_servers_;
  grpc_core::RefCountedPtr<grpc_core::FakeResolverResponseGenerator>
      response_generator_;
  std::vector<std::thread> client_threads_;
};

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc_init();
  grpc_test_init(argc, argv);
  grpc::testing::ClientChannelStressTest test;
  test.Run();
  grpc_shutdown();
  return 0;
}

#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <grpc++/channel.h>
#include <grpc++/completion_queue.h>
#include <grpc++/create_channel.h>
#include <grpc++/security/server_credentials.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc++/server_context.h>
#include <grpc/grpc.h>
#include <grpc/support/log.h>

#include "src/proto/grpc/testing/helloworld.grpc.pb.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/cpp/util/subprocess.h"

#include <gtest/gtest.h>

using grpc::Channel;
using grpc::ClientAsyncResponseReader;
using grpc::ClientContext;
using grpc::CompletionQueue;
using grpc::Server;
using grpc::ServerAsyncResponseWriter;
using grpc::ServerBuilder;
using grpc::ServerCompletionQueue;
using grpc::ServerContext;
using grpc::Status;
using helloworld::Greeter;
using helloworld::HelloReply;
using helloworld::HelloRequest;

static std::string g_root;

static const grpc::string kServerProgramName = "parallel_async_test_server";
static const int kNumRequestsPerThread = 10000;
static const int kNumSendThreads = 1;
static const int kNumReceiveThreads = 10;

class GreeterClient {
 public:
  explicit GreeterClient(std::shared_ptr<Channel> channel)
      : stub_(Greeter::NewStub(channel)) {
    for (int i = 0; i < kNumReceiveThreads; ++i) {
      response_threads_.emplace_back(&GreeterClient::HandleResponses, this);
    }
  }

  ~GreeterClient() {}

  void Shutdown() {
    cq_.Shutdown();
    for (auto& thread : response_threads_) thread.join();
  }

  struct Call {
    HelloReply reply;
    ClientContext context;
    Status status;
    std::unique_ptr<ClientAsyncResponseReader<HelloReply> > rpc;
  };

  void SayHello(const grpc::string& user) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      active_calls_++;
    }

    Call* call = new Call();
    HelloRequest request;
    request.set_name(user);
    gpr_log(GPR_INFO, "Send : %s", user.c_str());
    call->rpc = stub_->AsyncSayHello(&call->context, request, &cq_);
    call->rpc->Finish(&call->reply, &call->status, (void*)call);
  }

  void Wait() {
    std::unique_lock<std::mutex> lock(mutex_);
    auto no_active = [this]() { return !active_calls_; };
    while (!cond_.wait_until(
        lock, std::chrono::system_clock::now() + std::chrono::seconds(1),
        no_active))
      ;
  }

 private:
  void HandleResponses() {
    while (true) {
      void* got_tag;
      bool ok = false;
      if (!cq_.Next(&got_tag, &ok)) break;
      Call* call = static_cast<Call*>(got_tag);
      GPR_ASSERT(ok);
      if (call->status.ok()) {
        gpr_log(GPR_DEBUG, "Received: %s", call->reply.message().c_str());
      } else {
        gpr_log(GPR_DEBUG, "Error: %s", call->status.error_message().c_str());
      }
      delete call;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        active_calls_--;
        if (active_calls_ == 0) {
          cond_.notify_one();
        }
      }
    }
  }

  std::mutex mutex_;
  std::condition_variable cond_;
  int active_calls_ = 0;
  std::unique_ptr<Greeter::Stub> stub_;
  CompletionQueue cq_;
  std::vector<std::thread> response_threads_;
};

class ServerProcess {
 public:
  void Start(const grpc::string& addr) {
    gpr_log(GPR_INFO, "Starting server at address: %s", addr.c_str());
    server_.reset(new grpc::SubProcess({
        g_root + "/" + kServerProgramName, "--address=" + addr,
    }));

    GPR_ASSERT(server_);
  }

  void Kill() { server_.reset(); }

 private:
  std::unique_ptr<grpc::SubProcess> server_;
};

TEST(ParallelAsync, Test) {
  ServerProcess server;

  auto port = grpc_pick_unused_port_or_die();
  std::ostringstream addr_stream;
  addr_stream << "localhost:" << port;
  auto addr = addr_stream.str();

  server.Start(addr);

  GreeterClient greeter(
      grpc::CreateChannel(addr, grpc::InsecureChannelCredentials()));

  std::vector<std::thread> send_threads;
  for (int i = 0; i < kNumSendThreads; ++i) {
    send_threads.emplace_back([&greeter]() {
      for (int j = 0; j < kNumRequestsPerThread; ++j)
        greeter.SayHello(std::to_string(j));
    });
  }
  for (auto& t : send_threads) t.join();

  greeter.Wait();
  greeter.Shutdown();
  server.Kill();
}

int main(int argc, char** argv) {
  std::string me = argv[0];
  auto lslash = me.rfind('/');
  if (lslash != std::string::npos) {
    g_root = me.substr(0, lslash);
  } else {
    g_root = ".";
  }

  grpc_test_init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);

  for (int i = 0; i < 10; i++) {
    if (RUN_ALL_TESTS() != 0) {
      return 1;
    }
  }
  return 0;
}

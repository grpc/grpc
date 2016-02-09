#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <mutex>

#include <grpc++/channel.h>
#include <grpc++/completion_queue.h>
#include <grpc++/create_channel.h>
#include <grpc++/security/server_credentials.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc++/server_context.h>
#include <grpc/grpc.h>
#include <grpc/support/log.h>

#include "src/proto/helloworld/helloworld.grpc.pb.h"
#include "test/core/util/test_config.h"

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

class GreeterClient {
 public:
  explicit GreeterClient(std::shared_ptr<Channel> channel)
      : stub_(Greeter::NewStub(channel)) {
    for (int i = 0; i < 10; ++i)
      response_threads_.emplace_back(&GreeterClient::HandleResponses, this);
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
    call->rpc = stub_->AsyncSayHello(&call->context, request, &cq_);
    call->rpc->Finish(&call->reply, &call->status, (void*)call);
  }

  void Wait() {
    std::unique_lock<std::mutex> lock(mutex_);
    auto no_active = [this]() { return !active_calls_; };
    while (!cond_.wait_until(lock, std::chrono::system_clock::now() + std::chrono::seconds(1), no_active));
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
        gpr_log(GPR_DEBUG, "Greeter received: %s", call->reply.message().c_str());
      } else {
        gpr_log(GPR_DEBUG, "Greeter error: %s", call->status.error_message().c_str());
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

class GreeterServiceImpl final : public Greeter::Service {
  Status SayHello(ServerContext* context, const HelloRequest* request,
                  HelloReply* reply) override {
    grpc::string prefix("Hello ");
    reply->set_message(prefix + request->name());
    return Status::OK;
  }
};

TEST(ParallelAsync, Test) {
  grpc::string address = "unix:/tmp/test";

  GreeterServiceImpl service;

  ServerBuilder builder;
  builder.AddListeningPort(address, ::grpc::InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<Server> server(builder.BuildAndStart());
  gpr_log(GPR_INFO, "Server listening on %s", address.c_str());

  GreeterClient greeter(
      grpc::CreateChannel(address, grpc::InsecureChannelCredentials()));

  std::vector<std::thread> send_threads;
  for (int i = 0; i < 10; ++i) {
    send_threads.emplace_back([&greeter]() {
      for (int j = 0; j < 1000; ++j) greeter.SayHello(std::to_string(j));
    });
  }
  for (auto& t : send_threads) t.join();

  greeter.Wait();
  greeter.Shutdown();
  server->Shutdown();
}

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

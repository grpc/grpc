/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <forward_list>
#include <functional>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/signal.h>
#include <thread>

#include <gflags/gflags.h>
#include <grpc/support/alloc.h>
#include <grpc/support/host_port.h>
#include <grpc++/async_unary_call.h>
#include <grpc++/config.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc++/server_context.h>
#include <grpc++/status.h>
#include <gtest/gtest.h>
#include "src/cpp/server/thread_pool.h"
#include "test/core/util/grpc_profiler.h"
#include "test/cpp/qps/qpstest.pb.h"

#include <grpc/grpc.h>
#include <grpc/support/log.h>

DEFINE_bool(enable_ssl, false, "Whether to use ssl/tls.");
DEFINE_int32(port, 0, "Server port.");
DEFINE_int32(server_threads, 4, "Number of server threads.");

using grpc::CompletionQueue;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ThreadPool;
using grpc::testing::Payload;
using grpc::testing::PayloadType;
using grpc::testing::ServerStats;
using grpc::testing::SimpleRequest;
using grpc::testing::SimpleResponse;
using grpc::testing::StatsRequest;
using grpc::testing::TestService;
using grpc::Status;

// In some distros, gflags is in the namespace google, and in some others,
// in gflags. This hack is enabling us to find both.
namespace google {}
namespace gflags {}
using namespace google;
using namespace gflags;

static bool got_sigint = false;

static void sigint_handler(int x) { got_sigint = 1; }

static double time_double(struct timeval *tv) {
  return tv->tv_sec + 1e-6 * tv->tv_usec;
}

static bool SetPayload(PayloadType type, int size, Payload *payload) {
  PayloadType response_type = type;
  // TODO(yangg): Support UNCOMPRESSABLE payload.
  if (type != PayloadType::COMPRESSABLE) {
    return false;
  }
  payload->set_type(response_type);
  std::unique_ptr<char[]> body(new char[size]());
  payload->set_body(body.get(), size);
  return true;
}

namespace {

class AsyncQpsServerTest {
 public:
  AsyncQpsServerTest() : srv_cq_(), async_service_(&srv_cq_), server_(nullptr) {
    char *server_address = NULL;
    gpr_join_host_port(&server_address, "::", FLAGS_port);

    ServerBuilder builder;
    builder.AddPort(server_address);

    builder.RegisterAsyncService(&async_service_);

    server_ = builder.BuildAndStart();
    gpr_log(GPR_INFO, "Server listening on %s\n", server_address);
    gpr_free(server_address);

    using namespace std::placeholders;
    request_unary_ = std::bind(&TestService::AsyncService::RequestUnaryCall,
                               &async_service_, _1, _2, _3, &srv_cq_, _4);
    request_stats_ =
        std::bind(&TestService::AsyncService::RequestCollectServerStats,
                  &async_service_, _1, _2, _3, &srv_cq_, _4);
    for (int i = 0; i < 100; i++) {
      contexts_.push_front(
          new ServerRpcContextUnaryImpl<SimpleRequest, SimpleResponse>(
              request_unary_, UnaryCall));
      contexts_.push_front(
          new ServerRpcContextUnaryImpl<StatsRequest, ServerStats>(
              request_stats_, CollectServerStats));
    }
  }
  ~AsyncQpsServerTest() {
    server_->Shutdown();
    void *ignored_tag;
    bool ignored_ok;
    srv_cq_.Shutdown();
    while (srv_cq_.Next(&ignored_tag, &ignored_ok)) {
    }
    while (!contexts_.empty()) {
      delete contexts_.front();
      contexts_.pop_front();
    }
  }
  void ServeRpcs(int num_threads) {
    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; i++) {
      threads.push_back(std::thread([=]() {
        // Wait until work is available or we are shutting down
        bool ok;
        void *got_tag;
        while (srv_cq_.Next(&got_tag, &ok)) {
          EXPECT_EQ(ok, true);
          ServerRpcContext *ctx = detag(got_tag);
          // The tag is a pointer to an RPC context to invoke
          if (ctx->RunNextState() == false) {
            // this RPC context is done, so refresh it
            ctx->Reset();
          }
        }
        return;
      }));
    }
    while (!got_sigint) {
      std::this_thread::sleep_for(std::chrono::seconds(5));
    }
  }

 private:
  class ServerRpcContext {
   public:
    ServerRpcContext() {}
    virtual ~ServerRpcContext(){};
    virtual bool RunNextState() = 0;// do next state, return false if all done
    virtual void Reset() = 0;     // start this back at a clean state
  };
  static void *tag(ServerRpcContext *func) {
    return reinterpret_cast<void *>(func);
  }
  static ServerRpcContext *detag(void *tag) {
    return reinterpret_cast<ServerRpcContext *>(tag);
  }

  template <class RequestType, class ResponseType>
  class ServerRpcContextUnaryImpl : public ServerRpcContext {
   public:
    ServerRpcContextUnaryImpl(
        std::function<void(ServerContext *, RequestType *,
                           grpc::ServerAsyncResponseWriter<ResponseType> *,
                           void *)> request_method,
        std::function<grpc::Status(const RequestType *, ResponseType *)>
            invoke_method)
        : next_state_(&ServerRpcContextUnaryImpl::invoker),
          request_method_(request_method),
          invoke_method_(invoke_method),
          response_writer_(&srv_ctx_) {
      request_method_(&srv_ctx_, &req_, &response_writer_,
                      AsyncQpsServerTest::tag(this));
    }
    ~ServerRpcContextUnaryImpl() GRPC_OVERRIDE {}
    bool RunNextState() GRPC_OVERRIDE { return (this->*next_state_)(); }
    void Reset() GRPC_OVERRIDE {
      srv_ctx_ = ServerContext();
      req_ = RequestType();
      response_writer_ =
          grpc::ServerAsyncResponseWriter<ResponseType>(&srv_ctx_);

      // Then request the method
      next_state_ = &ServerRpcContextUnaryImpl::invoker;
      request_method_(&srv_ctx_, &req_, &response_writer_,
                      AsyncQpsServerTest::tag(this));
    }

   private:
    bool finisher() { return false; }
    bool invoker() {
      ResponseType response;

      // Call the RPC processing function
      grpc::Status status = invoke_method_(&req_, &response);

      // Have the response writer work and invoke on_finish when done
      next_state_ = &ServerRpcContextUnaryImpl::finisher;
      response_writer_.Finish(response, status, AsyncQpsServerTest::tag(this));
      return true;
    }
    ServerContext srv_ctx_;
    RequestType req_;
    bool (ServerRpcContextUnaryImpl::*next_state_)();
    std::function<void(ServerContext *, RequestType *,
                       grpc::ServerAsyncResponseWriter<ResponseType> *, void *)>
        request_method_;
    std::function<grpc::Status(const RequestType *, ResponseType *)>
        invoke_method_;
    grpc::ServerAsyncResponseWriter<ResponseType> response_writer_;
  };

  static Status CollectServerStats(const StatsRequest *,
                                   ServerStats *response) {
    struct rusage usage;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    getrusage(RUSAGE_SELF, &usage);
    response->set_time_now(time_double(&tv));
    response->set_time_user(time_double(&usage.ru_utime));
    response->set_time_system(time_double(&usage.ru_stime));
    return Status::OK;
  }
  static Status UnaryCall(const SimpleRequest *request,
                          SimpleResponse *response) {
    if (request->has_response_size() && request->response_size() > 0) {
      if (!SetPayload(request->response_type(), request->response_size(),
                      response->mutable_payload())) {
        return Status(grpc::StatusCode::INTERNAL, "Error creating payload.");
      }
    }
    return Status::OK;
  }
  CompletionQueue srv_cq_;
  TestService::AsyncService async_service_;
  std::unique_ptr<Server> server_;
  std::function<void(ServerContext *, SimpleRequest *,
                     grpc::ServerAsyncResponseWriter<SimpleResponse> *, void *)>
      request_unary_;
  std::function<void(ServerContext *, StatsRequest *,
                     grpc::ServerAsyncResponseWriter<ServerStats> *, void *)>
      request_stats_;
  std::forward_list<ServerRpcContext *> contexts_;
};

}  // namespace

static void RunServer() {
  AsyncQpsServerTest server;

  grpc_profiler_start("qps_server_async.prof");

  server.ServeRpcs(FLAGS_server_threads);

  grpc_profiler_stop();
}

int main(int argc, char **argv) {
  grpc_init();
  ParseCommandLineFlags(&argc, &argv, true);
  GPR_ASSERT(FLAGS_port != 0);
  GPR_ASSERT(!FLAGS_enable_ssl);

  signal(SIGINT, sigint_handler);

  RunServer();

  grpc_shutdown();
  google::protobuf::ShutdownProtobufLibrary();

  return 0;
}

/*
 *
 * Copyright 2015 gRPC authors.
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
#include <forward_list>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

#include <grpc++/generic/async_generic_service.h>
#include <grpc++/resource_quota.h>
#include <grpc++/security/server_credentials.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc++/server_context.h>
#include <grpc++/support/config.h>
#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/host_port.h>
#include <grpc/support/log.h>

#include "src/core/lib/surface/completion_queue.h"
#include "src/proto/grpc/testing/services.grpc.pb.h"
#include "test/core/util/test_config.h"
#include "test/cpp/qps/server.h"

namespace grpc {
namespace testing {

template <class RequestType, class ResponseType, class ServiceType,
          class ServerContextType>
class AsyncQpsServerTest final : public grpc::testing::Server {
 public:
  AsyncQpsServerTest(
      const ServerConfig& config,
      std::function<void(ServerBuilder*, ServiceType*)> register_service,
      std::function<void(ServiceType*, ServerContextType*, RequestType*,
                         ServerAsyncResponseWriter<ResponseType>*,
                         CompletionQueue*, ServerCompletionQueue*, void*)>
          request_unary_function,
      std::function<void(ServiceType*, ServerContextType*,
                         ServerAsyncReaderWriter<ResponseType, RequestType>*,
                         CompletionQueue*, ServerCompletionQueue*, void*)>
          request_streaming_function,
      std::function<void(ServiceType*, ServerContextType*,
                         ServerAsyncReader<ResponseType, RequestType>*,
                         CompletionQueue*, ServerCompletionQueue*, void*)>
          request_streaming_from_client_function,
      std::function<void(ServiceType*, ServerContextType*, RequestType*,
                         ServerAsyncWriter<ResponseType>*, CompletionQueue*,
                         ServerCompletionQueue*, void*)>
          request_streaming_from_server_function,
      std::function<void(ServiceType*, ServerContextType*,
                         ServerAsyncReaderWriter<ResponseType, RequestType>*,
                         CompletionQueue*, ServerCompletionQueue*, void*)>
          request_streaming_both_ways_function,
      std::function<grpc::Status(const PayloadConfig&, RequestType*,
                                 ResponseType*)>
          process_rpc)
      : Server(config) {
    ServerBuilder builder;

    auto port_num = port();
    // Negative port number means inproc server, so no listen port needed
    if (port_num >= 0) {
      char* server_address = nullptr;
      gpr_join_host_port(&server_address, "::", port_num);
      builder.AddListeningPort(server_address,
                               Server::CreateServerCredentials(config));
      gpr_free(server_address);
    }

    register_service(&builder, &async_service_);

    int num_threads = config.async_server_threads();
    if (num_threads <= 0) {  // dynamic sizing
      num_threads = cores();
      gpr_log(GPR_INFO, "Sizing async server to %d threads", num_threads);
    }

    int tpc = std::max(1, config.threads_per_cq());  // 1 if unspecified
    int num_cqs = (num_threads + tpc - 1) / tpc;     // ceiling operator
    for (int i = 0; i < num_cqs; i++) {
      srv_cqs_.emplace_back(builder.AddCompletionQueue());
    }
    for (int i = 0; i < num_threads; i++) {
      cq_.emplace_back(i % srv_cqs_.size());
    }

    ApplyConfigToBuilder(config, &builder);

    server_ = builder.BuildAndStart();

    auto process_rpc_bound =
        std::bind(process_rpc, config.payload_config(), std::placeholders::_1,
                  std::placeholders::_2);

    for (int i = 0; i < 5000; i++) {
      for (int j = 0; j < num_cqs; j++) {
        if (request_unary_function) {
          auto request_unary = std::bind(
              request_unary_function, &async_service_, std::placeholders::_1,
              std::placeholders::_2, std::placeholders::_3, srv_cqs_[j].get(),
              srv_cqs_[j].get(), std::placeholders::_4);
          contexts_.emplace_back(
              new ServerRpcContextUnaryImpl(request_unary, process_rpc_bound));
        }
        if (request_streaming_function) {
          auto request_streaming = std::bind(
              request_streaming_function, &async_service_,
              std::placeholders::_1, std::placeholders::_2, srv_cqs_[j].get(),
              srv_cqs_[j].get(), std::placeholders::_3);
          contexts_.emplace_back(new ServerRpcContextStreamingImpl(
              request_streaming, process_rpc_bound));
        }
        if (request_streaming_from_client_function) {
          auto request_streaming_from_client = std::bind(
              request_streaming_from_client_function, &async_service_,
              std::placeholders::_1, std::placeholders::_2, srv_cqs_[j].get(),
              srv_cqs_[j].get(), std::placeholders::_3);
          contexts_.emplace_back(new ServerRpcContextStreamingFromClientImpl(
              request_streaming_from_client, process_rpc_bound));
        }
        if (request_streaming_from_server_function) {
          auto request_streaming_from_server =
              std::bind(request_streaming_from_server_function, &async_service_,
                        std::placeholders::_1, std::placeholders::_2,
                        std::placeholders::_3, srv_cqs_[j].get(),
                        srv_cqs_[j].get(), std::placeholders::_4);
          contexts_.emplace_back(new ServerRpcContextStreamingFromServerImpl(
              request_streaming_from_server, process_rpc_bound));
        }
        if (request_streaming_both_ways_function) {
          // TODO(vjpai): Add this code
        }
      }
    }

    for (int i = 0; i < num_threads; i++) {
      shutdown_state_.emplace_back(new PerThreadShutdownState());
      threads_.emplace_back(&AsyncQpsServerTest::ThreadFunc, this, i);
    }
  }
  ~AsyncQpsServerTest() {
    for (auto ss = shutdown_state_.begin(); ss != shutdown_state_.end(); ++ss) {
      std::lock_guard<std::mutex> lock((*ss)->mutex);
      (*ss)->shutdown = true;
    }
    std::thread shutdown_thread(&AsyncQpsServerTest::ShutdownThreadFunc, this);
    for (auto cq = srv_cqs_.begin(); cq != srv_cqs_.end(); ++cq) {
      (*cq)->Shutdown();
    }
    for (auto thr = threads_.begin(); thr != threads_.end(); thr++) {
      thr->join();
    }
    for (auto cq = srv_cqs_.begin(); cq != srv_cqs_.end(); ++cq) {
      bool ok;
      void* got_tag;
      while ((*cq)->Next(&got_tag, &ok))
        ;
    }
    shutdown_thread.join();
  }

  int GetPollCount() override {
    int count = 0;
    for (auto cq = srv_cqs_.begin(); cq != srv_cqs_.end(); cq++) {
      count += grpc_get_cq_poll_num((*cq)->cq());
    }
    return count;
  }

  std::shared_ptr<Channel> InProcessChannel(
      const ChannelArguments& args) override {
    return server_->InProcessChannel(args);
  }

 private:
  void ShutdownThreadFunc() {
    // TODO (vpai): Remove this deadline and allow Shutdown to finish properly
    auto deadline = std::chrono::system_clock::now() + std::chrono::seconds(3);
    server_->Shutdown(deadline);
  }

  void ThreadFunc(int thread_idx) {
    // Wait until work is available or we are shutting down
    bool ok;
    void* got_tag;
    if (!srv_cqs_[cq_[thread_idx]]->Next(&got_tag, &ok)) {
      return;
    }
    ServerRpcContext* ctx;
    std::mutex* mu_ptr = &shutdown_state_[thread_idx]->mutex;
    do {
      ctx = detag(got_tag);
      // The tag is a pointer to an RPC context to invoke
      // Proceed while holding a lock to make sure that
      // this thread isn't supposed to shut down
      mu_ptr->lock();
      if (shutdown_state_[thread_idx]->shutdown) {
        mu_ptr->unlock();
        return;
      }
    } while (srv_cqs_[cq_[thread_idx]]->DoThenAsyncNext(
        [&, ctx, ok, mu_ptr]() {
          ctx->lock();
          if (!ctx->RunNextState(ok)) {
            ctx->Reset();
          }
          ctx->unlock();
          mu_ptr->unlock();
        },
        &got_tag, &ok, gpr_inf_future(GPR_CLOCK_REALTIME)));
  }

  class ServerRpcContext {
   public:
    ServerRpcContext() {}
    void lock() { mu_.lock(); }
    void unlock() { mu_.unlock(); }
    virtual ~ServerRpcContext(){};
    virtual bool RunNextState(bool) = 0;  // next state, return false if done
    virtual void Reset() = 0;             // start this back at a clean state
   private:
    std::mutex mu_;
  };
  static void* tag(ServerRpcContext* func) {
    return reinterpret_cast<void*>(func);
  }
  static ServerRpcContext* detag(void* tag) {
    return reinterpret_cast<ServerRpcContext*>(tag);
  }

  class ServerRpcContextUnaryImpl final : public ServerRpcContext {
   public:
    ServerRpcContextUnaryImpl(
        std::function<void(ServerContextType*, RequestType*,
                           grpc::ServerAsyncResponseWriter<ResponseType>*,
                           void*)>
            request_method,
        std::function<grpc::Status(RequestType*, ResponseType*)> invoke_method)
        : srv_ctx_(new ServerContextType),
          next_state_(&ServerRpcContextUnaryImpl::invoker),
          request_method_(request_method),
          invoke_method_(invoke_method),
          response_writer_(srv_ctx_.get()) {
      request_method_(srv_ctx_.get(), &req_, &response_writer_,
                      AsyncQpsServerTest::tag(this));
    }
    ~ServerRpcContextUnaryImpl() override {}
    bool RunNextState(bool ok) override { return (this->*next_state_)(ok); }
    void Reset() override {
      srv_ctx_.reset(new ServerContextType);
      req_ = RequestType();
      response_writer_ =
          grpc::ServerAsyncResponseWriter<ResponseType>(srv_ctx_.get());

      // Then request the method
      next_state_ = &ServerRpcContextUnaryImpl::invoker;
      request_method_(srv_ctx_.get(), &req_, &response_writer_,
                      AsyncQpsServerTest::tag(this));
    }

   private:
    bool finisher(bool) { return false; }
    bool invoker(bool ok) {
      if (!ok) {
        return false;
      }

      // Call the RPC processing function
      grpc::Status status = invoke_method_(&req_, &response_);

      // Have the response writer work and invoke on_finish when done
      next_state_ = &ServerRpcContextUnaryImpl::finisher;
      response_writer_.Finish(response_, status, AsyncQpsServerTest::tag(this));
      return true;
    }
    std::unique_ptr<ServerContextType> srv_ctx_;
    RequestType req_;
    ResponseType response_;
    bool (ServerRpcContextUnaryImpl::*next_state_)(bool);
    std::function<void(ServerContextType*, RequestType*,
                       grpc::ServerAsyncResponseWriter<ResponseType>*, void*)>
        request_method_;
    std::function<grpc::Status(RequestType*, ResponseType*)> invoke_method_;
    grpc::ServerAsyncResponseWriter<ResponseType> response_writer_;
  };

  class ServerRpcContextStreamingImpl final : public ServerRpcContext {
   public:
    ServerRpcContextStreamingImpl(
        std::function<void(
            ServerContextType*,
            grpc::ServerAsyncReaderWriter<ResponseType, RequestType>*, void*)>
            request_method,
        std::function<grpc::Status(RequestType*, ResponseType*)> invoke_method)
        : srv_ctx_(new ServerContextType),
          next_state_(&ServerRpcContextStreamingImpl::request_done),
          request_method_(request_method),
          invoke_method_(invoke_method),
          stream_(srv_ctx_.get()) {
      request_method_(srv_ctx_.get(), &stream_, AsyncQpsServerTest::tag(this));
    }
    ~ServerRpcContextStreamingImpl() override {}
    bool RunNextState(bool ok) override { return (this->*next_state_)(ok); }
    void Reset() override {
      srv_ctx_.reset(new ServerContextType);
      req_ = RequestType();
      stream_ = grpc::ServerAsyncReaderWriter<ResponseType, RequestType>(
          srv_ctx_.get());

      // Then request the method
      next_state_ = &ServerRpcContextStreamingImpl::request_done;
      request_method_(srv_ctx_.get(), &stream_, AsyncQpsServerTest::tag(this));
    }

   private:
    bool request_done(bool ok) {
      if (!ok) {
        return false;
      }
      next_state_ = &ServerRpcContextStreamingImpl::read_done;
      stream_.Read(&req_, AsyncQpsServerTest::tag(this));
      return true;
    }

    bool read_done(bool ok) {
      if (ok) {
        // invoke the method
        // Call the RPC processing function
        grpc::Status status = invoke_method_(&req_, &response_);
        // initiate the write
        next_state_ = &ServerRpcContextStreamingImpl::write_done;
        stream_.Write(response_, AsyncQpsServerTest::tag(this));
      } else {  // client has sent writes done
        // finish the stream
        next_state_ = &ServerRpcContextStreamingImpl::finish_done;
        stream_.Finish(Status::OK, AsyncQpsServerTest::tag(this));
      }
      return true;
    }
    bool write_done(bool ok) {
      // now go back and get another streaming read!
      if (ok) {
        next_state_ = &ServerRpcContextStreamingImpl::read_done;
        stream_.Read(&req_, AsyncQpsServerTest::tag(this));
      } else {
        next_state_ = &ServerRpcContextStreamingImpl::finish_done;
        stream_.Finish(Status::OK, AsyncQpsServerTest::tag(this));
      }
      return true;
    }
    bool finish_done(bool ok) { return false; /* reset the context */ }

    std::unique_ptr<ServerContextType> srv_ctx_;
    RequestType req_;
    ResponseType response_;
    bool (ServerRpcContextStreamingImpl::*next_state_)(bool);
    std::function<void(
        ServerContextType*,
        grpc::ServerAsyncReaderWriter<ResponseType, RequestType>*, void*)>
        request_method_;
    std::function<grpc::Status(RequestType*, ResponseType*)> invoke_method_;
    grpc::ServerAsyncReaderWriter<ResponseType, RequestType> stream_;
  };

  class ServerRpcContextStreamingFromClientImpl final
      : public ServerRpcContext {
   public:
    ServerRpcContextStreamingFromClientImpl(
        std::function<void(ServerContextType*,
                           grpc::ServerAsyncReader<ResponseType, RequestType>*,
                           void*)>
            request_method,
        std::function<grpc::Status(RequestType*, ResponseType*)> invoke_method)
        : srv_ctx_(new ServerContextType),
          next_state_(&ServerRpcContextStreamingFromClientImpl::request_done),
          request_method_(request_method),
          invoke_method_(invoke_method),
          stream_(srv_ctx_.get()) {
      request_method_(srv_ctx_.get(), &stream_, AsyncQpsServerTest::tag(this));
    }
    ~ServerRpcContextStreamingFromClientImpl() override {}
    bool RunNextState(bool ok) override { return (this->*next_state_)(ok); }
    void Reset() override {
      srv_ctx_.reset(new ServerContextType);
      req_ = RequestType();
      stream_ =
          grpc::ServerAsyncReader<ResponseType, RequestType>(srv_ctx_.get());

      // Then request the method
      next_state_ = &ServerRpcContextStreamingFromClientImpl::request_done;
      request_method_(srv_ctx_.get(), &stream_, AsyncQpsServerTest::tag(this));
    }

   private:
    bool request_done(bool ok) {
      if (!ok) {
        return false;
      }
      next_state_ = &ServerRpcContextStreamingFromClientImpl::read_done;
      stream_.Read(&req_, AsyncQpsServerTest::tag(this));
      return true;
    }

    bool read_done(bool ok) {
      if (ok) {
        // In this case, just do another read
        // next_state_ is unchanged
        stream_.Read(&req_, AsyncQpsServerTest::tag(this));
        return true;
      } else {  // client has sent writes done
        // invoke the method
        // Call the RPC processing function
        grpc::Status status = invoke_method_(&req_, &response_);
        // finish the stream
        next_state_ = &ServerRpcContextStreamingFromClientImpl::finish_done;
        stream_.Finish(response_, Status::OK, AsyncQpsServerTest::tag(this));
      }
      return true;
    }
    bool finish_done(bool ok) { return false; /* reset the context */ }

    std::unique_ptr<ServerContextType> srv_ctx_;
    RequestType req_;
    ResponseType response_;
    bool (ServerRpcContextStreamingFromClientImpl::*next_state_)(bool);
    std::function<void(ServerContextType*,
                       grpc::ServerAsyncReader<ResponseType, RequestType>*,
                       void*)>
        request_method_;
    std::function<grpc::Status(RequestType*, ResponseType*)> invoke_method_;
    grpc::ServerAsyncReader<ResponseType, RequestType> stream_;
  };

  class ServerRpcContextStreamingFromServerImpl final
      : public ServerRpcContext {
   public:
    ServerRpcContextStreamingFromServerImpl(
        std::function<void(ServerContextType*, RequestType*,
                           grpc::ServerAsyncWriter<ResponseType>*, void*)>
            request_method,
        std::function<grpc::Status(RequestType*, ResponseType*)> invoke_method)
        : srv_ctx_(new ServerContextType),
          next_state_(&ServerRpcContextStreamingFromServerImpl::request_done),
          request_method_(request_method),
          invoke_method_(invoke_method),
          stream_(srv_ctx_.get()) {
      request_method_(srv_ctx_.get(), &req_, &stream_,
                      AsyncQpsServerTest::tag(this));
    }
    ~ServerRpcContextStreamingFromServerImpl() override {}
    bool RunNextState(bool ok) override { return (this->*next_state_)(ok); }
    void Reset() override {
      srv_ctx_.reset(new ServerContextType);
      req_ = RequestType();
      stream_ = grpc::ServerAsyncWriter<ResponseType>(srv_ctx_.get());

      // Then request the method
      next_state_ = &ServerRpcContextStreamingFromServerImpl::request_done;
      request_method_(srv_ctx_.get(), &req_, &stream_,
                      AsyncQpsServerTest::tag(this));
    }

   private:
    bool request_done(bool ok) {
      if (!ok) {
        return false;
      }
      // invoke the method
      // Call the RPC processing function
      grpc::Status status = invoke_method_(&req_, &response_);

      next_state_ = &ServerRpcContextStreamingFromServerImpl::write_done;
      stream_.Write(response_, AsyncQpsServerTest::tag(this));
      return true;
    }

    bool write_done(bool ok) {
      if (ok) {
        // Do another write!
        // next_state_ is unchanged
        stream_.Write(response_, AsyncQpsServerTest::tag(this));
      } else {  // must be done so let's finish
        next_state_ = &ServerRpcContextStreamingFromServerImpl::finish_done;
        stream_.Finish(Status::OK, AsyncQpsServerTest::tag(this));
      }
      return true;
    }
    bool finish_done(bool ok) { return false; /* reset the context */ }

    std::unique_ptr<ServerContextType> srv_ctx_;
    RequestType req_;
    ResponseType response_;
    bool (ServerRpcContextStreamingFromServerImpl::*next_state_)(bool);
    std::function<void(ServerContextType*, RequestType*,
                       grpc::ServerAsyncWriter<ResponseType>*, void*)>
        request_method_;
    std::function<grpc::Status(RequestType*, ResponseType*)> invoke_method_;
    grpc::ServerAsyncWriter<ResponseType> stream_;
  };

  std::vector<std::thread> threads_;
  std::unique_ptr<grpc::Server> server_;
  std::vector<std::unique_ptr<grpc::ServerCompletionQueue>> srv_cqs_;
  std::vector<int> cq_;
  ServiceType async_service_;
  std::vector<std::unique_ptr<ServerRpcContext>> contexts_;

  struct PerThreadShutdownState {
    mutable std::mutex mutex;
    bool shutdown;
    PerThreadShutdownState() : shutdown(false) {}
  };

  std::vector<std::unique_ptr<PerThreadShutdownState>> shutdown_state_;
};

static void RegisterBenchmarkService(ServerBuilder* builder,
                                     BenchmarkService::AsyncService* service) {
  builder->RegisterService(service);
}
static void RegisterGenericService(ServerBuilder* builder,
                                   grpc::AsyncGenericService* service) {
  builder->RegisterAsyncGenericService(service);
}

static Status ProcessSimpleRPC(const PayloadConfig&, SimpleRequest* request,
                               SimpleResponse* response) {
  if (request->response_size() > 0) {
    if (!Server::SetPayload(request->response_type(), request->response_size(),
                            response->mutable_payload())) {
      return Status(grpc::StatusCode::INTERNAL, "Error creating payload.");
    }
  }
  // We are done using the request. Clear it to reduce working memory.
  // This proves to reduce cache misses in large message size cases.
  request->Clear();
  return Status::OK;
}

static Status ProcessGenericRPC(const PayloadConfig& payload_config,
                                ByteBuffer* request, ByteBuffer* response) {
  // We are done using the request. Clear it to reduce working memory.
  // This proves to reduce cache misses in large message size cases.
  request->Clear();
  int resp_size = payload_config.bytebuf_params().resp_size();
  std::unique_ptr<char[]> buf(new char[resp_size]);
  Slice slice(buf.get(), resp_size);
  *response = ByteBuffer(&slice, 1);
  return Status::OK;
}

std::unique_ptr<Server> CreateAsyncServer(const ServerConfig& config) {
  return std::unique_ptr<Server>(
      new AsyncQpsServerTest<SimpleRequest, SimpleResponse,
                             BenchmarkService::AsyncService,
                             grpc::ServerContext>(
          config, RegisterBenchmarkService,
          &BenchmarkService::AsyncService::RequestUnaryCall,
          &BenchmarkService::AsyncService::RequestStreamingCall,
          &BenchmarkService::AsyncService::RequestStreamingFromClient,
          &BenchmarkService::AsyncService::RequestStreamingFromServer,
          &BenchmarkService::AsyncService::RequestStreamingBothWays,
          ProcessSimpleRPC));
}
std::unique_ptr<Server> CreateAsyncGenericServer(const ServerConfig& config) {
  return std::unique_ptr<Server>(
      new AsyncQpsServerTest<ByteBuffer, ByteBuffer, grpc::AsyncGenericService,
                             grpc::GenericServerContext>(
          config, RegisterGenericService, nullptr,
          &grpc::AsyncGenericService::RequestCall, nullptr, nullptr, nullptr,
          ProcessGenericRPC));
}

}  // namespace testing
}  // namespace grpc

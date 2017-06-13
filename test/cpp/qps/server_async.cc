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

#include "src/proto/grpc/testing/services.grpc.pb.h"
#include "test/core/util/test_config.h"
#include "test/cpp/qps/server.h"

namespace grpc {
namespace testing {

static Status ProcessMessage(const PayloadConfig &payload_config,
                             const SimpleRequest *request,
                             SimpleResponse *response) {
  if (request->response_size() > 0) {
    if (!Server::SetPayload(request->response_type(), request->response_size(),
                            response->mutable_payload())) {
      return Status(grpc::StatusCode::INTERNAL, "Error creating payload.");
    }
  }
  return Status::OK;
}

static Status ProcessMessage(const PayloadConfig &payload_config,
                             const ByteBuffer *request, ByteBuffer *response) {
  int resp_size = payload_config.bytebuf_params().resp_size();
  std::unique_ptr<char[]> buf(new char[resp_size]);
  grpc_slice s = grpc_slice_from_copied_buffer(buf.get(), resp_size);
  Slice slice(s, Slice::STEAL_REF);
  *response = ByteBuffer(&slice, 1);
  return Status::OK;
}

template <class ServerTraits>
class ServerRpcContext {
 public:
  ServerRpcContext() {}
  virtual ~ServerRpcContext(){};
  virtual bool RunNextState(bool) = 0;  // next state, return false if done
  virtual void Reset() = 0;             // start this back at a clean state
  static void *tag(ServerRpcContext *func) {
    return reinterpret_cast<void *>(func);
  }
  static ServerRpcContext *detag(void *tag) {
    return reinterpret_cast<ServerRpcContext *>(tag);
  }
};

template <class ServerTraits>
struct ContextArgs {
  typename ServerTraits::ServiceType *service;
  ServerCompletionQueue *cq;
  PayloadConfig config;
};

template <class ServerTraits>
class ServerRpcContextUnaryImpl final : public ServerRpcContext<ServerTraits> {
 public:
  ServerRpcContextUnaryImpl(const ContextArgs<ServerTraits> &context_args)
      : context_args_(context_args),
        srv_ctx_(new typename ServerTraits::ContextType),
        next_state_(&ServerRpcContextUnaryImpl::invoker),
        response_writer_(srv_ctx_.get()) {}
  ~ServerRpcContextUnaryImpl() override {}

  bool RunNextState(bool ok) override { return (this->*next_state_)(ok); }
  void Reset() override {
    srv_ctx_.reset(new typename ServerTraits::ContextType);
    req_ = typename ServerTraits::RequestType();
    response_writer_ =
        grpc::ServerAsyncResponseWriter<typename ServerTraits::ResponseType>(
            srv_ctx_.get());

    // Then request the method
    next_state_ = &ServerRpcContextUnaryImpl::invoker;
    context_args_.service->RequestUnaryCall(
        srv_ctx_.get(), &req_, &response_writer_, context_args_.cq,
        context_args_.cq, ServerRpcContext<ServerTraits>::tag(this));
  }

 private:
  bool finisher(bool) { return false; }
  bool invoker(bool ok) {
    if (!ok) {
      return false;
    }

    // Call the RPC processing function
    grpc::Status status =
        ProcessMessage(context_args_.config, &req_, &response_);

    // Have the response writer work and invoke on_finish when done
    next_state_ = &ServerRpcContextUnaryImpl::finisher;
    response_writer_.Finish(response_, status,
                            ServerRpcContext<ServerTraits>::tag(this));
    return true;
  }
  ContextArgs<ServerTraits> context_args_;
  std::unique_ptr<typename ServerTraits::ContextType> srv_ctx_;
  typename ServerTraits::RequestType req_;
  typename ServerTraits::ResponseType response_;
  bool (ServerRpcContextUnaryImpl::*next_state_)(bool);
  grpc::ServerAsyncResponseWriter<typename ServerTraits::ResponseType>
      response_writer_;
};

template <class ServerTraits>
class ServerRpcContextStreamingImpl final
    : public ServerRpcContext<ServerTraits> {
 public:
  ServerRpcContextStreamingImpl(const ContextArgs<ServerTraits> &context_args)
      : context_args_(context_args),
        srv_ctx_(new typename ServerTraits::ContextType),
        next_state_(&ServerRpcContextStreamingImpl::request_done),
        stream_(srv_ctx_.get()) {}
  ~ServerRpcContextStreamingImpl() override {}
  bool RunNextState(bool ok) override { return (this->*next_state_)(ok); }
  void Reset() override {
    srv_ctx_.reset(new typename ServerTraits::ContextType);
    req_ = typename ServerTraits::RequestType();
    stream_ = grpc::ServerAsyncReaderWriter<typename ServerTraits::ResponseType,
                                            typename ServerTraits::RequestType>(
        srv_ctx_.get());

    // Then request the method
    next_state_ = &ServerRpcContextStreamingImpl::request_done;
    ServerTraits::RequestStreamingCall(
        context_args_.service, srv_ctx_.get(), &stream_, context_args_.cq,
        context_args_.cq, ServerRpcContext<ServerTraits>::tag(this));
  }

 private:
  bool request_done(bool ok) {
    if (!ok) {
      return false;
    }
    next_state_ = &ServerRpcContextStreamingImpl::read_done;
    stream_.Read(&req_, ServerRpcContext<ServerTraits>::tag(this));
    return true;
  }

  bool read_done(bool ok) {
    if (ok) {
      // invoke the method
      // Call the RPC processing function
      grpc::Status status =
          ProcessMessage(context_args_.config, &req_, &response_);
      // initiate the write
      next_state_ = &ServerRpcContextStreamingImpl::write_done;
      stream_.Write(response_, ServerRpcContext<ServerTraits>::tag(this));
    } else {  // client has sent writes done
      // finish the stream
      next_state_ = &ServerRpcContextStreamingImpl::finish_done;
      stream_.Finish(Status::OK, ServerRpcContext<ServerTraits>::tag(this));
    }
    return true;
  }
  bool write_done(bool ok) {
    // now go back and get another streaming read!
    if (ok) {
      next_state_ = &ServerRpcContextStreamingImpl::read_done;
      stream_.Read(&req_, ServerRpcContext<ServerTraits>::tag(this));
    } else {
      next_state_ = &ServerRpcContextStreamingImpl::finish_done;
      stream_.Finish(Status::OK, ServerRpcContext<ServerTraits>::tag(this));
    }
    return true;
  }
  bool finish_done(bool ok) { return false; /* reset the context */ }

  ContextArgs<ServerTraits> context_args_;
  std::unique_ptr<typename ServerTraits::ContextType> srv_ctx_;
  typename ServerTraits::RequestType req_;
  typename ServerTraits::ResponseType response_;
  bool (ServerRpcContextStreamingImpl::*next_state_)(bool);
  grpc::ServerAsyncReaderWriter<typename ServerTraits::ResponseType,
                                typename ServerTraits::RequestType>
      stream_;
};

template <class ServerTraits>
class ServerRpcContextStreamingFromClientImpl final
    : public ServerRpcContext<ServerTraits> {
 public:
  ServerRpcContextStreamingFromClientImpl(
      const ContextArgs<ServerTraits> &context_args)
      : context_args_(context_args),
        srv_ctx_(new typename ServerTraits::ContextType),
        next_state_(&ServerRpcContextStreamingFromClientImpl::request_done),
        stream_(srv_ctx_.get()) {}
  ~ServerRpcContextStreamingFromClientImpl() override {}
  bool RunNextState(bool ok) override { return (this->*next_state_)(ok); }
  void Reset() override {
    srv_ctx_.reset(new typename ServerTraits::ContextType);
    req_ = typename ServerTraits::RequestType();
    stream_ = grpc::ServerAsyncReader<typename ServerTraits::ResponseType,
                                      typename ServerTraits::RequestType>(
        srv_ctx_.get());

    // Then request the method
    next_state_ = &ServerRpcContextStreamingFromClientImpl::request_done;
    context_args_.service->RequestStreamingFromClient(
        srv_ctx_.get(), &stream_, context_args_.cq, context_args_.cq,
        ServerRpcContext<ServerTraits>::tag(this));
  }

 private:
  bool request_done(bool ok) {
    if (!ok) {
      return false;
    }
    next_state_ = &ServerRpcContextStreamingFromClientImpl::read_done;
    stream_.Read(&req_, ServerRpcContext<ServerTraits>::tag(this));
    return true;
  }

  bool read_done(bool ok) {
    if (ok) {
      // In this case, just do another read
      // next_state_ is unchanged
      stream_.Read(&req_, ServerRpcContext<ServerTraits>::tag(this));
      return true;
    } else {  // client has sent writes done
      // invoke the method
      // Call the RPC processing function
      grpc::Status status =
          ProcessMessage(context_args_.config, &req_, &response_);
      // finish the stream
      next_state_ = &ServerRpcContextStreamingFromClientImpl::finish_done;
      stream_.Finish(response_, Status::OK,
                     ServerRpcContext<ServerTraits>::tag(this));
    }
    return true;
  }
  bool finish_done(bool ok) { return false; /* reset the context */ }

  ContextArgs<ServerTraits> context_args_;
  std::unique_ptr<typename ServerTraits::ContextType> srv_ctx_;
  typename ServerTraits::RequestType req_;
  typename ServerTraits::ResponseType response_;
  bool (ServerRpcContextStreamingFromClientImpl::*next_state_)(bool);
  grpc::ServerAsyncReader<typename ServerTraits::ResponseType,
                          typename ServerTraits::RequestType>
      stream_;
};

template <class ServerTraits>
class ServerRpcContextStreamingFromServerImpl final
    : public ServerRpcContext<ServerTraits> {
 public:
  ServerRpcContextStreamingFromServerImpl(
      const ContextArgs<ServerTraits> &context_args)
      : context_args_(context_args),
        srv_ctx_(new typename ServerTraits::ContextType),
        next_state_(&ServerRpcContextStreamingFromServerImpl::request_done),
        stream_(srv_ctx_.get()) {
    Reset();
  }
  ~ServerRpcContextStreamingFromServerImpl() override {}
  bool RunNextState(bool ok) override { return (this->*next_state_)(ok); }
  void Reset() override {
    srv_ctx_.reset(new typename ServerTraits::ContextType);
    req_ = typename ServerTraits::RequestType();
    stream_ = grpc::ServerAsyncWriter<typename ServerTraits::ResponseType>(
        srv_ctx_.get());

    // Then request the method
    next_state_ = &ServerRpcContextStreamingFromServerImpl::request_done;
    context_args_.service->RequestStreamingFromServer(
        srv_ctx_.get(), &req_, &stream_, context_args_.cq, context_args_.cq,
        ServerRpcContext<ServerTraits>::tag(this));
  }

 private:
  bool request_done(bool ok) {
    if (!ok) {
      return false;
    }
    // invoke the method
    // Call the RPC processing function
    grpc::Status status =
        ProcessMessage(context_args_.config, &req_, &response_);

    next_state_ = &ServerRpcContextStreamingFromServerImpl::write_done;
    stream_.Write(response_, ServerRpcContext<ServerTraits>::tag(this));
    return true;
  }

  bool write_done(bool ok) {
    if (ok) {
      // Do another write!
      // next_state_ is unchanged
      stream_.Write(response_, ServerRpcContext<ServerTraits>::tag(this));
    } else {  // must be done so let's finish
      next_state_ = &ServerRpcContextStreamingFromServerImpl::finish_done;
      stream_.Finish(Status::OK, ServerRpcContext<ServerTraits>::tag(this));
    }
    return true;
  }
  bool finish_done(bool ok) { return false; /* reset the context */ }

  ContextArgs<ServerTraits> context_args_;
  std::unique_ptr<typename ServerTraits::ContextType> srv_ctx_;
  typename ServerTraits::RequestType req_;
  typename ServerTraits::ResponseType response_;
  bool (ServerRpcContextStreamingFromServerImpl::*next_state_)(bool);
  grpc::ServerAsyncWriter<typename ServerTraits::ResponseType> stream_;
};

template <class ServerTraits>
class AsyncQpsServerTest final : public grpc::testing::Server {
 public:
  AsyncQpsServerTest(
      const ServerConfig &config,
      const std::vector<std::function<ServerRpcContext<ServerTraits> *(
          const ContextArgs<ServerTraits> &)>> &context_factories)
      : Server(config) {
    char *server_address = NULL;

    gpr_join_host_port(&server_address, "::", port());

    ServerBuilder builder;
    builder.AddListeningPort(server_address,
                             Server::CreateServerCredentials(config));
    gpr_free(server_address);

    ServerTraits::RegisterService(&async_service_, &builder);

    int num_threads = config.async_server_threads();
    if (num_threads <= 0) {  // dynamic sizing
      num_threads = cores();
      gpr_log(GPR_INFO, "Sizing async server to %d threads", num_threads);
    }

    for (int i = 0; i < num_threads; i++) {
      srv_cqs_.emplace_back(builder.AddCompletionQueue());
    }

    if (config.resource_quota_size() > 0) {
      builder.SetResourceQuota(ResourceQuota("AsyncQpsServerTest")
                                   .Resize(config.resource_quota_size()));
    }

    server_ = builder.BuildAndStart();

    for (int i = 0; i < 5000; i++) {
      for (int j = 0; j < num_threads; j++) {
        for (const auto &factory : context_factories) {
          contexts_.emplace_back(factory(ContextArgs<ServerTraits>{
              &async_service_, srv_cqs_[j].get(), config.payload_config()}));
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
      void *got_tag;
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

 private:
  void ShutdownThreadFunc() {
    // TODO (vpai): Remove this deadline and allow Shutdown to finish properly
    auto deadline = std::chrono::system_clock::now() + std::chrono::seconds(3);
    server_->Shutdown(deadline);
  }

  void ThreadFunc(int thread_idx) {
    // Wait until work is available or we are shutting down
    bool ok;
    void *got_tag;
    while (srv_cqs_[thread_idx]->Next(&got_tag, &ok)) {
      auto *ctx = ServerRpcContext<ServerTraits>::detag(got_tag);
      // The tag is a pointer to an RPC context to invoke
      // Proceed while holding a lock to make sure that
      // this thread isn't supposed to shut down
      std::lock_guard<std::mutex> l(shutdown_state_[thread_idx]->mutex);
      if (shutdown_state_[thread_idx]->shutdown) {
        return;
      }
      const bool still_going = ctx->RunNextState(ok);
      // if this RPC context is done, refresh it
      if (!still_going) {
        ctx->Reset();
      }
    }
    return;
  }

  std::vector<std::thread> threads_;
  std::unique_ptr<grpc::Server> server_;
  std::vector<std::unique_ptr<grpc::ServerCompletionQueue>> srv_cqs_;
  typename ServerTraits::ServiceType async_service_;
  std::vector<std::unique_ptr<ServerRpcContext<ServerTraits>>> contexts_;

  struct PerThreadShutdownState {
    mutable std::mutex mutex;
    bool shutdown;
    PerThreadShutdownState() : shutdown(false) {}
  };

  std::vector<std::unique_ptr<PerThreadShutdownState>> shutdown_state_;
};

struct ProtoServerTraits {
  typedef BenchmarkService::AsyncService ServiceType;
  typedef SimpleRequest RequestType;
  typedef SimpleResponse ResponseType;
  typedef grpc::ServerContext ContextType;

  static void RegisterService(ServiceType *service, ServerBuilder *builder) {
    builder->RegisterService(service);
  }

  static void RequestStreamingCall(
      ServiceType *service, ContextType *context,
      ServerAsyncReaderWriter<ResponseType, RequestType> *stream,
      CompletionQueue *new_call_cq, ServerCompletionQueue *notification_cq,
      void *tag) {
    service->RequestStreamingCall(context, stream, new_call_cq, notification_cq,
                                  tag);
  }
};

struct GenericServerTraits {
  typedef AsyncGenericService ServiceType;
  typedef ByteBuffer RequestType;
  typedef ByteBuffer ResponseType;
  typedef GenericServerContext ContextType;

  static void RegisterService(ServiceType *service, ServerBuilder *builder) {
    builder->RegisterAsyncGenericService(service);
  }

  static void RequestStreamingCall(
      ServiceType *service, ContextType *context,
      ServerAsyncReaderWriter<ResponseType, RequestType> *stream,
      CompletionQueue *new_call_cq, ServerCompletionQueue *notification_cq,
      void *tag) {
    service->RequestCall(context, stream, new_call_cq, notification_cq, tag);
  }
};

template <template <class> class ContextImpl, class Traits>
static std::function<
    ServerRpcContext<Traits> *(const ContextArgs<Traits> &traits)>
ContextFactory() {
  return [](const ContextArgs<Traits> &args) {
    return new ContextImpl<Traits>(args);
  };
}

std::unique_ptr<Server> CreateAsyncServer(const ServerConfig &config) {
  return std::unique_ptr<Server>(new AsyncQpsServerTest<ProtoServerTraits>(
      config,
      {
          ContextFactory<ServerRpcContextUnaryImpl, ProtoServerTraits>(),
          ContextFactory<ServerRpcContextStreamingImpl, ProtoServerTraits>(),
          ContextFactory<ServerRpcContextStreamingFromClientImpl,
                         ProtoServerTraits>(),
          ContextFactory<ServerRpcContextStreamingFromServerImpl,
                         ProtoServerTraits>(),
      }));
}

std::unique_ptr<Server> CreateAsyncGenericServer(const ServerConfig &config) {
  return std::unique_ptr<Server>(new AsyncQpsServerTest<GenericServerTraits>(
      config,
      {ContextFactory<ServerRpcContextStreamingImpl, GenericServerTraits>()}));
}

}  // namespace testing
}  // namespace grpc

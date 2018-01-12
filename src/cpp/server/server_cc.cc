/*
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

#include <grpc++/server.h>

#include <cstdlib>
#include <sstream>
#include <utility>

#include <grpc++/completion_queue.h>
#include <grpc++/generic/async_generic_service.h>
#include <grpc++/impl/codegen/async_unary_call.h>
#include <grpc++/impl/codegen/completion_queue_tag.h>
#include <grpc++/impl/grpc_library.h>
#include <grpc++/impl/method_handler_impl.h>
#include <grpc++/impl/rpc_service_method.h>
#include <grpc++/impl/server_initializer.h>
#include <grpc++/impl/service_type.h>
#include <grpc++/security/server_credentials.h>
#include <grpc++/server_context.h>
#include <grpc++/support/time.h>
#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/ext/transport/inproc/inproc_transport.h"
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/surface/call.h"
#include "src/cpp/client/create_channel_internal.h"
#include "src/cpp/server/health/default_health_check_service.h"
#include "src/cpp/thread_manager/thread_manager.h"

namespace grpc {

class DefaultGlobalCallbacks final : public Server::GlobalCallbacks {
 public:
  ~DefaultGlobalCallbacks() override {}
  void PreSynchronousRequest(ServerContext* context) override {}
  void PostSynchronousRequest(ServerContext* context) override {}
};

static std::shared_ptr<Server::GlobalCallbacks> g_callbacks = nullptr;
static gpr_once g_once_init_callbacks = GPR_ONCE_INIT;

static void InitGlobalCallbacks() {
  if (!g_callbacks) {
    g_callbacks.reset(new DefaultGlobalCallbacks());
  }
}

class Server::UnimplementedAsyncRequestContext {
 protected:
  UnimplementedAsyncRequestContext() : generic_stream_(&server_context_) {}

  GenericServerContext server_context_;
  GenericServerAsyncReaderWriter generic_stream_;
};

class Server::UnimplementedAsyncRequest final
    : public UnimplementedAsyncRequestContext,
      public GenericAsyncRequest {
 public:
  UnimplementedAsyncRequest(Server* server, ServerCompletionQueue* cq)
      : GenericAsyncRequest(server, &server_context_, &generic_stream_, cq, cq,
                            nullptr, false),
        server_(server),
        cq_(cq) {}

  bool FinalizeResult(void** tag, bool* status) override;

  ServerContext* context() { return &server_context_; }
  GenericServerAsyncReaderWriter* stream() { return &generic_stream_; }

 private:
  Server* const server_;
  ServerCompletionQueue* const cq_;
};

typedef internal::SneakyCallOpSet<internal::CallOpSendInitialMetadata,
                                  internal::CallOpServerSendStatus>
    UnimplementedAsyncResponseOp;
class Server::UnimplementedAsyncResponse final
    : public UnimplementedAsyncResponseOp {
 public:
  UnimplementedAsyncResponse(UnimplementedAsyncRequest* request);
  ~UnimplementedAsyncResponse() { delete request_; }

  bool FinalizeResult(void** tag, bool* status) override {
    bool r = UnimplementedAsyncResponseOp::FinalizeResult(tag, status);
    delete this;
    return r;
  }

 private:
  UnimplementedAsyncRequest* const request_;
};

class ShutdownTag : public internal::CompletionQueueTag {
 public:
  bool FinalizeResult(void** tag, bool* status) { return false; }
};

class DummyTag : public internal::CompletionQueueTag {
 public:
  bool FinalizeResult(void** tag, bool* status) {
    *status = true;
    return true;
  }
};

class Server::SyncRequest final : public internal::CompletionQueueTag {
 public:
  SyncRequest(internal::RpcServiceMethod* method, void* tag)
      : method_(method),
        tag_(tag),
        in_flight_(false),
        has_request_payload_(
            method->method_type() == internal::RpcMethod::NORMAL_RPC ||
            method->method_type() == internal::RpcMethod::SERVER_STREAMING),
        call_details_(nullptr),
        cq_(nullptr) {
    grpc_metadata_array_init(&request_metadata_);
  }

  ~SyncRequest() {
    if (call_details_) {
      delete call_details_;
    }
    grpc_metadata_array_destroy(&request_metadata_);
  }

  void SetupRequest() { cq_ = grpc_completion_queue_create_for_pluck(nullptr); }

  void TeardownRequest() {
    grpc_completion_queue_destroy(cq_);
    cq_ = nullptr;
  }

  void Request(grpc_server* server, grpc_completion_queue* notify_cq) {
    GPR_ASSERT(cq_ && !in_flight_);
    in_flight_ = true;
    if (tag_) {
      if (GRPC_CALL_OK !=
          grpc_server_request_registered_call(
              server, tag_, &call_, &deadline_, &request_metadata_,
              has_request_payload_ ? &request_payload_ : nullptr, cq_,
              notify_cq, this)) {
        TeardownRequest();
        return;
      }
    } else {
      if (!call_details_) {
        call_details_ = new grpc_call_details;
        grpc_call_details_init(call_details_);
      }
      if (grpc_server_request_call(server, &call_, call_details_,
                                   &request_metadata_, cq_, notify_cq,
                                   this) != GRPC_CALL_OK) {
        TeardownRequest();
        return;
      }
    }
  }

  bool FinalizeResult(void** tag, bool* status) override {
    if (!*status) {
      grpc_completion_queue_destroy(cq_);
    }
    if (call_details_) {
      deadline_ = call_details_->deadline;
      grpc_call_details_destroy(call_details_);
      grpc_call_details_init(call_details_);
    }
    return true;
  }

  class CallData final {
   public:
    explicit CallData(Server* server, SyncRequest* mrd)
        : cq_(mrd->cq_),
          call_(mrd->call_, server, &cq_, server->max_receive_message_size()),
          ctx_(mrd->deadline_, &mrd->request_metadata_),
          has_request_payload_(mrd->has_request_payload_),
          request_payload_(mrd->request_payload_),
          method_(mrd->method_) {
      ctx_.set_call(mrd->call_);
      ctx_.cq_ = &cq_;
      GPR_ASSERT(mrd->in_flight_);
      mrd->in_flight_ = false;
      mrd->request_metadata_.count = 0;
    }

    ~CallData() {
      if (has_request_payload_ && request_payload_) {
        grpc_byte_buffer_destroy(request_payload_);
      }
    }

    void Run(std::shared_ptr<GlobalCallbacks> global_callbacks) {
      ctx_.BeginCompletionOp(&call_);
      global_callbacks->PreSynchronousRequest(&ctx_);
      method_->handler()->RunHandler(internal::MethodHandler::HandlerParameter(
          &call_, &ctx_, request_payload_));
      global_callbacks->PostSynchronousRequest(&ctx_);
      request_payload_ = nullptr;

      cq_.Shutdown();

      internal::CompletionQueueTag* op_tag = ctx_.GetCompletionOpTag();
      cq_.TryPluck(op_tag, gpr_inf_future(GPR_CLOCK_REALTIME));

      /* Ensure the cq_ is shutdown */
      DummyTag ignored_tag;
      GPR_ASSERT(cq_.Pluck(&ignored_tag) == false);
    }

   private:
    CompletionQueue cq_;
    internal::Call call_;
    ServerContext ctx_;
    const bool has_request_payload_;
    grpc_byte_buffer* request_payload_;
    internal::RpcServiceMethod* const method_;
  };

 private:
  internal::RpcServiceMethod* const method_;
  void* const tag_;
  bool in_flight_;
  const bool has_request_payload_;
  grpc_call* call_;
  grpc_call_details* call_details_;
  gpr_timespec deadline_;
  grpc_metadata_array request_metadata_;
  grpc_byte_buffer* request_payload_;
  grpc_completion_queue* cq_;
};

// Implementation of ThreadManager. Each instance of SyncRequestThreadManager
// manages a pool of threads that poll for incoming Sync RPCs and call the
// appropriate RPC handlers
class Server::SyncRequestThreadManager : public ThreadManager {
 public:
  SyncRequestThreadManager(Server* server, CompletionQueue* server_cq,
                           std::shared_ptr<GlobalCallbacks> global_callbacks,
                           int min_pollers, int max_pollers,
                           int cq_timeout_msec)
      : ThreadManager(min_pollers, max_pollers),
        server_(server),
        server_cq_(server_cq),
        cq_timeout_msec_(cq_timeout_msec),
        global_callbacks_(global_callbacks) {}

  WorkStatus PollForWork(void** tag, bool* ok) override {
    *tag = nullptr;
    // TODO(ctiller): workaround for GPR_TIMESPAN based deadlines not working
    // right now
    gpr_timespec deadline =
        gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                     gpr_time_from_millis(cq_timeout_msec_, GPR_TIMESPAN));

    switch (server_cq_->AsyncNext(tag, ok, deadline)) {
      case CompletionQueue::TIMEOUT:
        return TIMEOUT;
      case CompletionQueue::SHUTDOWN:
        return SHUTDOWN;
      case CompletionQueue::GOT_EVENT:
        return WORK_FOUND;
    }

    GPR_UNREACHABLE_CODE(return TIMEOUT);
  }

  void DoWork(void* tag, bool ok) override {
    SyncRequest* sync_req = static_cast<SyncRequest*>(tag);

    if (!sync_req) {
      // No tag. Nothing to work on. This is an unlikley scenario and possibly a
      // bug in RPC Manager implementation.
      gpr_log(GPR_ERROR, "Sync server. DoWork() was called with NULL tag");
      return;
    }

    if (ok) {
      // Calldata takes ownership of the completion queue inside sync_req
      SyncRequest::CallData cd(server_, sync_req);
      // Prepare for the next request
      if (!IsShutdown()) {
        sync_req->SetupRequest();  // Create new completion queue for sync_req
        sync_req->Request(server_->c_server(), server_cq_->cq());
      }

      GPR_TIMER_SCOPE("cd.Run()", 0);
      cd.Run(global_callbacks_);
    }
    // TODO (sreek) If ok is false here (which it isn't in case of
    // grpc_request_registered_call), we should still re-queue the request
    // object
  }

  void AddSyncMethod(internal::RpcServiceMethod* method, void* tag) {
    sync_requests_.emplace_back(new SyncRequest(method, tag));
  }

  void AddUnknownSyncMethod() {
    if (!sync_requests_.empty()) {
      unknown_method_.reset(new internal::RpcServiceMethod(
          "unknown", internal::RpcMethod::BIDI_STREAMING,
          new internal::UnknownMethodHandler));
      sync_requests_.emplace_back(
          new SyncRequest(unknown_method_.get(), nullptr));
    }
  }

  void Shutdown() override {
    ThreadManager::Shutdown();
    server_cq_->Shutdown();
  }

  void Wait() override {
    ThreadManager::Wait();
    // Drain any pending items from the queue
    void* tag;
    bool ok;
    while (server_cq_->Next(&tag, &ok)) {
      // Do nothing
    }
  }

  void Start() {
    if (!sync_requests_.empty()) {
      for (auto m = sync_requests_.begin(); m != sync_requests_.end(); m++) {
        (*m)->SetupRequest();
        (*m)->Request(server_->c_server(), server_cq_->cq());
      }

      Initialize();  // ThreadManager's Initialize()
    }
  }

 private:
  Server* server_;
  CompletionQueue* server_cq_;
  int cq_timeout_msec_;
  std::vector<std::unique_ptr<SyncRequest>> sync_requests_;
  std::unique_ptr<internal::RpcServiceMethod> unknown_method_;
  std::unique_ptr<internal::RpcServiceMethod> health_check_;
  std::shared_ptr<Server::GlobalCallbacks> global_callbacks_;
};

static internal::GrpcLibraryInitializer g_gli_initializer;
Server::Server(
    int max_receive_message_size, ChannelArguments* args,
    std::shared_ptr<std::vector<std::unique_ptr<ServerCompletionQueue>>>
        sync_server_cqs,
    int min_pollers, int max_pollers, int sync_cq_timeout_msec)
    : max_receive_message_size_(max_receive_message_size),
      sync_server_cqs_(sync_server_cqs),
      started_(false),
      shutdown_(false),
      shutdown_notified_(false),
      has_generic_service_(false),
      server_(nullptr),
      server_initializer_(new ServerInitializer(this)),
      health_check_service_disabled_(false) {
  g_gli_initializer.summon();
  gpr_once_init(&g_once_init_callbacks, InitGlobalCallbacks);
  global_callbacks_ = g_callbacks;
  global_callbacks_->UpdateArguments(args);

  for (auto it = sync_server_cqs_->begin(); it != sync_server_cqs_->end();
       it++) {
    sync_req_mgrs_.emplace_back(new SyncRequestThreadManager(
        this, (*it).get(), global_callbacks_, min_pollers, max_pollers,
        sync_cq_timeout_msec));
  }

  grpc_channel_args channel_args;
  args->SetChannelArgs(&channel_args);

  for (size_t i = 0; i < channel_args.num_args; i++) {
    if (0 ==
        strcmp(channel_args.args[i].key, kHealthCheckServiceInterfaceArg)) {
      if (channel_args.args[i].value.pointer.p == nullptr) {
        health_check_service_disabled_ = true;
      } else {
        health_check_service_.reset(static_cast<HealthCheckServiceInterface*>(
            channel_args.args[i].value.pointer.p));
      }
      break;
    }
  }

  server_ = grpc_server_create(&channel_args, nullptr);
}

Server::~Server() {
  {
    std::unique_lock<std::mutex> lock(mu_);
    if (started_ && !shutdown_) {
      lock.unlock();
      Shutdown();
    } else if (!started_) {
      // Shutdown the completion queues
      for (auto it = sync_req_mgrs_.begin(); it != sync_req_mgrs_.end(); it++) {
        (*it)->Shutdown();
      }
    }
  }

  grpc_server_destroy(server_);
}

void Server::SetGlobalCallbacks(GlobalCallbacks* callbacks) {
  GPR_ASSERT(!g_callbacks);
  GPR_ASSERT(callbacks);
  g_callbacks.reset(callbacks);
}

grpc_server* Server::c_server() { return server_; }

std::shared_ptr<Channel> Server::InProcessChannel(
    const ChannelArguments& args) {
  grpc_channel_args channel_args = args.c_channel_args();
  return CreateChannelInternal(
      "inproc", grpc_inproc_channel_create(server_, &channel_args, nullptr));
}

static grpc_server_register_method_payload_handling PayloadHandlingForMethod(
    internal::RpcServiceMethod* method) {
  switch (method->method_type()) {
    case internal::RpcMethod::NORMAL_RPC:
    case internal::RpcMethod::SERVER_STREAMING:
      return GRPC_SRM_PAYLOAD_READ_INITIAL_BYTE_BUFFER;
    case internal::RpcMethod::CLIENT_STREAMING:
    case internal::RpcMethod::BIDI_STREAMING:
      return GRPC_SRM_PAYLOAD_NONE;
  }
  GPR_UNREACHABLE_CODE(return GRPC_SRM_PAYLOAD_NONE;);
}

bool Server::RegisterService(const grpc::string* host, Service* service) {
  bool has_async_methods = service->has_async_methods();
  if (has_async_methods) {
    GPR_ASSERT(service->server_ == nullptr &&
               "Can only register an asynchronous service against one server.");
    service->server_ = this;
  }

  const char* method_name = nullptr;
  for (auto it = service->methods_.begin(); it != service->methods_.end();
       ++it) {
    if (it->get() == nullptr) {  // Handled by generic service if any.
      continue;
    }

    internal::RpcServiceMethod* method = it->get();
    void* tag = grpc_server_register_method(
        server_, method->name(), host ? host->c_str() : nullptr,
        PayloadHandlingForMethod(method), 0);
    if (tag == nullptr) {
      gpr_log(GPR_DEBUG, "Attempt to register %s multiple times",
              method->name());
      return false;
    }

    if (method->handler() == nullptr) {  // Async method
      method->set_server_tag(tag);
    } else {
      for (auto it = sync_req_mgrs_.begin(); it != sync_req_mgrs_.end(); it++) {
        (*it)->AddSyncMethod(method, tag);
      }
    }

    method_name = method->name();
  }

  // Parse service name.
  if (method_name != nullptr) {
    std::stringstream ss(method_name);
    grpc::string service_name;
    if (std::getline(ss, service_name, '/') &&
        std::getline(ss, service_name, '/')) {
      services_.push_back(service_name);
    }
  }
  return true;
}

void Server::RegisterAsyncGenericService(AsyncGenericService* service) {
  GPR_ASSERT(service->server_ == nullptr &&
             "Can only register an async generic service against one server.");
  service->server_ = this;
  has_generic_service_ = true;
}

int Server::AddListeningPort(const grpc::string& addr,
                             ServerCredentials* creds) {
  GPR_ASSERT(!started_);
  int port = creds->AddPortToServer(addr, server_);
  global_callbacks_->AddPort(this, addr, creds, port);
  return port;
}

void Server::Start(ServerCompletionQueue** cqs, size_t num_cqs) {
  GPR_ASSERT(!started_);
  global_callbacks_->PreServerStart(this);
  started_ = true;

  // Only create default health check service when user did not provide an
  // explicit one.
  if (health_check_service_ == nullptr && !health_check_service_disabled_ &&
      DefaultHealthCheckServiceEnabled()) {
    if (sync_server_cqs_->empty()) {
      gpr_log(GPR_INFO,
              "Default health check service disabled at async-only server.");
    } else {
      auto* default_hc_service = new DefaultHealthCheckService;
      health_check_service_.reset(default_hc_service);
      RegisterService(nullptr, default_hc_service->GetHealthCheckService());
    }
  }

  grpc_server_start(server_);

  if (!has_generic_service_) {
    for (auto it = sync_req_mgrs_.begin(); it != sync_req_mgrs_.end(); it++) {
      (*it)->AddUnknownSyncMethod();
    }

    for (size_t i = 0; i < num_cqs; i++) {
      if (cqs[i]->IsFrequentlyPolled()) {
        new UnimplementedAsyncRequest(this, cqs[i]);
      }
    }
  }

  for (auto it = sync_req_mgrs_.begin(); it != sync_req_mgrs_.end(); it++) {
    (*it)->Start();
  }
}

void Server::ShutdownInternal(gpr_timespec deadline) {
  std::unique_lock<std::mutex> lock(mu_);
  if (!shutdown_) {
    shutdown_ = true;

    /// The completion queue to use for server shutdown completion notification
    CompletionQueue shutdown_cq;
    ShutdownTag shutdown_tag;  // Dummy shutdown tag
    grpc_server_shutdown_and_notify(server_, shutdown_cq.cq(), &shutdown_tag);

    shutdown_cq.Shutdown();

    void* tag;
    bool ok;
    CompletionQueue::NextStatus status =
        shutdown_cq.AsyncNext(&tag, &ok, deadline);

    // If this timed out, it means we are done with the grace period for a clean
    // shutdown. We should force a shutdown now by cancelling all inflight calls
    if (status == CompletionQueue::NextStatus::TIMEOUT) {
      grpc_server_cancel_all_calls(server_);
    }
    // Else in case of SHUTDOWN or GOT_EVENT, it means that the server has
    // successfully shutdown

    // Shutdown all ThreadManagers. This will try to gracefully stop all the
    // threads in the ThreadManagers (once they process any inflight requests)
    for (auto it = sync_req_mgrs_.begin(); it != sync_req_mgrs_.end(); it++) {
      (*it)->Shutdown();  // ThreadManager's Shutdown()
    }

    // Wait for threads in all ThreadManagers to terminate
    for (auto it = sync_req_mgrs_.begin(); it != sync_req_mgrs_.end(); it++) {
      (*it)->Wait();
    }

    // Drain the shutdown queue (if the previous call to AsyncNext() timed out
    // and we didn't remove the tag from the queue yet)
    while (shutdown_cq.Next(&tag, &ok)) {
      // Nothing to be done here. Just ignore ok and tag values
    }

    shutdown_notified_ = true;
    shutdown_cv_.notify_all();
  }
}

void Server::Wait() {
  std::unique_lock<std::mutex> lock(mu_);
  while (started_ && !shutdown_notified_) {
    shutdown_cv_.wait(lock);
  }
}

void Server::PerformOpsOnCall(internal::CallOpSetInterface* ops,
                              internal::Call* call) {
  static const size_t MAX_OPS = 8;
  size_t nops = 0;
  grpc_op cops[MAX_OPS];
  ops->FillOps(call->call(), cops, &nops);
  auto result = grpc_call_start_batch(call->call(), cops, nops, ops, nullptr);
  if (result != GRPC_CALL_OK) {
    gpr_log(GPR_ERROR, "Fatal: grpc_call_start_batch returned %d", result);
    grpc_call_log_batch(__FILE__, __LINE__, GPR_LOG_SEVERITY_ERROR,
                        call->call(), cops, nops, ops);
    abort();
  }
}

ServerInterface::BaseAsyncRequest::BaseAsyncRequest(
    ServerInterface* server, ServerContext* context,
    internal::ServerAsyncStreamingInterface* stream, CompletionQueue* call_cq,
    void* tag, bool delete_on_finalize)
    : server_(server),
      context_(context),
      stream_(stream),
      call_cq_(call_cq),
      tag_(tag),
      delete_on_finalize_(delete_on_finalize),
      call_(nullptr) {
  call_cq_->RegisterAvalanching();  // This op will trigger more ops
}

ServerInterface::BaseAsyncRequest::~BaseAsyncRequest() {
  call_cq_->CompleteAvalanching();
}

bool ServerInterface::BaseAsyncRequest::FinalizeResult(void** tag,
                                                       bool* status) {
  if (*status) {
    context_->client_metadata_.FillMap();
  }
  context_->set_call(call_);
  context_->cq_ = call_cq_;
  internal::Call call(call_, server_, call_cq_,
                      server_->max_receive_message_size());
  if (*status && call_) {
    context_->BeginCompletionOp(&call);
  }
  // just the pointers inside call are copied here
  stream_->BindCall(&call);
  *tag = tag_;
  if (delete_on_finalize_) {
    delete this;
  }
  return true;
}

ServerInterface::RegisteredAsyncRequest::RegisteredAsyncRequest(
    ServerInterface* server, ServerContext* context,
    internal::ServerAsyncStreamingInterface* stream, CompletionQueue* call_cq,
    void* tag)
    : BaseAsyncRequest(server, context, stream, call_cq, tag, true) {}

void ServerInterface::RegisteredAsyncRequest::IssueRequest(
    void* registered_method, grpc_byte_buffer** payload,
    ServerCompletionQueue* notification_cq) {
  GPR_ASSERT(GRPC_CALL_OK == grpc_server_request_registered_call(
                                 server_->server(), registered_method, &call_,
                                 &context_->deadline_,
                                 context_->client_metadata_.arr(), payload,
                                 call_cq_->cq(), notification_cq->cq(), this));
}

ServerInterface::GenericAsyncRequest::GenericAsyncRequest(
    ServerInterface* server, GenericServerContext* context,
    internal::ServerAsyncStreamingInterface* stream, CompletionQueue* call_cq,
    ServerCompletionQueue* notification_cq, void* tag, bool delete_on_finalize)
    : BaseAsyncRequest(server, context, stream, call_cq, tag,
                       delete_on_finalize) {
  grpc_call_details_init(&call_details_);
  GPR_ASSERT(notification_cq);
  GPR_ASSERT(call_cq);
  GPR_ASSERT(GRPC_CALL_OK == grpc_server_request_call(
                                 server->server(), &call_, &call_details_,
                                 context->client_metadata_.arr(), call_cq->cq(),
                                 notification_cq->cq(), this));
}

bool ServerInterface::GenericAsyncRequest::FinalizeResult(void** tag,
                                                          bool* status) {
  // TODO(yangg) remove the copy here.
  if (*status) {
    static_cast<GenericServerContext*>(context_)->method_ =
        StringFromCopiedSlice(call_details_.method);
    static_cast<GenericServerContext*>(context_)->host_ =
        StringFromCopiedSlice(call_details_.host);
    context_->deadline_ = call_details_.deadline;
  }
  grpc_slice_unref(call_details_.method);
  grpc_slice_unref(call_details_.host);
  return BaseAsyncRequest::FinalizeResult(tag, status);
}

bool Server::UnimplementedAsyncRequest::FinalizeResult(void** tag,
                                                       bool* status) {
  if (GenericAsyncRequest::FinalizeResult(tag, status) && *status) {
    new UnimplementedAsyncRequest(server_, cq_);
    new UnimplementedAsyncResponse(this);
  } else {
    delete this;
  }
  return false;
}

Server::UnimplementedAsyncResponse::UnimplementedAsyncResponse(
    UnimplementedAsyncRequest* request)
    : request_(request) {
  Status status(StatusCode::UNIMPLEMENTED, "");
  internal::UnknownMethodHandler::FillOps(request_->context(), this);
  request_->stream()->call_.PerformOps(this);
}

ServerInitializer* Server::initializer() { return server_initializer_.get(); }

}  // namespace grpc

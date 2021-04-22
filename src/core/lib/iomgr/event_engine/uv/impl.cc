#include <uv.h>
#include <functional>
#include <future>

#include "grpc/event_engine/event_engine.h"
#include "src/core/lib/gprpp/atomic.h"
#include "src/core/lib/gprpp/mpscq.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/thd.h"
#include "src/core/lib/iomgr/socket_utils.h"

class uvEngine;

class uvTCP final {
 public:
  uvTCP(uvEngine* engine,
        grpc_event_engine::experimental::EventEngine::Listener::AcceptCallback
            on_accept,
        grpc_event_engine::experimental::EventEngine::Callback on_shutdown,
        const grpc_event_engine::experimental::ChannelArgs& args,
        grpc_event_engine::experimental::SliceAllocatorFactory
            slice_allocator_factory)
      : engine_(engine),
        on_accept_(on_accept),
        on_shutdown_(on_shutdown),
        slice_allocator_factory_(std::move(slice_allocator_factory)) {
    tcp_.data = this;
  }
  uvEngine* engine_;
  grpc_event_engine::experimental::EventEngine::Listener::AcceptCallback
      on_accept_;
  grpc_event_engine::experimental::EventEngine::Callback on_shutdown_;
  grpc_event_engine::experimental::SliceAllocatorFactory
      slice_allocator_factory_;
  uv_tcp_t tcp_;
};

class uvListener final
    : public grpc_event_engine::experimental::EventEngine::Listener {
 public:
  uvListener(uvEngine* engine, Listener::AcceptCallback on_accept,
             grpc_event_engine::experimental::EventEngine::Callback on_shutdown,
             const grpc_event_engine::experimental::ChannelArgs& args,
             grpc_event_engine::experimental::SliceAllocatorFactory
                 slice_allocator_factory);
  virtual ~uvListener();
  virtual absl::StatusOr<int> Bind(
      const grpc_event_engine::experimental::EventEngine::ResolvedAddress& addr)
      override final;
  virtual absl::Status Start() override final;
  uvEngine* engine_;
  uvTCP* uvTCP_ = nullptr;
};

class uvDNSResolver final
    : public grpc_event_engine::experimental::EventEngine::DNSResolver {
 public:
  virtual ~uvDNSResolver() override final {}
  uvDNSResolver(uvEngine* engine) : engine_(engine) {}
  virtual LookupTaskHandle LookupHostname(LookupHostnameCallback on_resolve,
                                          absl::string_view address,
                                          absl::string_view default_port,
                                          absl::Time deadline) override final {}
  virtual LookupTaskHandle LookupSRV(LookupSRVCallback on_resolve,
                                     absl::string_view name,
                                     absl::Time deadline) override final {}
  virtual LookupTaskHandle LookupTXT(LookupTXTCallback on_resolve,
                                     absl::string_view name,
                                     absl::Time deadline) override final {}
  virtual void TryCancelLookup(LookupTaskHandle handle) override final {}
  uvEngine* engine_;
};

class uvEndpoint final
    : public grpc_event_engine::experimental::EventEngine::Endpoint {
 public:
  virtual ~uvEndpoint() override final {}
  virtual void Read(
      grpc_event_engine::experimental::EventEngine::Callback on_read,
      grpc_event_engine::experimental::SliceBuffer* buffer,
      absl::Time deadline) override final {}
  virtual void Write(
      grpc_event_engine::experimental::EventEngine::Callback on_writable,
      grpc_event_engine::experimental::SliceBuffer* data,
      absl::Time deadline) override final {}
  virtual void Close(grpc_event_engine::experimental::EventEngine::Callback
                         on_close) override final {}
  virtual const grpc_event_engine::experimental::EventEngine::ResolvedAddress*
  GetPeerAddress() const override final{};
  virtual const grpc_event_engine::experimental::EventEngine::ResolvedAddress*
  GetLocalAddress() const override final{};
};

struct schedulingRequest : grpc_core::MultiProducerSingleConsumerQueue::Node {
  typedef std::function<void(uvEngine*)> functor;
  schedulingRequest(functor&& f) : f_(std::move(f)) {}
  functor f_;
};

class uvEngine final : public grpc_event_engine::experimental::EventEngine {
 public:
  void schedule(schedulingRequest::functor&& f) {
    schedulingRequest* request = new schedulingRequest(std::move(f));
    queue_.Push(request);
    uv_async_send(&kicker_);
  }
  static void threadBodyTrampoline(void* arg) {
    uvEngine* engine = reinterpret_cast<uvEngine*>(arg);
    engine->threadBody();
  }
  static void kickerTrampoline(uv_async_t* async) {
    uvEngine* engine = reinterpret_cast<uvEngine*>(async->loop->data);
    engine->kicker();
  }
  void kicker() {
    bool empty = false;
    while (!empty) {
      schedulingRequest* node =
          reinterpret_cast<schedulingRequest*>(queue_.PopAndCheckEnd(&empty));
      node->f_(this);
      delete node;
    }
  }
  static void closeCallback(uv_handle_t* handle) {
    uvTCP* tcp = reinterpret_cast<uvTCP*>(handle->data);
    delete tcp;
  }
  void threadBody() {
    uv_loop_init(&loop_);
    loop_.data = this;
    uv_async_init(&loop_, &kicker_, kickerTrampoline);
    running_.Store(true, grpc_core::MemoryOrder::RELAXED);
    while (uv_run(&loop_, UV_RUN_DEFAULT) != 0)
      ;
  }
  uvEngine() {
    bool success = false;
    thread_ =
        grpc_core::Thread("uv loop", threadBodyTrampoline, this, &success);
    GPR_ASSERT(success);
    while (!running_.Load(grpc_core::MemoryOrder::RELAXED))
      ;
  }
  virtual absl::StatusOr<std::unique_ptr<Listener>> CreateListener(
      Listener::AcceptCallback on_accept, Callback on_shutdown,
      const grpc_event_engine::experimental::ChannelArgs& args,
      grpc_event_engine::experimental::SliceAllocatorFactory
          slice_allocator_factory) override final {
    return absl::make_unique<uvListener>(this, on_accept, on_shutdown, args,
                                         std::move(slice_allocator_factory));
  }

  virtual absl::Status Connect(
      OnConnectCallback on_connect, const ResolvedAddress& addr,
      const grpc_event_engine::experimental::ChannelArgs& args,
      grpc_event_engine::experimental::SliceAllocator slice_allocator,
      absl::Time deadline) override final {
    return absl::OkStatus();
  }

  virtual ~uvEngine() override final {}

  virtual absl::StatusOr<std::unique_ptr<
      grpc_event_engine::experimental::EventEngine::DNSResolver>>
  GetDNSResolver() override final {
    return absl::make_unique<uvDNSResolver>(this);
  }

  virtual TaskHandle Run(Callback fn, RunOptions opts) override final {}
  virtual TaskHandle RunAt(absl::Time when, Callback fn,
                           RunOptions opts) override final {}
  virtual void TryCancel(TaskHandle handle) override final {}
  virtual absl::Status Shutdown() override final {
    thread_.Join();
    return absl::OkStatus();
  }

  uv_loop_t loop_;
  uv_async_t kicker_;
  grpc_core::Thread thread_;
  grpc_core::Atomic<bool> running_;
  grpc_core::MultiProducerSingleConsumerQueue queue_;
};

uvListener::uvListener(
    uvEngine* engine, Listener::AcceptCallback on_accept,
    grpc_event_engine::experimental::EventEngine::Callback on_shutdown,
    const grpc_event_engine::experimental::ChannelArgs& args,
    grpc_event_engine::experimental::SliceAllocatorFactory
        slice_allocator_factory)
    : engine_(engine),
      uvTCP_(new uvTCP(engine, on_accept, on_shutdown, args,
                       std::move(slice_allocator_factory))) {
  engine->schedule(
      [this](uvEngine* engine) { uv_tcp_init(&engine->loop_, &uvTCP_->tcp_); });
}

uvListener::~uvListener() {
  engine_->schedule([this](uvEngine* engine) {
    uv_close(reinterpret_cast<uv_handle_t*>(&uvTCP_->tcp_),
             uvEngine::closeCallback);
  });
}

absl::StatusOr<int> uvListener::Bind(
    const grpc_event_engine::experimental::EventEngine::ResolvedAddress& addr) {
  std::promise<absl::StatusOr<int>> p;
  engine_->schedule([this, &p, &addr](uvEngine* engine) {
    int r = uv_tcp_bind(&uvTCP_->tcp_, addr.address(), 0 /* flags */);
    switch (r) {
      case UV_EINVAL:
        p.set_value(absl::Status(
            absl::StatusCode::kInvalidArgument,
            "uv_tcp_bind said we passed an invalid argument to it"));
        return;
      case 0:
        break;
      default:
        p.set_value(absl::Status(
            absl::StatusCode::kUnknown,
            "uv_tcp_bind returned an error code we don't know about"));
        return;
    }
    sockaddr_storage boundAddr;
    int addrLen = sizeof(boundAddr);
    r = uv_tcp_getsockname(&uvTCP_->tcp_,
                           reinterpret_cast<sockaddr*>(&boundAddr), &addrLen);
    switch (r) {
      case UV_EINVAL:
        p.set_value(absl::Status(
            absl::StatusCode::kInvalidArgument,
            "uv_tcp_getsockname said we passed an invalid argument to it"));
        return;
      case 0:
        break;
      default:
        p.set_value(absl::Status(
            absl::StatusCode::kUnknown,
            "uv_tcp_getsockname returned an error code we don't know about"));
        return;
    }
    switch (boundAddr.ss_family) {
      case AF_INET: {
        sockaddr_in* sin = (sockaddr_in*)&boundAddr;
        p.set_value(grpc_htons(sin->sin_port));
        break;
      }
      case AF_INET6: {
        sockaddr_in6* sin6 = (sockaddr_in6*)&boundAddr;
        p.set_value(grpc_htons(sin6->sin6_port));
        break;
      }
      default:
        p.set_value(absl::Status(
            absl::StatusCode::kInvalidArgument,
            "returned socket address in :Bind is neither IPv4 nor IPv6"));
        break;
    }
  });
  return p.get_future().get();
}

absl::Status uvListener::Start() {
  engine_->schedule([](uvEngine* engine) {});
  return absl::OkStatus();
}

absl::Status uvEngineRegister() {
  return grpc_event_engine::experimental::SetDefaultEventEngine(
      std::make_shared<uvEngine>());
}

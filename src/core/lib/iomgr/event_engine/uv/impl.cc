#include <uv.h>

#include <atomic>
#include <functional>
#include <future>
#include <unordered_map>

#include "absl/strings/str_format.h"

#include "grpc/event_engine/event_engine.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/gprpp/mpscq.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/thd.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/socket_utils.h"

extern grpc_core::TraceFlag grpc_tcp_trace;

static void hexdump(const std::string& prefix, const uint8_t* data,
                    size_t size) {
  char ascii[17];
  ascii[16] = 0;
  for (unsigned p = 0; p < 16; p++) ascii[p] = ' ';
  std::string line;
  for (size_t i = 0; i < size; i++) {
    uint8_t d = data[i];
    if (i % 16 == 0) {
      line = prefix + absl::StrFormat(" %08x  |", i);
    }
    line += absl::StrFormat("%02X ", d);
    ascii[i % 16] = isprint(d) ? d : '.';
    size_t n = i + 1;
    if (((n % 8) == 0) || (n == size)) {
      line += " ";
      if (((n % 16) != 0) || (n != size)) continue;
      if (n == size) {
        n %= 16;
        for (unsigned p = n; p < 16; p++) line += "   ";
      }
      gpr_log(GPR_DEBUG, "%s|   %s", line.c_str(), ascii);
    }
  }
}

class uvEngine;
class uvTask;
class uvLookupTask;

// the base class to hold libuv socket information for both listeners and
// endpoints. it holds the uv_tcp_t object as well as the uvCloseCB counter,
// that allows it to self-delete once libuv is done with its object.
class uvTCPbase {
 public:
  uvTCPbase() = default;
  virtual ~uvTCPbase() = default;
  static void uvCloseCB(uv_handle_t* handle) {
    uvTCPbase* tcp = reinterpret_cast<uvTCPbase*>(handle->data);
    if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
      gpr_log(GPR_DEBUG, "EE::UV::uvTCPbase:%p close CB, callbacks pending: %i",
              tcp, tcp->to_close_ - 1);
    }
    if (--tcp->to_close_ == 0) {
      delete tcp;
    }
  }

  int init(uvEngine* engine);

  uv_tcp_t tcp_;
  int to_close_ = 0;
};

class uvTCPlistener final : public uvTCPbase {
 public:
  uvTCPlistener(
      grpc_event_engine::experimental::EventEngine::Listener::AcceptCallback
          on_accept,
      grpc_event_engine::experimental::EventEngine::Callback on_shutdown,
      const grpc_event_engine::experimental::ChannelArgs& args,
      grpc_event_engine::experimental::SliceAllocatorFactory
          slice_allocator_factory)
      : on_accept_(std::move(on_accept)),
        on_shutdown_(std::move(on_shutdown)),
        args_(args),
        slice_allocator_factory_(std::move(slice_allocator_factory)) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
      gpr_log(GPR_DEBUG, "EE::UV::uvTCPlistener:%p created", this);
    }
  }
  virtual ~uvTCPlistener() = default;
  grpc_event_engine::experimental::EventEngine::Listener::AcceptCallback
      on_accept_;
  grpc_event_engine::experimental::EventEngine::Callback on_shutdown_;
  grpc_event_engine::experimental::ChannelArgs args_;
  grpc_event_engine::experimental::SliceAllocatorFactory
      slice_allocator_factory_;
};

class uvTCP final : public uvTCPbase {
 public:
  uvTCP(const grpc_event_engine::experimental::ChannelArgs& args,
        grpc_event_engine::experimental::SliceAllocator slice_allocator)
      : args_(args), slice_allocator_(std::move(slice_allocator)) {}
  virtual ~uvTCP() = default;

  int init(uvEngine* engine);
  uv_timer_t write_timer_;
  uv_timer_t read_timer_;
  const grpc_event_engine::experimental::ChannelArgs args_;
  grpc_event_engine::experimental::SliceAllocator slice_allocator_;
  uv_write_t write_req_;
  uv_buf_t* write_bufs_ = nullptr;
  size_t write_bufs_count_ = 0;
  // temporary until the slice allocator is complete
  uint8_t read_buf_[4096];
  grpc_event_engine::experimental::SliceBuffer* read_sb_;
  grpc_event_engine::experimental::EventEngine::Callback on_writable_;
  grpc_event_engine::experimental::EventEngine::Callback on_read_;
  grpc_event_engine::experimental::EventEngine::ResolvedAddress peer_address_;
  grpc_event_engine::experimental::EventEngine::ResolvedAddress local_address_;
};

// the listener class; only holds a pointer to a uvTCPlistener and nothing else
class uvListener final
    : public grpc_event_engine::experimental::EventEngine::Listener {
 public:
  uvListener(Listener::AcceptCallback on_accept,
             grpc_event_engine::experimental::EventEngine::Callback on_shutdown,
             const grpc_event_engine::experimental::ChannelArgs& args,
             grpc_event_engine::experimental::SliceAllocatorFactory
                 slice_allocator_factory);

  virtual ~uvListener();

  uvEngine* getEngine() {
    return reinterpret_cast<uvEngine*>(uvTCP_->tcp_.loop->data);
  }
  int init(uvEngine* engine) { return uvTCP_->init(engine); }

  virtual absl::StatusOr<int> Bind(
      const grpc_event_engine::experimental::EventEngine::ResolvedAddress& addr)
      override final;

  virtual absl::Status Start() override final;

  uvTCPlistener* uvTCP_ = nullptr;
};

// this class is only a tiny temporary shell around the engine itself,
// and doesn't hold any state of its own
class uvDNSResolver final
    : public grpc_event_engine::experimental::EventEngine::DNSResolver {
 public:
  virtual ~uvDNSResolver() override final {}

  uvDNSResolver(uvEngine* engine) : engine_(engine) {}

  virtual LookupTaskHandle LookupHostname(LookupHostnameCallback on_resolve,
                                          absl::string_view address,
                                          absl::string_view default_port,
                                          absl::Time deadline) override final;

  virtual LookupTaskHandle LookupSRV(LookupSRVCallback on_resolve,
                                     absl::string_view name,
                                     absl::Time deadline) override final {
    abort();
  }

  virtual LookupTaskHandle LookupTXT(LookupTXTCallback on_resolve,
                                     absl::string_view name,
                                     absl::Time deadline) override final {
    abort();
  }

  virtual void TryCancelLookup(LookupTaskHandle handle) override final;

  uvEngine* engine_;
};

// the uv endpoint, a bit like the listener, is the wrapper around uvTCP,
// and doesn't have anything of its own beyond the uvTCP pointer, and
// the connect request data; the connect request data will be used
// before it's converted into a unique_ptr so it's fine to be stored
// there instead of the uvTCP class
class uvEndpoint final
    : public grpc_event_engine::experimental::EventEngine::Endpoint {
 public:
  uvEndpoint(const grpc_event_engine::experimental::ChannelArgs& args,
             grpc_event_engine::experimental::SliceAllocator slice_allocator)
      : uvTCP_(new uvTCP(args, std::move(slice_allocator))) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
      gpr_log(GPR_DEBUG, "EE::UV::uvEndpoint:%p created", this);
    }
  }
  virtual ~uvEndpoint() override final;
  virtual void* GetResourceUser() override final {
    return uvTCP_->slice_allocator_.GetResourceUser();
  }
  int init(uvEngine* engine);
  int populateAddressesUnsafe() {
    auto populate =
        [this](
            std::function<int(const uv_tcp_t*, struct sockaddr*, int*)> f,
            grpc_event_engine::experimental::EventEngine::ResolvedAddress* a) {
          int namelen;
          sockaddr_storage addr;
          int ret =
              f(&uvTCP_->tcp_, reinterpret_cast<sockaddr*>(&addr), &namelen);
          *a = grpc_event_engine::experimental::EventEngine::ResolvedAddress(
              reinterpret_cast<sockaddr*>(&addr), namelen);
          return ret;
        };
    int r = 0;
    r |= populate(uv_tcp_getsockname, &uvTCP_->local_address_);
    r |= populate(uv_tcp_getpeername, &uvTCP_->peer_address_);
    if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
      gpr_log(GPR_DEBUG, "EE::UV::uvEndpoint@%p::populateAddresses, r=%d", this,
              r);
    }
    return r;
  }

  virtual void Read(
      grpc_event_engine::experimental::EventEngine::Callback on_read,
      grpc_event_engine::experimental::SliceBuffer* buffer,
      absl::Time deadline) override final;

  virtual void Write(
      grpc_event_engine::experimental::EventEngine::Callback on_writable,
      grpc_event_engine::experimental::SliceBuffer* data,
      absl::Time deadline) override final;

  virtual const grpc_event_engine::experimental::EventEngine::ResolvedAddress*
  GetPeerAddress() const override final {
    return &uvTCP_->peer_address_;
  };

  virtual const grpc_event_engine::experimental::EventEngine::ResolvedAddress*
  GetLocalAddress() const override final {
    return &uvTCP_->local_address_;
  };

  absl::Status Connect(
      uvEngine* engine,
      grpc_event_engine::experimental::EventEngine::OnConnectCallback
          on_connect,
      const grpc_event_engine::experimental::EventEngine::ResolvedAddress&
          addr);

  uvEngine* getEngine() {
    return reinterpret_cast<uvEngine*>(uvTCP_->tcp_.loop->data);
  }

  uvTCP* uvTCP_ = nullptr;
  uv_connect_t connect_;
  grpc_event_engine::experimental::EventEngine::OnConnectCallback on_connect_ =
      nullptr;
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

  void kicker() {
    bool empty = false;
    while (!empty) {
      schedulingRequest* node =
          reinterpret_cast<schedulingRequest*>(queue_.PopAndCheckEnd(&empty));
      if (!node) continue;
      schedulingRequest::functor f = std::move(node->f_);
      delete node;
      f(this);
    }
  }

  void thread() {
    int r = 0;
    r = uv_loop_init(&loop_);
    loop_.data = this;
    r |= uv_async_init(&loop_, &kicker_, [](uv_async_t* async) {
      uvEngine* engine = reinterpret_cast<uvEngine*>(async->loop->data);
      engine->kicker();
    });
    if (r != 0) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
        gpr_log(GPR_ERROR, "EE::UV::uvEngine::thread@%p, failed to start: %i",
                this, r);
      }
      ready_.set_value(false);
      return;
    }
    ready_.set_value(true);
    grpc_core::ExecCtx ctx;
    while (uv_run(&loop_, UV_RUN_ONCE) != 0) {
      ctx.Flush();
    }
    shutdown_ = true;
    if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
      gpr_log(GPR_DEBUG, "EE::UV::uvEngine::thread@%p, shutting down", this);
    }
    on_shutdown_complete_(absl::OkStatus());
  }

  uvEngine() {
    bool success = false;
    if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
      gpr_log(GPR_DEBUG, "EE::UV::uvEngine:%p created", this);
    }
    grpc_core::Thread::Options options;
    options.set_joinable(false);
    thread_ = grpc_core::Thread(
        "uv loop",
        [](void* arg) {
          uvEngine* engine = reinterpret_cast<uvEngine*>(arg);
          engine->thread();
        },
        this, &success, options);
    thread_.Start();
    GPR_ASSERT(success);
    success = ready_.get_future().get();
    GPR_ASSERT(success);
  }

  virtual absl::StatusOr<std::unique_ptr<Listener>> CreateListener(
      Listener::AcceptCallback on_accept, Callback on_shutdown,
      const grpc_event_engine::experimental::ChannelArgs& args,
      grpc_event_engine::experimental::SliceAllocatorFactory
          slice_allocator_factory) override final {
    std::unique_ptr<uvListener> ret = absl::make_unique<uvListener>(
        on_accept, on_shutdown, args, std::move(slice_allocator_factory));
    if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
      gpr_log(GPR_DEBUG, "EE::UV::uvEngine::CreateListener@%p, created %p",
              this, ret.get());
    }
    ret->init(this);
    return std::move(ret);
  }

  virtual absl::Status Connect(
      OnConnectCallback on_connect, const ResolvedAddress& addr,
      const grpc_event_engine::experimental::ChannelArgs& args,
      grpc_event_engine::experimental::SliceAllocator slice_allocator,
      absl::Time deadline) override final {
    uvEndpoint* e = new uvEndpoint(args, std::move(slice_allocator));
    if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
      gpr_log(GPR_DEBUG, "EE::UV::uvEngine::Connect@%p, created %p", this, e);
    }
    return e->Connect(this, std::move(on_connect), addr);
  }

  virtual ~uvEngine() override final { GPR_ASSERT(shutdown_); }

  virtual absl::StatusOr<std::unique_ptr<
      grpc_event_engine::experimental::EventEngine::DNSResolver>>
  GetDNSResolver() override final {
    return absl::make_unique<uvDNSResolver>(this);
  }

  virtual TaskHandle Run(Callback fn, RunOptions opts) override final;
  virtual TaskHandle RunAt(absl::Time when, Callback fn,
                           RunOptions opts) override final;
  virtual void TryCancel(TaskHandle handle) override final;

  virtual void Shutdown(Callback on_shutdown_complete) override final {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
      gpr_log(GPR_DEBUG, "EE::UV::uvEngine::Shutdown@%p", this);
    }
    on_shutdown_complete_ = on_shutdown_complete;
    schedule([](uvEngine* engine) {
      uv_unref(reinterpret_cast<uv_handle_t*>(&engine->kicker_));
    });
  }

  void eraseTask(intptr_t taskKey);
  void eraseLookupTask(intptr_t taskKey);

  uv_loop_t loop_;
  uv_async_t kicker_;
  std::promise<bool> ready_;
  grpc_core::Thread thread_;
  grpc_core::MultiProducerSingleConsumerQueue queue_;
  std::atomic<intptr_t> taskKey_;
  std::atomic<intptr_t> lookupTaskKey_;
  std::unordered_map<intptr_t, uvTask*> taskMap_;
  std::unordered_map<intptr_t, uvLookupTask*> lookupTaskMap_;
  grpc_event_engine::experimental::EventEngine::Callback on_shutdown_complete_;
  bool shutdown_ = false;
};

absl::Status uvEndpoint::Connect(
    uvEngine* engine,
    grpc_event_engine::experimental::EventEngine::OnConnectCallback on_connect,
    const grpc_event_engine::experimental::EventEngine::ResolvedAddress& addr) {
  on_connect_ = on_connect;
  engine->schedule([addr, this](uvEngine* engine) {
    int r;
    r = init(engine);
    r = uv_tcp_connect(
        &connect_, &uvTCP_->tcp_, addr.address(),
        [](uv_connect_t* req, int status) {
          uvEndpoint* epRaw = reinterpret_cast<uvEndpoint*>(req->data);
          std::unique_ptr<uvEndpoint> ep(epRaw);
          auto on_connect = std::move(ep->on_connect_);
          if (status == 0) {
            ep->populateAddressesUnsafe();
            if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
              gpr_log(GPR_DEBUG, "EE::UV::uvEndpoint::Connect@%p, success",
                      epRaw);
            }
            on_connect(std::move(ep));
          } else {
            if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
              gpr_log(GPR_INFO, "EE::UV::uvEndpoint::Connect@%p, failed: %i",
                      epRaw, status);
            }
            on_connect(absl::UnknownError(
                "uv_tcp_connect gave us an asynchronous error"));
          }
        });
    if (r != 0) {
      auto on_connect = std::move(on_connect_);
      if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
        gpr_log(GPR_INFO, "EE::UV::uvEndpoint::Connect@%p, failed: %i", this,
                r);
      }
      delete this;
      on_connect(absl::UnknownError("uv_tcp_connect gave us an error"));
    }
  });
  return absl::OkStatus();
}

int uvTCPbase::init(uvEngine* engine) {
  tcp_.data = this;
  return uv_tcp_init(&engine->loop_, &tcp_);
}

int uvTCP::init(uvEngine* engine) {
  uvTCPbase::init(engine);
  write_req_.data = this;
  write_timer_.data = this;
  read_timer_.data = this;
  uv_timer_init(&engine->loop_, &write_timer_);
  uv_timer_init(&engine->loop_, &read_timer_);
  return 0;
}

uvListener::uvListener(
    Listener::AcceptCallback on_accept,
    grpc_event_engine::experimental::EventEngine::Callback on_shutdown,
    const grpc_event_engine::experimental::ChannelArgs& args,
    grpc_event_engine::experimental::SliceAllocatorFactory
        slice_allocator_factory)
    : uvTCP_(new uvTCPlistener(on_accept, on_shutdown, args,
                               std::move(slice_allocator_factory))) {
  uvTCP_->tcp_.data = uvTCP_;
}

uvListener::~uvListener() {
  uvTCPbase* tcp = uvTCP_;
  getEngine()->schedule([tcp](uvEngine* engine) {
    tcp->tcp_.data = tcp;
    tcp->to_close_ = 1;
    uv_close(reinterpret_cast<uv_handle_t*>(&tcp->tcp_), uvTCPbase::uvCloseCB);
  });
}

absl::StatusOr<int> uvListener::Bind(
    const grpc_event_engine::experimental::EventEngine::ResolvedAddress& addr) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
    grpc_resolved_address grpcaddr;
    grpcaddr.len = addr.size();
    memcpy(grpcaddr.addr, addr.address(), grpcaddr.len);
    gpr_log(GPR_DEBUG, "EE::UV::uvListener::Bind@%p to %s", this,
            grpc_sockaddr_to_uri(&grpcaddr).c_str());
  }
  std::promise<absl::StatusOr<int>> p;
  getEngine()->schedule([this, &p, &addr](uvEngine* engine) {
    int r = uv_tcp_bind(&uvTCP_->tcp_, addr.address(), 0 /* flags */);
    switch (r) {
      case UV_EINVAL:
        p.set_value(absl::InvalidArgumentError(
            "uv_tcp_bind said we passed an invalid argument to it"));
        return;
      case 0:
        break;
      default:
        if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
          gpr_log(GPR_INFO,
                  "EE::UV::uvListener::Bind@%p, uv_tcp_bind failed: %i", this,
                  r);
        }
        p.set_value(absl::InvalidArgumentError(
            "uv_tcp_bind returned an error code we don't know about"));
        return;
    }
    sockaddr_storage boundAddr;
    int addrLen = sizeof(boundAddr);
    r = uv_tcp_getsockname(&uvTCP_->tcp_,
                           reinterpret_cast<sockaddr*>(&boundAddr), &addrLen);
    switch (r) {
      case UV_EINVAL:
        p.set_value(absl::InvalidArgumentError(
            "uv_tcp_getsockname said we passed an invalid argument to it"));
        return;
      case 0:
        break;
      default:
        if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
          gpr_log(GPR_INFO,
                  "EE::UV::uvListener::Bind@%p, uv_tcp_getsockname failed: %i",
                  this, r);
        }
        p.set_value(absl::UnknownError(
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
        if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
          gpr_log(GPR_INFO,
                  "EE::UV::uvListener::Bind@%p, unknown addr family: %i", this,
                  boundAddr.ss_family);
        }
        p.set_value(absl::InvalidArgumentError(
            "returned socket address in :Bind is neither IPv4 nor IPv6"));
        return;
    }
    if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
      gpr_log(GPR_DEBUG, "EE::UV::uvListener::Bind@%p, success", this);
    }
  });
  return p.get_future().get();
}

absl::Status uvListener::Start() {
  std::promise<absl::Status> ret;
  getEngine()->schedule([this, &ret](uvEngine* engine) {
    int r;
    r = uv_listen(
        reinterpret_cast<uv_stream_t*>(&uvTCP_->tcp_), 42,
        [](uv_stream_t* server, int status) {
          if (status < 0) return;
          uvTCPlistener* l = static_cast<uvTCPlistener*>(server->data);
          std::unique_ptr<uvEndpoint> e = absl::make_unique<uvEndpoint>(
              l->args_,
              l->slice_allocator_factory_.CreateSliceAllocator("foo"));
          uvEngine* engine = static_cast<uvEngine*>(server->loop->data);
          e->init(engine);
          int r;
          r = uv_accept(server,
                        reinterpret_cast<uv_stream_t*>(&e->uvTCP_->tcp_));
          if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
            gpr_log(
                GPR_DEBUG,
                "EE::UV::uvListener::Start@%p, accepting new connection: %i", l,
                r);
          }
          if (r == 0) {
            e->populateAddressesUnsafe();
            l->on_accept_(std::move(e));
          }
        });
    if (r == 0) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
        gpr_log(GPR_DEBUG, "EE::UV::uvListener::Start@%p, success", this);
      }
      ret.set_value(absl::OkStatus());
    } else {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
        gpr_log(GPR_INFO, "EE::UV::uvListener::Start@%p, failure: %i", this, r);
      }
      ret.set_value(absl::UnknownError(
          "uv_listen returned an error code we don't know about"));
    }
  });
  auto status = ret.get_future().get();
  // until we can handle "SO_REUSEPORT", always return OkStatus.
  // return status;
  return absl::OkStatus();
}

std::shared_ptr<grpc_event_engine::experimental::EventEngine>
grpc_event_engine::experimental::DefaultEventEngineFactory() {
  return std::make_shared<uvEngine>();
}

class uvTask {
 public:
  uvTask(uvEngine* engine) : key_(engine->taskKey_.fetch_add(1)) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
      gpr_log(GPR_DEBUG, "EE::UV::uvTask@%p, created: key = %" PRIiPTR, this,
              key_);
    }
    timer_.data = this;
  }
  grpc_event_engine::experimental::EventEngine::Callback fn_;
  uv_timer_t timer_;
  const intptr_t key_;
  void cancel() {
    uv_timer_stop(&timer_);
    uv_close(reinterpret_cast<uv_handle_t*>(&timer_), [](uv_handle_t* handle) {
      uv_timer_t* timer = reinterpret_cast<uv_timer_t*>(handle);
      uvTask* task = reinterpret_cast<uvTask*>(timer->data);
      uvEngine* engine = reinterpret_cast<uvEngine*>(timer->loop->data);
      engine->eraseTask(task->key_);
    });
  }
};

uvEngine::TaskHandle uvEngine::Run(Callback fn, RunOptions opts) {
  return RunAt(absl::Now(), fn, opts);
}

uvEngine::TaskHandle uvEngine::RunAt(absl::Time when, Callback fn,
                                     RunOptions opts) {
  uvTask* task = new uvTask(this);
  task->fn_ = std::move(fn);
  schedule([task, when](uvEngine* engine) {
    intptr_t key = task->key_;
    engine->taskMap_[key] = task;
    uv_timer_init(&engine->loop_, &task->timer_);
    absl::Duration timeout = when - absl::Now();
    uv_timer_start(
        &task->timer_,
        [](uv_timer_t* timer) {
          uvTask* task = reinterpret_cast<uvTask*>(timer->data);
          if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
            gpr_log(GPR_DEBUG, "EE::UV::uvTask@%p, triggered: key = %" PRIiPTR,
                    task, task->key_);
          }
          task->cancel();
          task->fn_(absl::OkStatus());
        },
        timeout / absl::Milliseconds(1), 0);
  });

  return {task->key_};
}
void uvEngine::TryCancel(TaskHandle handle) {
  schedule([handle](uvEngine* engine) {
    auto it = engine->taskMap_.find(handle.key);
    if (it == engine->taskMap_.end()) return;
    if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
      gpr_log(GPR_DEBUG, "EE::UV::uvTask@%p, cancelled: key = %" PRIiPTR,
              it->second, it->second->key_);
    }
    it->second->cancel();
  });
}

class uvLookupTask {
 public:
  uvLookupTask(uvEngine* engine) : key_(engine->lookupTaskKey_.fetch_add(1)) {
    req_.data = this;
    timer_.data = this;
  }
  std::string address_;
  std::string default_port_;
  uv_getaddrinfo_t req_;
  uv_timer_t timer_;
  bool cancelled_ = false;
  const intptr_t key_;
  grpc_event_engine::experimental::EventEngine::DNSResolver::
      LookupHostnameCallback on_resolve_;
  static void uvResolverCallbackTrampoline(uv_getaddrinfo_t* req, int status,
                                           struct addrinfo* res) {
    uvLookupTask* task = reinterpret_cast<uvLookupTask*>(req->data);
    task->uvResolverCallback(status, res);
  }
  void uvResolverCallback(int status, struct addrinfo* res) {
    uvEngine* engine = reinterpret_cast<uvEngine*>(req_.loop->data);
    uv_timer_stop(&timer_);
    if (cancelled_ == 1) {
      uv_freeaddrinfo(res);
      engine->eraseLookupTask(key_);
      return;
    }
    struct addrinfo* p;
    std::vector<grpc_event_engine::experimental::EventEngine::ResolvedAddress>
        ret;

    for (p = res; p != nullptr; p = p->ai_next) {
      ret.emplace_back(p->ai_addr, p->ai_addrlen);
    }

    uv_freeaddrinfo(res);
    uv_close(reinterpret_cast<uv_handle_t*>(&timer_), [](uv_handle_t* timer) {
      uvLookupTask* task = reinterpret_cast<uvLookupTask*>(timer->data);
      uvEngine* engine = reinterpret_cast<uvEngine*>(timer->loop->data);
      engine->eraseLookupTask(task->key_);
    });
    if (status == UV_ECANCELED) {
      on_resolve_(absl::CancelledError("Cancelled"));
    } else {
      on_resolve_(std::move(ret));
    }
  }
  static void uvTimerCallback(uv_timer_t* timer) {
    uvLookupTask* task = reinterpret_cast<uvLookupTask*>(timer->data);
    task->cancelled_ = true;
  }
  void cancel(uvEngine* engine) {
    cancelled_ = true;
    uv_timer_stop(&timer_);
    uv_cancel(reinterpret_cast<uv_req_t*>(&req_));
  }
};

grpc_event_engine::experimental::EventEngine::DNSResolver::LookupTaskHandle
uvDNSResolver::LookupHostname(LookupHostnameCallback on_resolve,
                              absl::string_view address,
                              absl::string_view default_port,
                              absl::Time deadline) {
  uvLookupTask* task = new uvLookupTask(engine_);
  task->on_resolve_ = std::move(on_resolve);
  if (!grpc_core::SplitHostPort(address, &task->address_,
                                &task->default_port_)) {
    task->address_ = std::string(address);
    task->default_port_ = std::string(default_port);
  }
  engine_->schedule([task, deadline](uvEngine* engine) {
    intptr_t key = task->key_;
    engine->lookupTaskMap_[key] = task;
    const char* ccaddress = task->address_.c_str();
    const char* ccdefault_port = task->default_port_.c_str();
    int r = uv_getaddrinfo(&engine->loop_, &task->req_,
                           uvLookupTask::uvResolverCallbackTrampoline,
                           ccaddress, ccdefault_port, nullptr);
    if (r != 0) {
      auto on_resolve = std::move(task->on_resolve_);
      engine->eraseLookupTask(key);
      on_resolve(absl::UnknownError("blah"));
      return;
    }
    uv_timer_init(&engine->loop_, &task->timer_);
    absl::Duration timeout = deadline - absl::Now();
    uv_timer_start(&task->timer_, uvLookupTask::uvTimerCallback,
                   timeout / absl::Milliseconds(1), 0);
  });

  return {task->key_};
}

void uvDNSResolver::TryCancelLookup(
    grpc_event_engine::experimental::EventEngine::DNSResolver::LookupTaskHandle
        task) {
  engine_->schedule([task](uvEngine* engine) {
    auto it = engine->lookupTaskMap_.find(task.key);
    if (it == engine->lookupTaskMap_.end()) return;
    it->second->cancel(engine);
  });
}

void uvEngine::eraseTask(intptr_t taskKey) {
  auto it = taskMap_.find(taskKey);
  GPR_ASSERT(it != taskMap_.end());
  delete it->second;
  taskMap_.erase(it);
}

void uvEngine::eraseLookupTask(intptr_t taskKey) {
  auto it = lookupTaskMap_.find(taskKey);
  GPR_ASSERT(it != lookupTaskMap_.end());
  delete it->second;
  lookupTaskMap_.erase(it);
}

uvEndpoint::~uvEndpoint() {
  GPR_ASSERT(uvTCP_->write_bufs_ == nullptr);
  GPR_ASSERT(uvTCP_->on_read_ == nullptr);
  uvTCP* tcp = uvTCP_;
  getEngine()->schedule([tcp](uvEngine* engine) {
    tcp->to_close_ = 3;
    uvTCPbase* base = tcp;
    tcp->tcp_.data = base;
    uv_close(reinterpret_cast<uv_handle_t*>(&tcp->tcp_), uvTCPbase::uvCloseCB);
    uv_close(reinterpret_cast<uv_handle_t*>(&tcp->write_timer_),
             uvTCPbase::uvCloseCB);
    uv_close(reinterpret_cast<uv_handle_t*>(&tcp->read_timer_),
             uvTCPbase::uvCloseCB);
  });
}

int uvEndpoint::init(uvEngine* engine) {
  connect_.data = this;
  return uvTCP_->init(engine);
}

void uvEndpoint::Write(
    grpc_event_engine::experimental::EventEngine::Callback on_writable,
    grpc_event_engine::experimental::SliceBuffer* data, absl::Time deadline) {
  GPR_ASSERT(uvTCP_->write_bufs_ == nullptr);
  size_t count = data->count();
  uvTCP_->write_bufs_ = new uv_buf_t[count];
  uvTCP_->write_bufs_count_ = count;
  uvTCP_->on_writable_ = std::move(on_writable);
  for (size_t i = 0; i < count; i++) {
    grpc_event_engine::experimental::Slice slice = data->ref(i);
    if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
      std::string prefix =
          absl::StrFormat("EE::UV::uvEndpoint::Write@%p", this);
      hexdump(prefix, slice.begin(), slice.size());
    }
    uvTCP_->write_bufs_[i].base =
        reinterpret_cast<char*>(const_cast<uint8_t*>(slice.begin()));
    uvTCP_->write_bufs_[i].len = slice.size();
  }
  getEngine()->schedule([this, deadline](uvEngine* engine) {
    absl::Duration timeout = deadline - absl::Now();
    uv_timer_start(
        &uvTCP_->write_timer_,
        [](uv_timer_t* timer) {
          uvTCP* tcp = reinterpret_cast<uvTCP*>(timer->data);
          uv_cancel(reinterpret_cast<uv_req_t*>(&tcp->write_req_));
          delete[] tcp->write_bufs_;
          tcp->write_bufs_ = nullptr;
          tcp->on_writable_(absl::CancelledError("deadline"));
        },
        timeout / absl::Milliseconds(1), 0);
    uv_write(&uvTCP_->write_req_, reinterpret_cast<uv_stream_t*>(&uvTCP_->tcp_),
             uvTCP_->write_bufs_, uvTCP_->write_bufs_count_,
             [](uv_write_t* req, int status) {
               uvTCP* tcp = reinterpret_cast<uvTCP*>(req->data);
               delete[] tcp->write_bufs_;
               tcp->write_bufs_ = nullptr;
               uv_timer_stop(&tcp->write_timer_);
               tcp->on_writable_(
                   status == 0
                       ? absl::OkStatus()
                       : absl::UnknownError("uv_write gave us an error"));
             });
  });
}

void uvEndpoint::Read(
    grpc_event_engine::experimental::EventEngine::Callback on_read,
    grpc_event_engine::experimental::SliceBuffer* buffer, absl::Time deadline) {
  buffer->clear();
  uvTCP_->read_sb_ = buffer;
  uvTCP_->on_read_ = std::move(on_read);
  getEngine()->schedule([this, deadline](uvEngine* engine) {
    absl::Duration timeout = deadline - absl::Now();
    uv_timer_start(
        &uvTCP_->read_timer_,
        [](uv_timer_t* timer) {
          uvTCP* tcp = reinterpret_cast<uvTCP*>(timer->data);
          uv_read_stop(reinterpret_cast<uv_stream_t*>(&tcp->tcp_));
          tcp->on_read_(absl::CancelledError("deadline"));
        },
        timeout / absl::Milliseconds(1), 0);
    uv_read_start(
        reinterpret_cast<uv_stream_t*>(&uvTCP_->tcp_),
        [](uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
          uvTCP* tcp = reinterpret_cast<uvTCP*>(handle->data);
          buf->base = reinterpret_cast<char*>(tcp->read_buf_);
          buf->len = sizeof(tcp->read_buf_);
        },
        [](uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
          if (nread == 0) return;
          uvTCP* tcp = reinterpret_cast<uvTCP*>(stream->data);
          uv_timer_stop(&tcp->read_timer_);
          uv_read_stop(stream);
          auto on_read = std::move(tcp->on_read_);
          if (nread < 0) {
            on_read(absl::UnknownError("uv_read_start gave us an error"));
            return;
          }
          grpc_event_engine::experimental::Slice slice(tcp->read_buf_, nread);
          if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
            std::string prefix = absl::StrFormat("EE::UV::uvEndpoint::onRead");
            hexdump(prefix, slice.begin(), slice.size());
          }
          tcp->read_sb_->add(slice);
          on_read(absl::OkStatus());
          memset(tcp->read_buf_, 0, buf->len);
        });
  });
}

grpc_core::DebugOnlyTraceFlag grpc_polling_trace(false, "polling");

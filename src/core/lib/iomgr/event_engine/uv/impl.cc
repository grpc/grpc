#include <uv.h>

#include <atomic>
#include <functional>
#include <future>
#include <unordered_map>

#include "grpc/event_engine/event_engine.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/gprpp/mpscq.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/thd.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/socket_utils.h"

class uvEngine;
class uvTask;
class uvLookupTask;

class uvTCPbase {
 public:
  uvTCPbase() = default;
  uvTCPbase(
      grpc_event_engine::experimental::EventEngine::Listener::AcceptCallback
          on_accept,
      grpc_event_engine::experimental::EventEngine::Callback on_shutdown)
      : on_accept_(on_accept), on_shutdown_(on_shutdown) {}
  virtual ~uvTCPbase() = default;
  static void uvCloseCB(uv_handle_t* handle) {
    uvTCPbase* tcp = reinterpret_cast<uvTCPbase*>(handle->data);
    if (--tcp->to_close_ == 0) {
      delete tcp;
    }
  }

  int init(uvEngine* engine);

  grpc_event_engine::experimental::EventEngine::Listener::AcceptCallback
      on_accept_;

  grpc_event_engine::experimental::EventEngine::Callback on_shutdown_;

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
      : uvTCPbase(on_accept, on_shutdown),
        slice_allocator_factory_(std::move(slice_allocator_factory)) {}
  virtual ~uvTCPlistener() = default;
  grpc_event_engine::experimental::SliceAllocatorFactory
      slice_allocator_factory_;
};

class uvTCP final : public uvTCPbase {
 public:
  uvTCP(const grpc_event_engine::experimental::ChannelArgs& args,
        grpc_event_engine::experimental::SliceAllocator slice_allocator) {}
  virtual ~uvTCP() = default;

  uv_connect_t connect_;
  uv_timer_t write_timer_;
  uv_timer_t read_timer_;
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

  uvTCPlistener* uvTCP_ = nullptr;
  int init_result_ = 0;
};

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

class uvEndpoint final
    : public grpc_event_engine::experimental::EventEngine::Endpoint {
 public:
  uvEndpoint(const grpc_event_engine::experimental::ChannelArgs& args,
             grpc_event_engine::experimental::SliceAllocator slice_allocator)
      : uvTCP_(new uvTCP(args, std::move(slice_allocator))) {
    uvTCP_->tcp_.data = this;
    write_req_.data = this;
    uvTCP_->write_timer_.data = this;
    uvTCP_->read_timer_.data = this;
  }
  virtual ~uvEndpoint() override final;
  int init(uvEngine* engine);

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
    abort();
  };

  virtual const grpc_event_engine::experimental::EventEngine::ResolvedAddress*

  GetLocalAddress() const override final {
    abort();
  };

  static void uvGetaddrInfoCB(uv_getaddrinfo_t* req, int status,
                              struct addrinfo* res);
  static void uvConnectCB(uv_connect_t* req, int status) {
    uvEndpoint* epRaw = reinterpret_cast<uvEndpoint*>(req->handle->data);
    std::unique_ptr<uvEndpoint> ep(epRaw);
    if (status == 0) {
      ep->on_connect_(std::move(ep));
    } else {
      ep->on_connect_(absl::UnknownError("uv_tcp_connect gave us an error"));
    }
  }

  static void uvWriteCB(uv_write_t* req, int status) {
    uvEndpoint* ep = reinterpret_cast<uvEndpoint*>(req->data);
    delete[] ep->write_bufs_;
    ep->write_bufs_ = nullptr;
    uv_timer_stop(&ep->uvTCP_->write_timer_);
    ep->on_writable_(status == 0
                         ? absl::OkStatus()
                         : absl::UnknownError("uv_write gave us an error"));
  }
  static void uvAllocCB(uv_handle_t* handle, size_t suggested_size,
                        uv_buf_t* buf) {
    uvEndpoint* ep = reinterpret_cast<uvEndpoint*>(handle->data);
    buf->base = reinterpret_cast<char*>(ep->read_buf_);
    buf->len = sizeof(ep->read_buf_);
  }

  static void uvReadCB(uv_stream_t* stream, ssize_t nread,
                       const uv_buf_t* buf) {
    if (nread == 0) return;
    uvEndpoint* ep = reinterpret_cast<uvEndpoint*>(stream->data);
    uv_timer_stop(&ep->uvTCP_->read_timer_);
    uv_read_stop(stream);
    auto cb = std::move(ep->on_read_);
    if (nread < 0) {
      cb(absl::UnknownError("uv_read_start gave us an error"));
      return;
    }
    grpc_event_engine::experimental::Slice slice(
        ep->read_buf_, nread,
        grpc_event_engine::experimental::Slice::STATIC_SLICE);
    ep->read_sb_->add(slice);
    cb(absl::OkStatus());
    memset(ep->read_buf_, 0, buf->len);
  }

  static void uvReadTimeoutCB(uv_timer_t* timer) {
    uvEndpoint* ep = reinterpret_cast<uvEndpoint*>(timer->data);
    uv_read_stop(reinterpret_cast<uv_stream_t*>(&ep->uvTCP_->tcp_));
    ep->on_read_(absl::CancelledError("deadline"));
  }

  static void uvWriteTimeoutCB(uv_timer_t* timer) {
    uvEndpoint* ep = reinterpret_cast<uvEndpoint*>(timer->data);
    uv_cancel(reinterpret_cast<uv_req_t*>(&ep->write_req_));
    delete[] ep->write_bufs_;
    ep->write_bufs_ = nullptr;
    ep->on_writable_(absl::CancelledError("deadline"));
  }

  uvTCP* uvTCP_ = nullptr;
  uv_write_t write_req_;
  uv_buf_t* write_bufs_ = nullptr;
  size_t write_bufs_count_ = 0;
  uint8_t read_buf_[4096];
  grpc_event_engine::experimental::SliceBuffer* read_sb_;
  grpc_event_engine::experimental::EventEngine::Callback on_writable_;
  grpc_event_engine::experimental::EventEngine::Callback on_read_;

  grpc_event_engine::experimental::EventEngine::OnConnectCallback on_connect_ =
      nullptr;

  uvEngine* getEngine() {
    return reinterpret_cast<uvEngine*>(uvTCP_->tcp_.loop->data);
  }
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
      if (!node) continue;
      schedulingRequest::functor f = std::move(node->f_);
      delete node;
      f(this);
    }
  }

  void threadBody() {
    if (uv_loop_init(&loop_) != 0) {
      ready_.set_value(false);
      return;
    }
    loop_.data = this;
    if (uv_async_init(&loop_, &kicker_, kickerTrampoline) != 0) {
      ready_.set_value(false);
      return;
    }
    ready_.set_value(true);
    uv_run(&loop_, UV_RUN_DEFAULT);
    on_shutdown_complete_(absl::OkStatus());
  }

  uvEngine() {
    bool success = false;
    grpc_core::Thread::Options options;
    options.set_joinable(false);
    thread_ = grpc_core::Thread("uv loop", threadBodyTrampoline, this, &success,
                                options);
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
    return absl::make_unique<uvListener>(this, on_accept, on_shutdown, args,
                                         std::move(slice_allocator_factory));
  }

  virtual absl::Status Connect(
      OnConnectCallback on_connect, const ResolvedAddress& addr,
      const grpc_event_engine::experimental::ChannelArgs& args,
      grpc_event_engine::experimental::SliceAllocator slice_allocator,
      absl::Time deadline) override final {
    uvEndpoint* e = new uvEndpoint(args, std::move(slice_allocator));
    e->on_connect_ = on_connect;
    schedule([addr, e](uvEngine* engine) {
      int r;
      r = e->init(engine);
      uv_tcp_connect(&e->uvTCP_->connect_, &e->uvTCP_->tcp_, addr.address(),
                     uvEndpoint::uvConnectCB);
    });
    return absl::OkStatus();
  }

  virtual ~uvEngine() override final {
    // still a bit non-obvious what to do here; probably check if
    // the shutdown happened, and assert on it ?

    // abort();
  }

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
};

int uvTCPbase::init(uvEngine* engine) {
  return uv_tcp_init(&engine->loop_, &tcp_);
}

uvListener::uvListener(
    uvEngine* engine, Listener::AcceptCallback on_accept,
    grpc_event_engine::experimental::EventEngine::Callback on_shutdown,
    const grpc_event_engine::experimental::ChannelArgs& args,
    grpc_event_engine::experimental::SliceAllocatorFactory
        slice_allocator_factory)
    : engine_(engine),
      uvTCP_(new uvTCPlistener(on_accept, on_shutdown, args,
                               std::move(slice_allocator_factory))) {
  std::promise<int> p;
  engine->schedule([this, &p](uvEngine* engine) {
    int r = uv_tcp_init(&engine->loop_, &uvTCP_->tcp_);

    p.set_value(r);
  });
  init_result_ = p.get_future().get();
}

uvListener::~uvListener() {
  uvTCPbase* tcp = uvTCP_;
  engine_->schedule([tcp](uvEngine* engine) {
    tcp->tcp_.data = tcp;
    tcp->to_close_ = 1;
    uv_close(reinterpret_cast<uv_handle_t*>(&tcp->tcp_), uvTCPbase::uvCloseCB);
  });
}

absl::StatusOr<int> uvListener::Bind(
    const grpc_event_engine::experimental::EventEngine::ResolvedAddress& addr) {
  std::promise<absl::StatusOr<int>> p;
  engine_->schedule([this, &p, &addr](uvEngine* engine) {
    int r = uv_tcp_bind(&uvTCP_->tcp_, addr.address(), 0 /* flags */);
    switch (r) {
      case UV_EINVAL:
        p.set_value(absl::InvalidArgumentError(
            "uv_tcp_bind said we passed an invalid argument to it"));
        return;
      case 0:
        break;
      default:
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
        p.set_value(absl::InvalidArgumentError(
            "returned socket address in :Bind is neither IPv4 nor IPv6"));
        break;
    }
  });
  return p.get_future().get();
}

absl::Status uvListener::Start() {
  engine_->schedule([](uvEngine* engine) { abort(); });
  return absl::OkStatus();
}

std::shared_ptr<grpc_event_engine::experimental::EventEngine>
grpc_event_engine::experimental::GetDefaultEventEngine() {
  // hmmm...
  static std::shared_ptr<grpc_event_engine::experimental::EventEngine> engine =
      std::make_shared<uvEngine>();
  return engine;
}

class uvTask {
 public:
  uvTask(uvEngine* engine) : key_(engine->taskKey_.fetch_add(1)) {
    timer_.data = this;
  }
  grpc_event_engine::experimental::EventEngine::Callback fn_;
  uv_timer_t timer_;
  bool done_ = false;
  const intptr_t key_;
  void uvTimerCallback() {
    cancel();
    fn_(absl::OkStatus());
  }
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
          reinterpret_cast<uvTask*>(timer->data)->uvTimerCallback();
        },
        timeout / absl::Milliseconds(1), 0);
  });

  return {task->key_};
}
void uvEngine::TryCancel(TaskHandle handle) {
  schedule([handle](uvEngine* engine) {
    auto it = engine->taskMap_.find(handle.key);
    if (it == engine->taskMap_.end()) return;
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
  GPR_ASSERT(write_bufs_ == nullptr);
  GPR_ASSERT(on_read_ == nullptr);
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
  uv_timer_init(&engine->loop_, &uvTCP_->write_timer_);
  uv_timer_init(&engine->loop_, &uvTCP_->read_timer_);
  return uvTCP_->init(engine);
}

void uvEndpoint::Write(
    grpc_event_engine::experimental::EventEngine::Callback on_writable,
    grpc_event_engine::experimental::SliceBuffer* data, absl::Time deadline) {
  GPR_ASSERT(write_bufs_ == nullptr);
  size_t count = data->count();
  write_bufs_ = new uv_buf_t[count];
  write_bufs_count_ = count;
  on_writable_ = std::move(on_writable);
  for (size_t i = 0; i < count; i++) {
    grpc_event_engine::experimental::Slice slice = data->ref(i);
    write_bufs_[i].base =
        reinterpret_cast<char*>(const_cast<uint8_t*>(slice.begin()));
    write_bufs_[i].len = slice.size();
  }
  getEngine()->schedule([this, deadline](uvEngine* engine) {
    absl::Duration timeout = deadline - absl::Now();
    uv_timer_start(&uvTCP_->write_timer_, uvWriteTimeoutCB,
                   timeout / absl::Milliseconds(1), 0);
    uv_write(&write_req_, reinterpret_cast<uv_stream_t*>(&uvTCP_->tcp_),
             write_bufs_, write_bufs_count_, uvWriteCB);
  });
}

void uvEndpoint::Read(
    grpc_event_engine::experimental::EventEngine::Callback on_read,
    grpc_event_engine::experimental::SliceBuffer* buffer, absl::Time deadline) {
  buffer->clear();
  read_sb_ = buffer;
  on_read_ = std::move(on_read);
  getEngine()->schedule([this, deadline](uvEngine* engine) {
    absl::Duration timeout = deadline - absl::Now();
    uv_timer_start(&uvTCP_->read_timer_, uvReadTimeoutCB,
                   timeout / absl::Milliseconds(1), 0);
    uv_read_start(reinterpret_cast<uv_stream_t*>(&uvTCP_->tcp_), uvAllocCB,
                  uvReadCB);
  });
}

grpc_core::DebugOnlyTraceFlag grpc_polling_trace(false, "polling");

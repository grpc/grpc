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

namespace {

class LibuvDNSResolver;
class LibuvEventEngine;
class LibuvListener;
class LibuvLookupTask;
class LibuvTask;

// Helper to dump network traffic in a legible manner.
// Probably belongs to a different utility file elsewhere, but wasn't sure if
// we even want to keep it eventually.
static void hexdump(const std::string& prefix, const void* data_, size_t size) {
  const uint8_t* data = static_cast<const uint8_t*>(data_);
  char ascii[17];
  ascii[16] = 0;
  memset(ascii, ' ', 16);
  std::string line;
  size_t beginning;
  for (size_t i = 0; i < size; i++) {
    if (i % 16 == 0) {
      line = "";
      beginning = i;
    }
    uint8_t d = data[i];
    line += absl::StrFormat("%02X ", d);
    ascii[i % 16] = isprint(d) ? d : '.';
    size_t n = i + 1;
    if (((n % 8) == 0) || (n == size)) {
      line += " ";
      if (((n % 16) != 0) && (n != size)) continue;
      if (n == size) {
        n %= 16;
        for (unsigned p = n; (n != 0) && (p < 16); p++) {
          line += "   ";
          ascii[p] = ' ';
        }
        if ((n <= 8) && (n != 0)) {
          line += " ";
        }
      }
      gpr_log(GPR_DEBUG, "%s %p %04zX  | %s| %s |", prefix.c_str(),
              data + beginning, beginning, line.c_str(), ascii);
    }
  }
}

constexpr size_t READ_BUFFER_SIZE = 4096;

////////////////////////////////////////////////////////////////////////////////
/// The base class to wrap a LibUV TCP handle. The class hierarchy that stems
/// from it is meant to split the Event Engine objects from the LibUV ones, as
/// their lifespan aren't the same. When an Event Engine object is destroyed,
/// we need to request the destruction into LibUV's API, while keeping LibUV's
/// structures around, which is what we're going to do here.
///
/// It will hold the few common data and functions between the Listener and
/// Endpoint classes that will derive from it.
///
////////////////////////////////////////////////////////////////////////////////
class LibuvWrapperBase {
 public:
  // Returns the LibUV loop object associated with this handle. Used by the
  // LibuvEventEngine class.
  uv_loop_t* GetLoop() { return tcp_.loop; }

  // The rest of the API really should only be used by the derived classes.
 protected:
  LibuvWrapperBase() { tcp_.data = this; }
  virtual ~LibuvWrapperBase() = default;

  // Registers the libuv handle into the libuv loop. This needs to be called
  // from the corresponding libuv Thread.
  void RegisterUnsafe(LibuvEventEngine* engine);

  void CloseUnsafe(int extraCloses) {
    tcp_.data = this;
    to_close_ = 1 + extraCloses;
    uv_close(reinterpret_cast<uv_handle_t*>(&tcp_),
             LibuvWrapperBase::LibuvCloseCB);
  }

  // We keep a counter on how many times this callback needs to be called
  // before we can actually delete the object. One of our derived objects may
  // contain more than one LibUV handle, which all need to have LibUV calling
  // us individually when it's safe to delete each of them. We shouldn't care
  // about the type of each of these handle, as long as they have been closed
  // with their respective API.
  static void LibuvCloseCB(uv_handle_t* handle) {
    LibuvWrapperBase* self = reinterpret_cast<LibuvWrapperBase*>(handle->data);
    if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
      gpr_log(GPR_DEBUG, "LibuvWrapperBase:%p close CB, callbacks pending: %i",
              self, self->to_close_ - 1);
    }
    if (--self->to_close_ == 0) {
      delete self;
    }
  }

  uv_tcp_t tcp_;
  int to_close_ = 0;
};

////////////////////////////////////////////////////////////////////////////////
/// The derived class to wrap a LibUV TCP listener handle. Its API will
/// closely match that of the Event Engine listener. Its only consumer should
/// be the LibuvListener class.
////////////////////////////////////////////////////////////////////////////////
class LibuvListenerWrapper final : public LibuvWrapperBase {
  friend class LibuvListener;

  LibuvListenerWrapper(
      grpc_event_engine::experimental::EventEngine::Listener::AcceptCallback
          on_accept,
      grpc_event_engine::experimental::EventEngine::Callback on_shutdown,
      const grpc_event_engine::experimental::EndpointConfig& args,
      std::unique_ptr<grpc_event_engine::experimental::SliceAllocatorFactory>
          slice_allocator_factory)
      : on_accept_(std::move(on_accept)),
        on_shutdown_(std::move(on_shutdown)),
        args_(args),
        slice_allocator_factory_(std::move(slice_allocator_factory)) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
      gpr_log(GPR_DEBUG, "LibuvListenerWrapper:%p created", this);
    }
  }

  void Close(LibuvEventEngine* engine);

  // When LibUV is finally done with the listener, this destructor will be
  // called from the base class, which is when we can properly invoke the
  // shutdown callback.
  virtual ~LibuvListenerWrapper() { on_shutdown_(absl::OkStatus()); };

  grpc_event_engine::experimental::EventEngine::Listener::AcceptCallback
      on_accept_;
  grpc_event_engine::experimental::EventEngine::Callback on_shutdown_;
  grpc_event_engine::experimental::EndpointConfig args_;
  std::unique_ptr<grpc_event_engine::experimental::SliceAllocatorFactory>
      slice_allocator_factory_;
};

////////////////////////////////////////////////////////////////////////////////
/// The derived class to wrap a LibUV TCP connected handle. Its API will
/// closely match that of the Event Engine endpoint. Its only consumer should
/// be the LibuvEndpoint class.
////////////////////////////////////////////////////////////////////////////////
class LibuvEndpointWrapper final : public LibuvWrapperBase {
  friend class LibuvEndpoint;

  LibuvEndpointWrapper(
      const grpc_event_engine::experimental::EndpointConfig& args,
      std::unique_ptr<grpc_event_engine::experimental::SliceAllocator>
          slice_allocator);

  virtual ~LibuvEndpointWrapper() {
    GPR_ASSERT(write_bufs_ == nullptr);
    GPR_ASSERT(on_read_ == nullptr);
  };

  const grpc_event_engine::experimental::EndpointConfig args_;
  std::unique_ptr<grpc_event_engine::experimental::SliceAllocator>
      slice_allocator_;
  uv_write_t write_req_;
  uv_buf_t* write_bufs_ = nullptr;
  size_t write_bufs_count_ = 0;
  grpc_event_engine::experimental::SliceBuffer* read_sb_;
  grpc_event_engine::experimental::EventEngine::Callback on_writable_;
  grpc_event_engine::experimental::EventEngine::Callback on_read_;
  grpc_event_engine::experimental::EventEngine::ResolvedAddress peer_address_;
  grpc_event_engine::experimental::EventEngine::ResolvedAddress local_address_;
};

////////////////////////////////////////////////////////////////////////////////
/// This class is only a tiny, very temporary shell around the engine itself,
/// and doesn't hold any state of its own.
////////////////////////////////////////////////////////////////////////////////
class LibuvDNSResolver final
    : public grpc_event_engine::experimental::EventEngine::DNSResolver {
 public:
  virtual ~LibuvDNSResolver() override = default;

  LibuvDNSResolver(LibuvEventEngine* engine) : engine_(engine) {}

 private:
  virtual LookupTaskHandle LookupHostname(LookupHostnameCallback on_resolve,
                                          absl::string_view address,
                                          absl::string_view default_port,
                                          absl::Time deadline) override;

  virtual LookupTaskHandle LookupSRV(LookupSRVCallback on_resolve,
                                     absl::string_view name,
                                     absl::Time deadline) override {
    // TODO(nnoble): implement on top of cares
    abort();
  }

  virtual LookupTaskHandle LookupTXT(LookupTXTCallback on_resolve,
                                     absl::string_view name,
                                     absl::Time deadline) override {
    // TODO(nnoble): implement on top of cares
    abort();
  }

  virtual void TryCancelLookup(LookupTaskHandle handle) override;

  LibuvEventEngine* engine_;
};

////////////////////////////////////////////////////////////////////////////////
/// The Event Engine Listener class. Aside from implementing the listener API,
/// it will only hold a pointer to a LibuvListenerWrapper.
///
/// Its main reason of existence is to transform its destruction into
/// scheduling a LibUV close() call of the underlying socket.
////////////////////////////////////////////////////////////////////////////////
class LibuvListener final
    : public grpc_event_engine::experimental::EventEngine::Listener {
 public:
  LibuvListener(
      Listener::AcceptCallback on_accept,
      grpc_event_engine::experimental::EventEngine::Callback on_shutdown,
      const grpc_event_engine::experimental::EndpointConfig& args,
      std::unique_ptr<grpc_event_engine::experimental::SliceAllocatorFactory>
          slice_allocator_factory);

  void RegisterUnsafe(LibuvEventEngine* engine) {
    uvTCP_->RegisterUnsafe(engine);
  }

  virtual ~LibuvListener() override;

 private:
  LibuvEventEngine* GetEventEngine() {
    return reinterpret_cast<LibuvEventEngine*>(uvTCP_->GetLoop()->data);
  }

  virtual absl::StatusOr<int> Bind(
      const grpc_event_engine::experimental::EventEngine::ResolvedAddress& addr)
      override;

  virtual absl::Status Start() override;

  LibuvListenerWrapper* uvTCP_ = nullptr;
};

////////////////////////////////////////////////////////////////////////////////
/// The Event Engine Endpoint class. Aside from implementing the listener API,
/// it will hold a pointer to a LibuvEndpointWrapper, as well as the temporary
/// connection information. The initial connection information is held in it,
/// because of the limbo state the handle is in between the moment we are
/// requested to initiate the connection, and the moment we are actually
/// connected and giving back the endpoint to the callback.
///
/// It's separate from the LibuvEndpointWrapper in order to transform its
/// destruction into scheduling a LibUV close() call of the underlying socket.
////////////////////////////////////////////////////////////////////////////////
class LibuvEndpoint final
    : public grpc_event_engine::experimental::EventEngine::Endpoint {
 public:
  LibuvEndpoint(const grpc_event_engine::experimental::EndpointConfig& args,
                std::unique_ptr<grpc_event_engine::experimental::SliceAllocator>
                    slice_allocator)
      : uvTCP_(new LibuvEndpointWrapper(args, std::move(slice_allocator))) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
      gpr_log(GPR_DEBUG, "LibuvEndpoint:%p created", this);
    }
    connect_.data = this;
  }

  absl::Status Connect(
      LibuvEventEngine* engine,
      grpc_event_engine::experimental::EventEngine::OnConnectCallback
          on_connect,
      const grpc_event_engine::experimental::EventEngine::ResolvedAddress&
          addr);

  void RegisterUnsafe(LibuvEventEngine* engine) {
    uvTCP_->RegisterUnsafe(engine);
  }

  bool AcceptUnsafe(LibuvEventEngine* engine, uv_stream_t* server) {
    RegisterUnsafe(engine);
    int r;
    r = uv_accept(server, reinterpret_cast<uv_stream_t*>(&uvTCP_->tcp_));
    if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
      gpr_log(GPR_DEBUG, "LibuvEndpoint@%p, accepting new connection: %i", this,
              r);
    }
    if (r == 0) {
      PopulateAddressesUnsafe();
      return true;
    }
    return false;
  }

  virtual ~LibuvEndpoint() override;

 private:
  LibuvEventEngine* GetEventEngine() {
    return reinterpret_cast<LibuvEventEngine*>(uvTCP_->tcp_.loop->data);
  }

  int PopulateAddressesUnsafe() {
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
      gpr_log(GPR_DEBUG, "LibuvEndpoint@%p::populateAddresses, r=%d", uvTCP_,
              r);
    }
    return r;
  }

  virtual void Read(
      grpc_event_engine::experimental::EventEngine::Callback on_read,
      grpc_event_engine::experimental::SliceBuffer* buffer) override;

  virtual void Write(
      grpc_event_engine::experimental::EventEngine::Callback on_writable,
      grpc_event_engine::experimental::SliceBuffer* data) override;

  virtual const grpc_event_engine::experimental::EventEngine::ResolvedAddress&
  GetPeerAddress() const override {
    return uvTCP_->peer_address_;
  };

  virtual const grpc_event_engine::experimental::EventEngine::ResolvedAddress&
  GetLocalAddress() const override {
    return uvTCP_->local_address_;
  };

  LibuvEndpointWrapper* uvTCP_ = nullptr;
  uv_connect_t connect_;
  grpc_event_engine::experimental::EventEngine::OnConnectCallback on_connect_ =
      nullptr;
};

struct SchedulingRequest : grpc_core::MultiProducerSingleConsumerQueue::Node {
  typedef std::function<void(LibuvEventEngine*)> functor;
  SchedulingRequest(functor&& f) : f_(std::move(f)) {}
  functor f_;
};

////////////////////////////////////////////////////////////////////////////////
/// The LibUV Event Engine itself.
////////////////////////////////////////////////////////////////////////////////
class LibuvEventEngine final
    : public grpc_event_engine::experimental::EventEngine {
  std::thread::id worker_thread_id_;

 public:
  LibuvEventEngine() {
    bool success = false;
    if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
      gpr_log(GPR_DEBUG, "LibuvEventEngine:%p created", this);
    }
    grpc_core::Thread::Options options;
    options.set_joinable(false);
    thread_ = grpc_core::Thread(
        "uv loop",
        [](void* arg) {
          LibuvEventEngine* engine = reinterpret_cast<LibuvEventEngine*>(arg);
          engine->Thread();
        },
        this, &success, options);
    thread_.Start();
    GPR_ASSERT(success);
    success = ready_.get_future().get();
    GPR_ASSERT(success);
  }

  void Schedule(SchedulingRequest::functor&& f) {
    SchedulingRequest* request = new SchedulingRequest(std::move(f));
    if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
      gpr_log(GPR_ERROR, "LibuvEventEngine@%p::Schedule, created %p", this,
              request);
    }
    queue_.Push(request);
    uv_async_send(&kicker_);
  }

  uv_loop_t* GetLoop() { return &loop_; }

 private:
  void Kicker() {
    bool empty = false;
    while (!empty) {
      SchedulingRequest* node =
          reinterpret_cast<SchedulingRequest*>(queue_.PopAndCheckEnd(&empty));
      if (!node) continue;
      SchedulingRequest::functor f = std::move(node->f_);
      if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
        gpr_log(GPR_ERROR, "LibuvEventEngine@%p::Kicker, got %p", this, node);
      }
      delete node;
      f(this);
    }
  }

  virtual bool IsWorkerThread() override {
    return worker_thread_id_ == std::this_thread::get_id();
  }

  void Thread() {
    // ugh
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGPIPE);
    pthread_sigmask(SIG_BLOCK, &set, NULL);
    int r = 0;
    worker_thread_id_ = std::this_thread::get_id();
    r = uv_loop_init(&loop_);
    loop_.data = this;
    r |= uv_async_init(&loop_, &kicker_, [](uv_async_t* async) {
      LibuvEventEngine* engine =
          reinterpret_cast<LibuvEventEngine*>(async->loop->data);
      engine->Kicker();
    });
    if (r != 0) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
        gpr_log(GPR_ERROR, "LibuvEventEngine@%p::Thread, failed to start: %i",
                this, r);
      }
      ready_.set_value(false);
      return;
    }
    ready_.set_value(true);
    grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
    grpc_core::ExecCtx ctx;
    while (uv_run(&loop_, UV_RUN_ONCE) != 0 && !shutdown_) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
        gpr_log(GPR_ERROR,
                "LibuvEventEngine@%p::Thread, uv_run requests a "
                "context flush",
                this);
      }
      ctx.Flush();
    }
    if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
      gpr_log(GPR_DEBUG, "LibuvEventEngine@%p::Thread, shutting down", this);
    }
    on_shutdown_complete_(absl::OkStatus());
  }

  virtual absl::StatusOr<std::unique_ptr<Listener>> CreateListener(
      Listener::AcceptCallback on_accept, Callback on_shutdown,
      const grpc_event_engine::experimental::EndpointConfig& args,
      std::unique_ptr<grpc_event_engine::experimental::SliceAllocatorFactory>
          slice_allocator_factory) override {
    std::unique_ptr<LibuvListener> ret = absl::make_unique<LibuvListener>(
        on_accept, on_shutdown, args, std::move(slice_allocator_factory));
    if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
      gpr_log(GPR_DEBUG, "LibuvEventEngine@%p::CreateListener, created %p",
              this, ret.get());
    }
    Schedule([&ret](LibuvEventEngine* engine) { ret->RegisterUnsafe(engine); });
    return ret;
  }

  virtual absl::Status Connect(
      OnConnectCallback on_connect, const ResolvedAddress& addr,
      const grpc_event_engine::experimental::EndpointConfig& args,
      std::unique_ptr<grpc_event_engine::experimental::SliceAllocator>
          slice_allocator,
      absl::Time deadline) override {
    LibuvEndpoint* e = new LibuvEndpoint(args, std::move(slice_allocator));
    if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
      gpr_log(GPR_DEBUG, "LibuvEventEngine@%p::Connect, created %p", this, e);
    }
    return e->Connect(this, std::move(on_connect), addr);
  }

  virtual ~LibuvEventEngine() override {}

  virtual absl::StatusOr<std::unique_ptr<
      grpc_event_engine::experimental::EventEngine::DNSResolver>>
  GetDNSResolver() override {
    return absl::make_unique<LibuvDNSResolver>(this);
  }

  virtual TaskHandle Run(Callback fn, RunOptions opts) override;
  virtual TaskHandle RunAt(absl::Time when, Callback fn,
                           RunOptions opts) override;
  virtual void TryCancel(TaskHandle handle) override;

  virtual void Shutdown(Callback on_shutdown_complete) override {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
      gpr_log(GPR_DEBUG, "LibuvEventEngine@%p::Shutdown", this);
    }
    on_shutdown_complete_ = on_shutdown_complete;
    shutdown_ = true;
    Schedule([](LibuvEventEngine* engine) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
        gpr_log(GPR_DEBUG,
                "LibuvEventEngine@%p shutting down, unreferencing "
                "Kicker now",
                engine);
      }
      uv_unref(reinterpret_cast<uv_handle_t*>(&engine->kicker_));
      if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
        uv_walk(
            &engine->loop_,
            [](uv_handle_t* handle, void* arg) {
              uv_handle_type type = uv_handle_get_type(handle);
              const char* name = uv_handle_type_name(type);
              gpr_log(GPR_DEBUG,
                      "in shutdown, handle %p type %s has references: %s",
                      handle, name, uv_has_ref(handle) ? "yes" : "no");
            },
            nullptr);
      }
    });
  }

  void EraseTask(intptr_t taskKey);
  void EraseLookupTask(intptr_t taskKey);

  uv_loop_t loop_;
  uv_async_t kicker_;
  std::promise<bool> ready_;
  grpc_core::Thread thread_;
  grpc_core::MultiProducerSingleConsumerQueue queue_;
  std::atomic<intptr_t> taskKey_;
  std::atomic<intptr_t> lookupTaskKey_;
  std::unordered_map<intptr_t, LibuvTask*> taskMap_;
  std::unordered_map<intptr_t, LibuvLookupTask*> lookupTaskMap_;
  grpc_event_engine::experimental::EventEngine::Callback on_shutdown_complete_;
  bool shutdown_ = false;

  friend class LibuvDNSResolver;
  friend class LibuvLookupTask;
  friend class LibuvTask;
};

class LibuvTask {
 public:
  LibuvTask(LibuvEventEngine* engine) : key_(engine->taskKey_.fetch_add(1)) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
      gpr_log(GPR_DEBUG, "LibuvTask@%p, created: key = %" PRIiPTR, this, key_);
    }
    timer_.data = this;
  }
  grpc_event_engine::experimental::EventEngine::Callback fn_;
  bool triggered_ = false;
  uv_timer_t timer_;
  const intptr_t key_;
  void Cancel() {
    if (uv_is_closing(reinterpret_cast<uv_handle_t*>(&timer_))) {
      return;
    }
    uv_timer_stop(&timer_);
    uv_close(reinterpret_cast<uv_handle_t*>(&timer_), [](uv_handle_t* handle) {
      uv_timer_t* timer = reinterpret_cast<uv_timer_t*>(handle);
      LibuvTask* task = reinterpret_cast<LibuvTask*>(timer->data);
      LibuvEventEngine* engine =
          reinterpret_cast<LibuvEventEngine*>(timer->loop->data);
      engine->EraseTask(task->key_);
    });
  }
};

class LibuvLookupTask {
 public:
  LibuvLookupTask(LibuvEventEngine* engine)
      : key_(engine->lookupTaskKey_.fetch_add(1)) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
      gpr_log(GPR_DEBUG, "LibuvLookupTask@%p, created: key = %" PRIiPTR, this,
              key_);
    }
    req_.data = this;
    timer_.data = this;
  }
  std::string address_;
  std::string default_port_;
  uv_getaddrinfo_t req_;
  uv_timer_t timer_;
  bool deadline_exceeded_ = false;
  const intptr_t key_;
  grpc_event_engine::experimental::EventEngine::DNSResolver::
      LookupHostnameCallback on_resolve_;
  void uvResolverCallback(int status, struct addrinfo* res) {
    uv_timer_stop(&timer_);
    if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
      gpr_log(GPR_DEBUG, "LookupHostname for %s:%s completed with status = %i",
              address_.c_str(), default_port_.c_str(), status);
    }
    uv_close(reinterpret_cast<uv_handle_t*>(&timer_), [](uv_handle_t* timer) {
      LibuvLookupTask* task = reinterpret_cast<LibuvLookupTask*>(timer->data);
      LibuvEventEngine* engine =
          reinterpret_cast<LibuvEventEngine*>(timer->loop->data);
      engine->EraseLookupTask(task->key_);
    });
    if (status == UV_ECANCELED) {
      if (deadline_exceeded_) {
        on_resolve_(absl::DeadlineExceededError("Deadline exceeded"));
      } else {
        on_resolve_(absl::CancelledError());
      }
    } else if (status != 0) {
      on_resolve_(
          absl::UnknownError("uv_getaddrinfo failed with an unknown error"));
    } else {
      struct addrinfo* p;
      std::vector<grpc_event_engine::experimental::EventEngine::ResolvedAddress>
          ret;

      for (p = res; p != nullptr; p = p->ai_next) {
        ret.emplace_back(p->ai_addr, p->ai_addrlen);
      }

      uv_freeaddrinfo(res);
      on_resolve_(std::move(ret));
    }
  }
  static void uvTimerCallback(uv_timer_t* timer) {
    LibuvLookupTask* task = reinterpret_cast<LibuvLookupTask*>(timer->data);
    task->deadline_exceeded_ = true;
    uv_cancel(reinterpret_cast<uv_req_t*>(&task->req_));
  }
  void Cancel(LibuvEventEngine* engine) {
    uv_timer_stop(&timer_);
    uv_cancel(reinterpret_cast<uv_req_t*>(&req_));
  }
};

}  // namespace

absl::Status LibuvEndpoint::Connect(
    LibuvEventEngine* engine,
    grpc_event_engine::experimental::EventEngine::OnConnectCallback on_connect,
    const grpc_event_engine::experimental::EventEngine::ResolvedAddress& addr) {
  on_connect_ = on_connect;
  engine->Schedule([addr, this](LibuvEventEngine* engine) {
    uvTCP_->RegisterUnsafe(engine);
    int r = uv_tcp_connect(
        &connect_, &uvTCP_->tcp_, addr.address(),
        [](uv_connect_t* req, int status) {
          LibuvEndpoint* epRaw = reinterpret_cast<LibuvEndpoint*>(req->data);
          std::unique_ptr<LibuvEndpoint> ep(epRaw);
          auto on_connect = std::move(ep->on_connect_);
          if (status == 0) {
            ep->PopulateAddressesUnsafe();
            if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
              gpr_log(GPR_DEBUG, "LibuvEndpoint@%p::Connect, success",
                      epRaw->uvTCP_);
            }
            on_connect(std::move(ep));
          } else {
            if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
              gpr_log(GPR_INFO, "LibuvEndpoint@%p::Connect, failed: %i",
                      epRaw->uvTCP_, status);
            }
            on_connect(absl::UnknownError(
                "uv_tcp_connect gave us an asynchronous error"));
          }
        });
    if (r != 0) {
      auto on_connect = std::move(on_connect_);
      if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
        gpr_log(GPR_INFO, "LibuvEndpoint@%p::Connect, failed: %i", uvTCP_, r);
      }
      delete this;
      on_connect(absl::UnknownError("uv_tcp_connect gave us an error"));
    }
  });
  return absl::OkStatus();
}

void LibuvWrapperBase::RegisterUnsafe(LibuvEventEngine* engine) {
  auto success = uv_tcp_init(engine->GetLoop(), &tcp_);
  GPR_ASSERT(success == 0);
}

LibuvEndpointWrapper::LibuvEndpointWrapper(
    const grpc_event_engine::experimental::EndpointConfig& args,
    std::unique_ptr<grpc_event_engine::experimental::SliceAllocator>
        slice_allocator)
    : args_(args), slice_allocator_(std::move(slice_allocator)) {
  write_req_.data = this;
}

LibuvListener::LibuvListener(
    Listener::AcceptCallback on_accept,
    grpc_event_engine::experimental::EventEngine::Callback on_shutdown,
    const grpc_event_engine::experimental::EndpointConfig& args,
    std::unique_ptr<grpc_event_engine::experimental::SliceAllocatorFactory>
        slice_allocator_factory)
    : uvTCP_(new LibuvListenerWrapper(on_accept, on_shutdown, args,
                                      std::move(slice_allocator_factory))) {
  uvTCP_->tcp_.data = uvTCP_;
}

LibuvListener::~LibuvListener() { uvTCP_->Close(GetEventEngine()); }

absl::StatusOr<int> LibuvListener::Bind(
    const grpc_event_engine::experimental::EventEngine::ResolvedAddress& addr) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
    grpc_resolved_address grpcaddr;
    grpcaddr.len = addr.size();
    memcpy(grpcaddr.addr, addr.address(), grpcaddr.len);
    gpr_log(GPR_DEBUG, "LibuvListener@%p::Bind to %s", uvTCP_,
            grpc_sockaddr_to_uri(&grpcaddr).c_str());
  }
  std::promise<absl::StatusOr<int>> p;
  GetEventEngine()->Schedule([this, &p, &addr](LibuvEventEngine* engine) {
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
          gpr_log(GPR_INFO, "LibuvListener@%p::Bind, uv_tcp_bind failed: %i",
                  uvTCP_, r);
        }
        p.set_value(absl::UnknownError(
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
                  "LibuvListener@%p::Bind, uv_tcp_getsockname failed: %i",
                  uvTCP_, r);
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
          gpr_log(GPR_INFO, "LibuvListener@%p::Bind, unknown addr family: %i",
                  uvTCP_, boundAddr.ss_family);
        }
        p.set_value(absl::InvalidArgumentError(
            "returned socket address in :Bind is neither IPv4 nor IPv6"));
        return;
    }
    if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
      gpr_log(GPR_DEBUG, "LibuvListener@%p::Bind, success", uvTCP_);
    }
  });
  return p.get_future().get();
}

absl::Status LibuvListener::Start() {
  std::promise<absl::Status> ret;
  GetEventEngine()->Schedule([this, &ret](LibuvEventEngine* engine) {
    int r;
    r = uv_listen(
        reinterpret_cast<uv_stream_t*>(&uvTCP_->tcp_), 42,
        [](uv_stream_t* server, int status) {
          if (status < 0) return;
          LibuvListenerWrapper* l =
              static_cast<LibuvListenerWrapper*>(server->data);
          std::unique_ptr<LibuvEndpoint> e = absl::make_unique<LibuvEndpoint>(
              l->args_, l->slice_allocator_factory_->CreateSliceAllocator(
                            "TODO(nnoble): get peer name"));
          LibuvEventEngine* engine =
              static_cast<LibuvEventEngine*>(server->loop->data);
          if (e->AcceptUnsafe(engine, server)) {
            l->on_accept_(std::move(e));
          }
        });
    if (r == 0) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
        gpr_log(GPR_DEBUG, "LibuvListener@%p::Start, success", uvTCP_);
      }
      ret.set_value(absl::OkStatus());
    } else {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
        gpr_log(GPR_INFO, "LibuvListener@%p::Start, failure: %i", uvTCP_, r);
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

LibuvEventEngine::TaskHandle LibuvEventEngine::Run(Callback fn,
                                                   RunOptions opts) {
  return RunAt(absl::Now(), fn, opts);
}

LibuvEventEngine::TaskHandle LibuvEventEngine::RunAt(absl::Time when,
                                                     Callback fn,
                                                     RunOptions opts) {
  LibuvTask* task = new LibuvTask(this);
  task->fn_ = std::move(fn);
  absl::Time now = absl::Now();
  uint64_t timeout;
  if (now >= when) {
    timeout = 0;
  } else {
    timeout = (when - now) / absl::Milliseconds(1);
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
    gpr_log(GPR_DEBUG,
            "LibuvTask@%p::RunAt, scheduled, timeout=%" PRIu64
            ", key = %" PRIiPTR,
            task, timeout, task->key_);
  }
  Schedule([task, timeout](LibuvEventEngine* engine) {
    intptr_t key = task->key_;
    engine->taskMap_[key] = task;
    uv_timer_init(&engine->loop_, &task->timer_);
    uv_timer_start(
        &task->timer_,
        [](uv_timer_t* timer) {
          LibuvTask* task = reinterpret_cast<LibuvTask*>(timer->data);
          if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
            gpr_log(GPR_DEBUG, "LibuvTask@%p, triggered: key = %" PRIiPTR, task,
                    task->key_);
          }
          task->Cancel();
          task->triggered_ = true;
          task->fn_(absl::OkStatus());
        },
        timeout, 0);
  });

  return {task->key_};
}

grpc_event_engine::experimental::EventEngine*
grpc_event_engine::experimental::DefaultEventEngineFactory() {
  return new LibuvEventEngine();
}

void LibuvEventEngine::TryCancel(TaskHandle handle) {
  Schedule([handle](LibuvEventEngine* engine) {
    auto it = engine->taskMap_.find(handle.keys[0]);
    if (it == engine->taskMap_.end()) return;
    if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
      gpr_log(GPR_DEBUG, "LibuvTask@%p, cancelled: key = %" PRIiPTR, it->second,
              it->second->key_);
    }
    auto* task = it->second;
    task->Cancel();
    if (!task->triggered_) {
      task->fn_(absl::CancelledError());
    }
  });
}

grpc_event_engine::experimental::EventEngine::DNSResolver::LookupTaskHandle
LibuvDNSResolver::LookupHostname(LookupHostnameCallback on_resolve,
                                 absl::string_view address,
                                 absl::string_view default_port,
                                 absl::Time deadline) {
  LibuvLookupTask* task = new LibuvLookupTask(engine_);
  task->on_resolve_ = std::move(on_resolve);
  if (!grpc_core::SplitHostPort(address, &task->address_,
                                &task->default_port_)) {
    task->address_ = std::string(address);
    task->default_port_ = std::string(default_port);
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
    gpr_log(GPR_DEBUG, "LookupHostname for %s:%s scheduled",
            task->address_.c_str(), task->default_port_.c_str());
  }
  engine_->Schedule([task, deadline](LibuvEventEngine* engine) {
    intptr_t key = task->key_;
    engine->lookupTaskMap_[key] = task;
    const char* ccaddress = task->address_.c_str();
    const char* ccdefault_port = task->default_port_.c_str();
    int r = uv_getaddrinfo(
        &engine->loop_, &task->req_,
        [](uv_getaddrinfo_t* req, int status, struct addrinfo* res) {
          LibuvLookupTask* task = reinterpret_cast<LibuvLookupTask*>(req->data);
          task->uvResolverCallback(status, res);
        },
        ccaddress, ccdefault_port, nullptr);
    if (r != 0) {
      auto on_resolve = std::move(task->on_resolve_);
      engine->EraseLookupTask(key);
      on_resolve(absl::UnknownError("Resolution error"));
      if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
        gpr_log(GPR_DEBUG, "LookupHostname for %s:%s failed early with %i",
                task->address_.c_str(), task->default_port_.c_str(), r);
      }
      return;
    }
    uv_timer_init(&engine->loop_, &task->timer_);
    absl::Duration timeout = deadline - absl::Now();
    uv_timer_start(&task->timer_, LibuvLookupTask::uvTimerCallback,
                   timeout / absl::Milliseconds(1), 0);
  });

  return {task->key_};
}

void LibuvDNSResolver::TryCancelLookup(
    grpc_event_engine::experimental::EventEngine::DNSResolver::LookupTaskHandle
        task) {
  engine_->Schedule([task](LibuvEventEngine* engine) {
    auto it = engine->lookupTaskMap_.find(task.keys[0]);
    if (it == engine->lookupTaskMap_.end()) return;
    it->second->Cancel(engine);
  });
}

void LibuvEventEngine::EraseTask(intptr_t taskKey) {
  auto it = taskMap_.find(taskKey);
  GPR_ASSERT(it != taskMap_.end());
  delete it->second;
  taskMap_.erase(it);
}

void LibuvEventEngine::EraseLookupTask(intptr_t taskKey) {
  auto it = lookupTaskMap_.find(taskKey);
  GPR_ASSERT(it != lookupTaskMap_.end());
  delete it->second;
  lookupTaskMap_.erase(it);
}

LibuvEndpoint::~LibuvEndpoint() {
  LibuvEndpointWrapper* tcp = uvTCP_;
  GetEventEngine()->Schedule([tcp](LibuvEventEngine* engine) {
    if (tcp->on_read_) {
      uv_read_stop(reinterpret_cast<uv_stream_t*>(&tcp->tcp_));
      auto cb = std::move(tcp->on_read_);
      cb(absl::CancelledError());
    }
    tcp->CloseUnsafe(0);
  });
}

void LibuvEndpoint::Write(
    grpc_event_engine::experimental::EventEngine::Callback on_writable,
    grpc_event_engine::experimental::SliceBuffer* data) {
  LibuvEndpointWrapper* tcp = uvTCP_;
  GPR_ASSERT(tcp->write_bufs_ == nullptr);
  size_t count = data->Count();
  tcp->write_bufs_ = new uv_buf_t[count];
  tcp->write_bufs_count_ = count;
  tcp->on_writable_ = std::move(on_writable);
  data->Enumerate([tcp](uint8_t* base, size_t len, size_t index) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
      std::string prefix = absl::StrFormat("LibuvEndpoint@%p::Write", tcp);
      hexdump(prefix, base, len);
    }
    tcp->write_bufs_[index].base = reinterpret_cast<char*>(base);
    tcp->write_bufs_[index].len = len;
  });
  GetEventEngine()->Schedule([tcp](LibuvEventEngine* engine) {
    uv_write(&tcp->write_req_, reinterpret_cast<uv_stream_t*>(&tcp->tcp_),
             tcp->write_bufs_, tcp->write_bufs_count_,
             [](uv_write_t* req, int status) {
               LibuvEndpointWrapper* tcp =
                   reinterpret_cast<LibuvEndpointWrapper*>(req->data);
               delete[] tcp->write_bufs_;
               tcp->write_bufs_ = nullptr;
               if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
                 gpr_log(GPR_DEBUG, "LibuvEndpoint@%p::Write completed", tcp);
               }
               switch (status) {
                 case 0:
                   tcp->on_writable_(absl::OkStatus());
                   break;
                 case UV_ECANCELED:
                   tcp->on_writable_(absl::CancelledError());
                   break;
                 default:
                   tcp->on_writable_(
                       absl::UnknownError("uv_write gave us an error"));
                   break;
               };
             });
  });
}

void LibuvEndpoint::Read(
    grpc_event_engine::experimental::EventEngine::Callback on_read,
    grpc_event_engine::experimental::SliceBuffer* buffer) {
  buffer->Clear();
  LibuvEndpointWrapper* tcp = uvTCP_;
  tcp->read_sb_ = buffer;
  tcp->on_read_ = std::move(on_read);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
    gpr_log(GPR_DEBUG, "LibuvEndpoint@%p::Read scheduled", tcp);
  }
  tcp->slice_allocator_->Allocate(
      READ_BUFFER_SIZE, tcp->read_sb_, [this, tcp](absl::Status status) {
        gpr_log(GPR_DEBUG, "allocate cb status: %s", status.ToString().c_str());
        this->GetEventEngine()->Schedule([tcp](LibuvEventEngine* engine) {
          uv_read_start(
              reinterpret_cast<uv_stream_t*>(&tcp->tcp_),
              [](uv_handle_t* handle, size_t /* suggested_size */,
                 uv_buf_t* buf) {
                LibuvEndpointWrapper* tcp =
                    reinterpret_cast<LibuvEndpointWrapper*>(handle->data);
                // TODO(nnoble): seems like we should have a way to get
                // individual Slices. discuss.
                tcp->read_sb_->Enumerate(
                    [buf](uint8_t* start, size_t len, size_t idx) {
                      if (idx == 0) {
                        buf->base = reinterpret_cast<char*>(start);
                        buf->len = len;
                      }
                    });
              },
              [](uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
                LibuvEndpointWrapper* tcp =
                    reinterpret_cast<LibuvEndpointWrapper*>(stream->data);
                uv_read_stop(stream);
                auto on_read = std::move(tcp->on_read_);
                if (nread < 0 && nread != UV_EOF) {
                  on_read(absl::UnknownError(absl::StrFormat(
                      "uv_read_start gave us an error: %i", nread)));
                  return;
                }
                if (nread == UV_EOF) {
                  tcp->read_sb_->TrimEnd(tcp->read_sb_->Length());
                  // this is unfortunate, but returning OK means there's more to
                  // read and gets us into an infinite loop.
                  on_read(absl::ResourceExhaustedError("EOF"));
                  return;
                };
                if (nread < tcp->read_sb_->Length()) {
                  tcp->read_sb_->TrimEnd(tcp->read_sb_->Length() - nread);
                }
                if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
                  std::string prefix =
                      absl::StrFormat("LibuvEndpoint@%p::Read", tcp);
                  tcp->read_sb_->Enumerate(
                      [nread](uint8_t* start, size_t len, size_t idx) {
                        hexdump("arst", start, nread);
                      });
                }
                on_read(absl::OkStatus());
              });
        });
      });
}

void LibuvListenerWrapper::Close(LibuvEventEngine* engine) {
  engine->Schedule([this](LibuvEventEngine* engine) { CloseUnsafe(0); });
}

// Copyright 2021 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <uv.h>

#include <atomic>
#include <functional>
#include <unordered_map>

#include "absl/strings/str_format.h"

#include <grpc/event_engine/event_engine.h>
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/gprpp/mpscq.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/thd.h"
#include "src/core/lib/iomgr/event_engine/promise.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/socket_utils.h"

extern grpc_core::TraceFlag grpc_tcp_trace;

using grpc_event_engine::experimental::EventEngine;

namespace {

class LibuvDNSResolver;
class LibuvEventEngine;
class LibuvListener;
class LibuvLookupTask;
class LibuvTask;

// Helper to dump network traffic in a legible manner.
// Probably belongs to a different utility file elsewhere, but wasn't sure if
// we even want to keep it eventually.
void hexdump(const std::string& prefix, const void* data_, size_t size) {
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

constexpr size_t kReadBufferSize = 4096;

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
/// The derived classes should hold all of the extra information that has to
/// follow the lifespan of the LibUV base socket, such as timers, callback
/// functors, buffers, etc.
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
  void RegisterInLibuvThread(LibuvEventEngine* engine);

  // Schedules a close of the base socket into the libuv loop. If the derived
  // class has more handles to wait on closing, like timers, then it needs
  // to tell how many in the \a extraCloses argument.
  //
  // NOTE: the derived classes USED to hold extra timers, due to the original
  // API having deadlines, but since those are gone, we're now always calling
  // this with extraCloses=0 at the moment. Should this change again in the
  // future however, this can be used again.
  void CloseInLibuvThread(int extraCloses) {
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
  // with their respective API. The idea being we won't have one object per
  // LibUV handle, but rather group all of the needed handles into a single
  // derived class, that we will delete all at once.
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

 private:
  int to_close_ = 0;
};

////////////////////////////////////////////////////////////////////////////////
/// The derived class to wrap a LibUV TCP listener handle. Its API will
/// closely match that of the Event Engine listener. Its only consumer should
/// be the LibuvListener class.
////////////////////////////////////////////////////////////////////////////////
class LibuvListenerWrapper final : public LibuvWrapperBase {
 private:
  friend class LibuvListener;

  // When LibUV is finally done with the listener, this destructor will be
  // called from the base class, which is when we can properly invoke the
  // shutdown callback.
  virtual ~LibuvListenerWrapper() { on_shutdown_(absl::OkStatus()); };

  LibuvListenerWrapper(
      Listener::AcceptCallback on_accept, Callback on_shutdown,
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

  // Schedules closing of the listener. Should be called from the proxy class'
  // destructor. Once the closing is completed, this object will automatically
  // delf-delete from the base class.
  void Close(LibuvEventEngine* engine);

  Listener::AcceptCallback on_accept_;
  Callback on_shutdown_;
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
  Callback on_writable_;
  Callback on_read_;
  ResolvedAddress peer_address_;
  ResolvedAddress local_address_;
};

////////////////////////////////////////////////////////////////////////////////
/// This class is only a tiny, very temporary shell around the engine itself,
/// and doesn't hold any state of its own. It only exists at the moment because
/// of the need to implement the DNSResolver class in EventEngine. Maybe the
/// cares implementation will change this into a more fleshed out class.
////////////////////////////////////////////////////////////////////////////////
class LibuvDNSResolver final : public DNSResolver {
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
/// It is a proxy class, and its main reason of existence is to transform its
/// destruction into scheduling a LibUV close() call of the underlying socket.
////////////////////////////////////////////////////////////////////////////////
class LibuvListener final : public Listener {
 public:
  LibuvListener(
      Listener::AcceptCallback on_accept, Callback on_shutdown,
      const grpc_event_engine::experimental::EndpointConfig& args,
      std::unique_ptr<grpc_event_engine::experimental::SliceAllocatorFactory>
          slice_allocator_factory);

  void RegisterInLibuvThread(LibuvEventEngine* engine) {
    uv_tcp_->RegisterInLibuvThread(engine);
  }

  virtual ~LibuvListener() override;

 private:
  // The LibuvEventEngine pointer is tucked away into the uv_loop_t user data.
  // Retrieve this pointer and cast it back to the LibuvEventEngine pointer.
  LibuvEventEngine* GetEventEngine() {
    return reinterpret_cast<LibuvEventEngine*>(uv_tcp_->GetLoop()->data);
  }

  virtual absl::StatusOr<int> Bind(const ResolvedAddress& addr) override;

  virtual absl::Status Start() override;

  LibuvListenerWrapper* uv_tcp_ = nullptr;
};

////////////////////////////////////////////////////////////////////////////////
/// The Event Engine Endpoint class. Aside from implementing the listener API,
/// it will hold a pointer to a LibuvEndpointWrapper, as well as the temporary
/// connection information. The initial connection information is held in it,
/// because of the limbo state the handle is in between the moment we are
/// requested to initiate the connection, and the moment we are actually
/// connected and giving back the endpoint to the callback. Other than that, it
/// essentially is a proxy class to LibuvEndpointWrapper.
///
/// It's separate from the LibuvEndpointWrapper in order to transform its
/// destruction into scheduling a LibUV close() call of the underlying socket.
////////////////////////////////////////////////////////////////////////////////
class LibuvEndpoint final : public Endpoint {
 public:
  LibuvEndpoint(const grpc_event_engine::experimental::EndpointConfig& args,
                std::unique_ptr<grpc_event_engine::experimental::SliceAllocator>
                    slice_allocator)
      : uv_tcp_(new LibuvEndpointWrapper(args, std::move(slice_allocator))) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
      gpr_log(GPR_DEBUG, "LibuvEndpoint:%p created", this);
    }
    connect_.data = this;
  }

  absl::Status Connect(LibuvEventEngine* engine, OnConnectCallback on_connect,
                       const ResolvedAddress& addr);

  void RegisterInLibuvThread(LibuvEventEngine* engine) {
    uv_tcp_->RegisterInLibuvThread(engine);
  }

  // When a listener creates an endpoint for an incoming connection, we need to
  // accept it and register it in the libuv loop. This can only be called from
  // the libuv thread. This function can be expanded into fanning out the
  // incoming connection to multiple libuv loops in the future.
  //
  // Returns true if the accept was successful.
  bool AcceptInLibuvThread(LibuvEventEngine* engine, uv_stream_t* server) {
    RegisterInLibuvThread(engine);
    int r;
    r = uv_accept(server, reinterpret_cast<uv_stream_t*>(&uv_tcp_->tcp_));
    if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
      gpr_log(GPR_DEBUG, "LibuvEndpoint@%p, accepting new connection: %i", this,
              r);
    }
    if (r == 0) {
      // Not sure what to do here if we're failing. Maybe just abort? But that
      // seems extreme given it shouldn't be a fatal error.
      PopulateAddressesInLibuvThread();
      return true;
    }
    return false;
  }

  virtual ~LibuvEndpoint() override;

 private:
  // The LibuvEventEngine pointer is tucked away into the uv_loop_t user data.
  // Retrieve this pointer and cast it back to the LibuvEventEngine pointer.
  LibuvEventEngine* GetEventEngine() {
    return reinterpret_cast<LibuvEventEngine*>(uv_tcp_->GetLoop()->data);
  }

  // Fills out the local and peer addresses of the connected socket. Needs to be
  // called from the libuv loop thread. Returns 0 if successful.
  int PopulateAddressesInLibuvThread() {
    auto populate =
        [this](std::function<int(const uv_tcp_t*, struct sockaddr*, int*)> f,
               ResolvedAddress* a) {
          int namelen;
          sockaddr_storage addr;
          int ret =
              f(&uv_tcp_->tcp_, reinterpret_cast<sockaddr*>(&addr), &namelen);
          *a = ResolvedAddress(reinterpret_cast<sockaddr*>(&addr), namelen);
          return ret;
        };
    int r = 0;
    r |= populate(uv_tcp_getsockname, &uv_tcp_->local_address_);
    r |= populate(uv_tcp_getpeername, &uv_tcp_->peer_address_);
    if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
      gpr_log(GPR_DEBUG, "LibuvEndpoint@%p::populateAddresses, r=%d", uv_tcp_,
              r);
    }
    return r;
  }

  virtual void Read(
      Callback on_read,
      grpc_event_engine::experimental::SliceBuffer* buffer) override;

  virtual void Write(
      Callback on_writable,
      grpc_event_engine::experimental::SliceBuffer* data) override;

  virtual const ResolvedAddress& GetPeerAddress() const override {
    return uv_tcp_->peer_address_;
  };

  virtual const ResolvedAddress& GetLocalAddress() const override {
    return uv_tcp_->local_address_;
  };

  LibuvEndpointWrapper* uv_tcp_ = nullptr;
  uv_connect_t connect_;
  OnConnectCallback on_connect_ = nullptr;
};

////////////////////////////////////////////////////////////////////////////////
/// The LibUV Event Engine itself. It implements an EventEngine class.
////////////////////////////////////////////////////////////////////////////////
class LibuvEventEngine final
    : public grpc_event_engine::experimental::EventEngine {
  // Since libuv is single-threaded and not thread-safe, we will be running
  // all operations in a multi-producer/single-consumer manner, where all of the
  // surface API of the EventEngine will only just schedule work to be executed
  // on the libuv thread. This structure holds one of these piece of work to
  // execute. Each "work" is just a standard functor that takes the
  // LibuvEventEngine pointer as an argument, in an attempt to lower capture
  // costs.
  struct SchedulingRequest : grpc_core::MultiProducerSingleConsumerQueue::Node {
    typedef std::function<void(LibuvEventEngine*)> functor;
    SchedulingRequest(functor&& f) : f(std::move(f)) {}
    functor f;
  };

 public:
  LibuvEventEngine() {
    bool success = false;
    if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
      gpr_log(GPR_DEBUG, "LibuvEventEngine:%p created", this);
    }
    // Creating the libuv loop thread straight upon construction. We don't need
    // the thread to be joinable, since it'll exit on its own gracefully without
    // needing for us to wait on it.
    grpc_core::Thread::Options options;
    options.set_joinable(false);
    // Why isn't grpc_core::Thread accepting a lambda?
    thread_ = grpc_core::Thread(
        "uv loop",
        [](void* arg) {
          LibuvEventEngine* engine = reinterpret_cast<LibuvEventEngine*>(arg);
          engine->Thread();
        },
        this, &success, options);
    thread_.Start();
    GPR_ASSERT(success);
    // This promise will be set to true once the thread has fully started and is
    // operational, so let's wait on it.
    success = ready_.Get();
    GPR_ASSERT(success);
  }

  // Schedules one lambda to be executed on the libuv thread. Our libuv loop
  // will have a special async event which is the only piece of API that's
  // marked as thread-safe.
  void RunInLibuvThread(SchedulingRequest::functor&& f) {
    SchedulingRequest* request = new SchedulingRequest(std::move(f));
    if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
      gpr_log(GPR_ERROR, "LibuvEventEngine@%p::RunInLibuvThread, created %p",
              this, request);
    }
    queue_.Push(request);
    uv_async_send(&kicker_);
  }

  uv_loop_t* GetLoop() { return &loop_; }

 private:
  // This is the callback that libuv will call on its thread once the
  // uv_async_send call above is being processed. The kick is only guaranteed to
  // be called once per loop iteration, even if we sent the event multiple
  // times, so we have to process as many events from the queue as possible.
  void Kicker() {
    bool empty = false;
    while (!empty) {
      SchedulingRequest* node =
          reinterpret_cast<SchedulingRequest*>(queue_.PopAndCheckEnd(&empty));
      if (node != nullptr) continue;
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
    // Ugh. LibUV doesn't take upon itself to mask SIGPIPE on its own on Unix
    // systems. If a connection gets broken, we will get killed, unless we mask
    // it out. These 4 lines of code likely need to be enclosed in a non-Windows
    // #ifdef check, although I'm not certain what platforms will or will not
    // have the necessary function calls here.
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGPIPE);
    pthread_sigmask(SIG_BLOCK, &set, NULL);

    // Setting up the loop.
    worker_thread_id_ = std::this_thread::get_id();
    int r = uv_loop_init(&loop_);
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
      ready_.Set(false);
      return;
    }
    ready_.Set(true);

    // The meat of running our event loop. We need the various exec contexts,
    // because some of the callbacks we will call will depend on them existing.
    //
    // Calling uv_run with UV_RUN_ONCE will stall until there is any sort of
    // event to process whatsoever, and then will return 0 if it needs to
    // shutdown. The libuv loop will shutdown naturally when there's no more
    // event to process. Since we created the async event for kick, there will
    // always be at least one event holding the loop, until we explicitly weaken
    // its reference to permit a graceful shutdown.
    //
    // When/if there are no longer any sort of exec contexts needed to flush,
    // then we can simply use UV_RUN instead, which will never return until
    // there's no more events to process, which is even more graceful.
    grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
    grpc_core::ExecCtx ctx;
    while (uv_run(&loop_, UV_RUN_ONCE) != 0) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
        gpr_log(GPR_ERROR,
                "LibuvEventEngine@%p::Thread, uv_run requests a "
                "context flush",
                this);
      }
      ctx.Flush();
      callback_exec_ctx.Flush();
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
        std::move(on_accept), std::move(on_shutdown), args,
        std::move(slice_allocator_factory));
    if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
      gpr_log(GPR_DEBUG, "LibuvEventEngine@%p::CreateListener, created %p",
              this, ret.get());
    }
    // Scheduling should have a guarantee on ordering. We don't need to wait
    // until this is finished to return. Any subsequent call on the returned
    // listener will schedule more work after this one.
    RunInLibuvThread([&ret](LibuvEventEngine* engine) {
      ret->RegisterInLibuvThread(engine);
    });
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

  virtual absl::StatusOr<std::unique_ptr<DNSResolver>> GetDNSResolver()
      override {
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
    RunInLibuvThread([](LibuvEventEngine* engine) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
        gpr_log(GPR_DEBUG,
                "LibuvEventEngine@%p shutting down, unreferencing "
                "Kicker now",
                engine);
      }
      // Shutting down at this point is essentially just this unref call here.
      // After it, the libuv loop will continue working until it has no more
      // events to monitor. It means that scheduling new work becomes
      // essentially undefined behavior, which is in line with our surface API
      // contracts, which stipulate the same thing.
      uv_unref(reinterpret_cast<uv_handle_t*>(&engine->kicker_));
      if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
        // This is an unstable API from libuv that we use for its intended
        // purpose: debugging. This can tell us if there's lingering handles
        // that are still going to hold up the loop at this point.
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

  // Helpers for the internal Task classes.
  void EraseTask(intptr_t taskKey);
  void EraseLookupTask(intptr_t taskKey);

  uv_loop_t loop_;
  uv_async_t kicker_;
  // This should be set only once to true by the thread when it's done setting
  // itself up.
  grpc_event_engine::experimental::Promise<bool> ready_;
  grpc_core::Thread thread_;
  grpc_core::MultiProducerSingleConsumerQueue queue_;

  // We keep a list of all of the tasks here. The atomics will serve as a simple
  // counter mechanism, with the assumption that if it ever rolls over, the
  // colliding tasks will have long been completed.
  //
  // NOTE: now that we're returning two intptr_t instead of just one for the
  // keys, this can be improved, as we can hold the pointer in one
  // key, and a tag in the other, to avoid the ABA problem. We'll keep the
  // atomics as tags in the second key slot, but we can get rid of the maps.
  //
  // TODO(nnoble): remove the maps, and fold the pointers into the keys,
  // alongside the ABA tag.
  std::atomic<intptr_t> task_key_;
  std::atomic<intptr_t> lookup_task_key_;
  std::unordered_map<intptr_t, LibuvTask*> task_map_;
  std::unordered_map<intptr_t, LibuvLookupTask*> lookup_task_map_;
  Callback on_shutdown_complete_;

  // Hopefully temporary until we can solve shutdown from the main grpc code.
  // Used by IsWorkerThread.
  std::thread::id worker_thread_id_;

  friend class LibuvDNSResolver;
  friend class LibuvLookupTask;
  friend class LibuvTask;
};

////////////////////////////////////////////////////////////////////////////////
/// The LibuvTask class will be used for Run and RunAt from LibuvEventEngine,
/// and will be internally what's allocated for the returned TaskHandle.
///
/// Its API is to be used solely by the Run and RunAt functions, while in the
/// libuv loop thread.
////////////////////////////////////////////////////////////////////////////////
class LibuvTask {
 public:
  LibuvTask(LibuvEventEngine* engine, Callback&& fn)
      : fn_(std::move(fn)), key_(engine->task_key_.fetch_add(1)) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
      gpr_log(GPR_DEBUG, "LibuvTask@%p, created: key = %" PRIiPTR, this, key_);
    }
    timer_.data = this;
  }

  void StartInLibuvThread(LibuvEventEngine* engine, uint64_t timeout) {
    uv_timer_init(&engine->loop_, &timer_);
    uv_timer_start(
        &timer_,
        [](uv_timer_t* timer) {
          LibuvTask* task = reinterpret_cast<LibuvTask*>(timer->data);
          if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
            gpr_log(GPR_DEBUG, "LibuvTask@%p, triggered: key = %" PRIiPTR, task,
                    task->Key());
          }
          task->CancelInLibuvThread();
          task->triggered_ = true;
          task->fn_(absl::OkStatus());
        },
        timeout, 0);
  }

  void TryCancelInLibuvThread() {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
      gpr_log(GPR_DEBUG, "LibuvTask@%p, cancelled: key = %" PRIiPTR, this,
              key_);
    }
    CancelInLibuvThread();
    if (!triggered_) {
      fn_(absl::CancelledError());
    }
  }

  // This one will be used by both TryCancel and the completion callback.
  void CancelInLibuvThread() {
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

  intptr_t Key() { return key_; }

 private:
  Callback fn_;
  bool triggered_ = false;
  uv_timer_t timer_;
  const intptr_t key_;
};

////////////////////////////////////////////////////////////////////////////////
/// The LibuvLookupTask class will be used exclusively by the LibuvEventEngine's
/// resolver functions.
///
/// Its API is to be used solely while in the libuv loop thread.
////////////////////////////////////////////////////////////////////////////////
class LibuvLookupTask {
 public:
  LibuvLookupTask(LibuvEventEngine* engine,
                  DNSResolver::LookupHostnameCallback on_resolve,
                  absl::string_view address, absl::string_view default_port)
      : key_(engine->lookup_task_key_.fetch_add(1)),
        on_resolve_(std::move(on_resolve)) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
      gpr_log(GPR_DEBUG, "LibuvLookupTask@%p, created: key = %" PRIiPTR, this,
              key_);
    }
    req_.data = this;
    timer_.data = this;
    if (!grpc_core::SplitHostPort(address, &address_, &default_port_)) {
      address_ = std::string(address);
      default_port_ = std::string(default_port);
    }
    if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
      gpr_log(GPR_DEBUG, "LookupHostname for %s:%s", address_.c_str(),
              default_port_.c_str());
    }
  }

  intptr_t Key() { return key_; }

  void StartInLibuvThread(LibuvEventEngine* engine, absl::Time deadline) {
    const char* const ccaddress = address_.c_str();
    const char* const ccdefault_port = default_port_.c_str();
    // Start resolving, and if successful, call the main resolver callback code.
    int r = uv_getaddrinfo(
        &engine->loop_, &req_,
        [](uv_getaddrinfo_t* req, int status, struct addrinfo* res) {
          LibuvLookupTask* task = reinterpret_cast<LibuvLookupTask*>(req->data);
          task->LibuvResolverCallback(status, res);
        },
        ccaddress, ccdefault_port, nullptr);
    // If we weren't successful in starting the resolution, our callback will
    // never be called, so we can simply fast-abort now.
    if (r != 0) {
      auto on_resolve = std::move(on_resolve_);
      engine->EraseLookupTask(key_);
      on_resolve(absl::UnknownError("Resolution error"));
      if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
        gpr_log(GPR_DEBUG, "LookupHostname for %s:%s failed early with %i",
                address_.c_str(), default_port_.c_str(), r);
      }
      return;
    }
    // Otherwise, set our timer, and request a cancellation of the lookup. The
    // uv_cancel call will fail if the call already finished or was already
    // canceled, which is fine with us, and we don't need take any further
    // action.
    uv_timer_init(&engine->loop_, &timer_);
    absl::Duration timeout = deadline - absl::Now();
    uv_timer_start(
        &timer_,
        [](uv_timer_t* timer) {
          LibuvLookupTask* task =
              reinterpret_cast<LibuvLookupTask*>(timer->data);
          task->deadline_exceeded_ = true;
          uv_cancel(reinterpret_cast<uv_req_t*>(&task->req_));
        },
        timeout / absl::Milliseconds(1), 0);
  }

  void CancelInLibuvThread(LibuvEventEngine* engine) {
    uv_timer_stop(&timer_);
    uv_cancel(reinterpret_cast<uv_req_t*>(&req_));
  }

 private:
  std::string address_;
  std::string default_port_;
  uv_getaddrinfo_t req_;
  uv_timer_t timer_;
  bool deadline_exceeded_ = false;
  const intptr_t key_;
  DNSResolver::LookupHostnameCallback on_resolve_;

  // Unlike other callbacks in this code, this one is guaranteed to only be
  // called once by libuv.
  void LibuvResolverCallback(int status, struct addrinfo* res) {
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
      std::vector<ResolvedAddress> ret;

      for (p = res; p != nullptr; p = p->ai_next) {
        ret.emplace_back(p->ai_addr, p->ai_addrlen);
      }

      uv_freeaddrinfo(res);
      on_resolve_(std::move(ret));
    }
  }
};

// Some implementation details require circular dependencies between classes, so
// we're implementing them here instead.
absl::Status LibuvEndpoint::Connect(LibuvEventEngine* engine,
                                    OnConnectCallback on_connect,
                                    const ResolvedAddress& addr) {
  on_connect_ = std::move(on_connect);
  engine->RunInLibuvThread([addr, this](LibuvEventEngine* engine) {
    uv_tcp_->RegisterInLibuvThread(engine);
    int r = uv_tcp_connect(
        &connect_, &uv_tcp_->tcp_, addr.address(),
        [](uv_connect_t* req, int status) {
          LibuvEndpoint* epRaw = reinterpret_cast<LibuvEndpoint*>(req->data);
          std::unique_ptr<LibuvEndpoint> ep(epRaw);
          auto on_connect = std::move(ep->on_connect_);
          if (status == 0) {
            ep->PopulateAddressesInLibuvThread();
            if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
              gpr_log(GPR_DEBUG, "LibuvEndpoint@%p::Connect, success",
                      epRaw->uv_tcp_);
            }
            on_connect(std::move(ep));
          } else {
            if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
              gpr_log(GPR_INFO, "LibuvEndpoint@%p::Connect, failed: %i",
                      epRaw->uv_tcp_, status);
            }
            on_connect(absl::UnknownError(
                "uv_tcp_connect gave us an asynchronous error"));
          }
        });
    // If we fail the call to uv_connect, the handle won't even be monitored by
    // the libuv, and won't need to be closed (it won't be in an opened state).
    // We can bail early here.
    if (r != 0) {
      auto on_connect = std::move(on_connect_);
      if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
        gpr_log(GPR_INFO, "LibuvEndpoint@%p::Connect, failed: %i", uv_tcp_, r);
      }
      delete this;
      on_connect(absl::UnknownError("uv_tcp_connect gave us an error"));
    }
  });
  return absl::OkStatus();
}

void LibuvWrapperBase::RegisterInLibuvThread(LibuvEventEngine* engine) {
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
    Listener::AcceptCallback on_accept, Callback on_shutdown,
    const grpc_event_engine::experimental::EndpointConfig& args,
    std::unique_ptr<grpc_event_engine::experimental::SliceAllocatorFactory>
        slice_allocator_factory)
    : uv_tcp_(new LibuvListenerWrapper(on_accept, on_shutdown, args,
                                       std::move(slice_allocator_factory))) {
  uv_tcp_->tcp_.data = uv_tcp_;
}

LibuvListener::~LibuvListener() { uv_tcp_->Close(GetEventEngine()); }

absl::StatusOr<int> LibuvListener::Bind(const ResolvedAddress& addr) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
    grpc_resolved_address grpcaddr;
    grpcaddr.len = addr.size();
    memcpy(grpcaddr.addr, addr.address(), grpcaddr.len);
    gpr_log(GPR_DEBUG, "LibuvListener@%p::Bind to %s", uv_tcp_,
            grpc_sockaddr_to_uri(&grpcaddr).c_str());
  }
  grpc_event_engine::experimental::Promise<absl::StatusOr<int>> p;
  GetEventEngine()->RunInLibuvThread([this, &p,
                                      &addr](LibuvEventEngine* engine) {
    // Most of this code is boilerplate to get the bound port back out.
    int r = uv_tcp_bind(&uv_tcp_->tcp_, addr.address(), 0 /* flags */);
    switch (r) {
      case UV_EINVAL:
        p.Set(absl::InvalidArgumentError(
            "uv_tcp_bind said we passed an invalid argument to it"));
        return;
      case 0:
        break;
      default:
        if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
          gpr_log(GPR_INFO, "LibuvListener@%p::Bind, uv_tcp_bind failed: %i",
                  uv_tcp_, r);
        }
        p.Set(absl::UnknownError(
            "uv_tcp_bind returned an error code we don't know about"));
        return;
    }

    sockaddr_storage bound_addr;
    int addr_len = sizeof(bound_addr);
    r = uv_tcp_getsockname(&uv_tcp_->tcp_,
                           reinterpret_cast<sockaddr*>(&bound_addr), &addr_len);
    switch (r) {
      case UV_EINVAL:
        p.Set(absl::InvalidArgumentError(
            "uv_tcp_getsockname said we passed an invalid argument to it"));
        return;
      case 0:
        break;
      default:
        if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
          gpr_log(GPR_INFO,
                  "LibuvListener@%p::Bind, uv_tcp_getsockname failed: %i",
                  uv_tcp_, r);
        }
        p.Set(absl::UnknownError(
            "uv_tcp_getsockname returned an error code we don't know about"));
        return;
    }
    switch (bound_addr.ss_family) {
      case AF_INET: {
        sockaddr_in* sin = (sockaddr_in*)&bound_addr;
        p.Set(grpc_htons(sin->sin_port));
        break;
      }
      case AF_INET6: {
        sockaddr_in6* sin6 = (sockaddr_in6*)&bound_addr;
        p.Set(grpc_htons(sin6->sin6_port));
        break;
      }
      default:
        if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
          gpr_log(GPR_INFO, "LibuvListener@%p::Bind, unknown addr family: %i",
                  uv_tcp_, bound_addr.ss_family);
        }
        p.Set(absl::InvalidArgumentError(
            "returned socket address in :Bind is neither IPv4 nor IPv6"));
        return;
    }

    if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
      gpr_log(GPR_DEBUG, "LibuvListener@%p::Bind, success", uv_tcp_);
    }
  });
  return p.Get();
}

absl::Status LibuvListener::Start() {
  grpc_event_engine::experimental::Promise<absl::Status> ret;
  GetEventEngine()->RunInLibuvThread([this, &ret](LibuvEventEngine* engine) {
    int r;
    // The callback to uv_listen will be invoked every time there is a new
    // connection coming up. The job then is to create a new endpoint, and
    // accept the incoming connection on it.
    r = uv_listen(
        reinterpret_cast<uv_stream_t*>(&uv_tcp_->tcp_), 42 /* backlog */,
        [](uv_stream_t* server, int status) {
          if (status < 0) return;
          LibuvListenerWrapper* l =
              static_cast<LibuvListenerWrapper*>(server->data);
          std::unique_ptr<LibuvEndpoint> e = absl::make_unique<LibuvEndpoint>(
              l->args_, l->slice_allocator_factory_->CreateSliceAllocator(
                            "TODO(nnoble): get peer name"));
          LibuvEventEngine* engine =
              static_cast<LibuvEventEngine*>(server->loop->data);
          // If we fail accepting, we don't need to do anything, as the
          // unique_ptr will take care of deleting the failed endpoint.
          if (e->AcceptInLibuvThread(engine, server)) {
            l->on_accept_(std::move(e));
          }
        });
    if (r == 0) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
        gpr_log(GPR_DEBUG, "LibuvListener@%p::Start, success", uv_tcp_);
      }
      ret.Set(absl::OkStatus());
    } else {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
        gpr_log(GPR_INFO, "LibuvListener@%p::Start, failure: %i", uv_tcp_, r);
      }
      ret.Set(absl::UnknownError(
          "uv_listen returned an error code we don't know about"));
    }
  });
  auto status = ret.Get();
  // until we can handle "SO_REUSEPORT", always return OkStatus.
  // return status;
  return absl::OkStatus();
}

// Might as well just reuse the same codepath for both cases ¯\_(ツ)_/¯
LibuvEventEngine::TaskHandle LibuvEventEngine::Run(Callback fn,
                                                   RunOptions opts) {
  return RunAt(absl::Now(), fn, opts);
}

LibuvEventEngine::TaskHandle LibuvEventEngine::RunAt(absl::Time when,
                                                     Callback fn,
                                                     RunOptions opts) {
  LibuvTask* task = new LibuvTask(this, std::move(fn));
  absl::Time now = absl::Now();
  uint64_t timeout;
  // Since libuv doesn't have a concept of negative timeout, we need to clamp
  // this to avoid almost-infinite timers.
  if (now >= when) {
    timeout = 0;
  } else {
    timeout = (when - now) / absl::Milliseconds(1);
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
    gpr_log(GPR_DEBUG,
            "LibuvTask@%p::RunAt, scheduled, timeout=%" PRIu64
            ", key = %" PRIiPTR,
            task, timeout, task->Key());
  }
  RunInLibuvThread([task, timeout](LibuvEventEngine* engine) {
    intptr_t key = task->Key();
    engine->task_map_[key] = task;
    task->StartInLibuvThread(engine, timeout);
  });

  return {task->Key()};
}

void LibuvEventEngine::TryCancel(TaskHandle handle) {
  RunInLibuvThread([handle](LibuvEventEngine* engine) {
    auto it = engine->task_map_.find(handle.keys[0]);
    if (it == engine->task_map_.end()) return;
    auto* task = it->second;
    task->TryCancelInLibuvThread();
  });
}

DNSResolver::LookupTaskHandle LibuvDNSResolver::LookupHostname(
    LookupHostnameCallback on_resolve, absl::string_view address,
    absl::string_view default_port, absl::Time deadline) {
  LibuvLookupTask* task = new LibuvLookupTask(engine_, std::move(on_resolve),
                                              address, default_port);
  engine_->RunInLibuvThread([task, deadline](LibuvEventEngine* engine) {
    intptr_t key = task->Key();
    engine->lookup_task_map_[key] = task;
    task->StartInLibuvThread(engine, deadline);
  });

  return {task->Key()};
}

void LibuvDNSResolver::TryCancelLookup(DNSResolver::LookupTaskHandle task) {
  engine_->RunInLibuvThread([task](LibuvEventEngine* engine) {
    auto it = engine->lookup_task_map_.find(task.keys[0]);
    if (it == engine->lookup_task_map_.end()) return;
    it->second->CancelInLibuvThread(engine);
  });
}

void LibuvEventEngine::EraseTask(intptr_t taskKey) {
  auto it = task_map_.find(taskKey);
  GPR_ASSERT(it != task_map_.end());
  delete it->second;
  task_map_.erase(it);
}

void LibuvEventEngine::EraseLookupTask(intptr_t taskKey) {
  auto it = lookup_task_map_.find(taskKey);
  GPR_ASSERT(it != lookup_task_map_.end());
  delete it->second;
  lookup_task_map_.erase(it);
}

LibuvEndpoint::~LibuvEndpoint() {
  LibuvEndpointWrapper* tcp = uv_tcp_;
  // When the proxy class is deleted, schedule a stop of our reads, and finally
  // close the socket.
  //
  // Question: what do we do if this proxy class is deleted, but we still get a
  // read from the other thread? The on_read callback will still get called
  // while this is getting scheduled. Is this okay?
  GetEventEngine()->RunInLibuvThread([tcp](LibuvEventEngine* engine) {
    if (tcp->on_read_) {
      uv_read_stop(reinterpret_cast<uv_stream_t*>(&tcp->tcp_));
      auto on_read = std::move(tcp->on_read_);
      on_read(absl::CancelledError());
    }
    tcp->CloseInLibuvThread(0);
  });
}

void LibuvEndpoint::Write(Callback on_writable,
                          grpc_event_engine::experimental::SliceBuffer* data) {
  LibuvEndpointWrapper* tcp = uv_tcp_;
  GPR_ASSERT(tcp->write_bufs_ == nullptr);
  // LibUV obviously doesn't understand the concept of grpc slices, and has its
  // own structure that can look fairly similar at a glance. We'll have to
  // create as many of these libuv write buffers as we have input slices, and
  // set them all up with all the pointers and sizes from the slices themselves.
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
  GetEventEngine()->RunInLibuvThread([tcp](LibuvEventEngine* engine) {
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

void LibuvEndpoint::Read(Callback on_read,
                         grpc_event_engine::experimental::SliceBuffer* buffer) {
  buffer->Clear();
  LibuvEndpointWrapper* tcp = uv_tcp_;
  // The read case is a tiny bit of a square peg round hole situation,
  // unfortunately. When libuv starts a read, it'll call us back with a request
  // to allocate data, but in the grpc case, we want this the other way around,
  // for the resource quota allocator, especially since the libuv callback has
  // to return immediately with available memory.
  //
  // Also, libuv works in a firehose fashion, meaning it'll call the allocation
  // + read callbacks continuously, until stopped. Since we can't know in
  // advance if we are going to continue reading or not, and since we may need
  // to delay due to the resource quota, we'll need to tell libuv to stop
  // reading immediately when we get a read callback.
  //
  // We may be able to cheese this somehow by keeping a few slices in advance,
  // and reusing them instead of waiting for the resource allocation to
  // complete, but that'd be further iteration on this code for later.
  tcp->read_sb_ = buffer;
  tcp->on_read_ = std::move(on_read);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
    gpr_log(GPR_DEBUG, "LibuvEndpoint@%p::Read scheduled", tcp);
  }
  tcp->slice_allocator_->Allocate(
      kReadBufferSize, tcp->read_sb_, [this, tcp](absl::Status status) {
        gpr_log(GPR_DEBUG, "allocate cb status: %s", status.ToString().c_str());
        this->GetEventEngine()->RunInLibuvThread(
            [tcp](LibuvEventEngine* engine) {
              uv_read_start(
                  reinterpret_cast<uv_stream_t*>(&tcp->tcp_),
                  // The allocator callback from libuv to us asking for memory
                  // to write to to complete the read operation.
                  [](uv_handle_t* handle, size_t /* suggested_size */,
                     uv_buf_t* buf) {
                    LibuvEndpointWrapper* tcp =
                        reinterpret_cast<LibuvEndpointWrapper*>(handle->data);
                    // TODO(nnoble): Rework this with iterators when we have
                    // them.
                    tcp->read_sb_->Enumerate(
                        [buf](uint8_t* start, size_t len, size_t idx) {
                          if (idx == 0) {
                            buf->base = reinterpret_cast<char*>(start);
                            buf->len = len;
                          }
                        });
                  },
                  // The actual read callback from libuv.
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
                      // This is unfortunate, but returning OK means there's
                      // more to read and gets us into an infinite loop.
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
  // We don't have any other handle to close, so this is a straight call to
  // CloseInLibuvThread.
  engine->RunInLibuvThread(
      [this](LibuvEventEngine* engine) { CloseInLibuvThread(0); });
}

}  // namespace

// The only exposed function, which acts as our entry point.
grpc_event_engine::experimental::EventEngine*
grpc_event_engine::experimental::DefaultEventEngineFactory() {
  return new LibuvEventEngine();
}

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

#include <grpc/impl/codegen/port_platform.h>

#include <functional>
#include <thread>
#include <unordered_map>

#include <uv.h>

#include "absl/strings/str_format.h"

#include <grpc/event_engine/event_engine.h>

#include "src/core/lib/event_engine/uv/libuv_event_engine.h"
#include "src/core/lib/gprpp/mpscq.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/thd.h"
#include "src/core/lib/iomgr/event_engine/promise.h"
#include "src/core/lib/iomgr/exec_ctx.h"

extern grpc_core::TraceFlag grpc_tcp_trace;

namespace grpc_event_engine {
namespace experimental {

namespace {
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

struct SchedulingRequest : grpc_core::MultiProducerSingleConsumerQueue::Node {
  typedef std::function<void(LibuvEventEngine*)> functor;
  SchedulingRequest(functor&& f) : f(std::move(f)) {}
  functor f;
};

}  // namespace

////////////////////////////////////////////////////////////////////////////////
/// The LibuvTask class will be used for Run and RunAt from LibuvEventEngine,
/// and will be internally what's allocated for the returned TaskHandle.
///
/// Its API is to be used solely by the Run and RunAt functions, while in the
/// libuv loop thread.
////////////////////////////////////////////////////////////////////////////////
class LibuvTask {
 public:
  LibuvTask(LibuvEventEngine* engine, std::function<void()>&& fn);
  /// Executes the held \a fn_ and removes itself from EventEngine's accounting.
  /// Must be called from within the libuv thread.
  void Start(LibuvEventEngine* engine, uint64_t timeout);
  /// Cancel this task.
  /// The promise meanings are the same as in \a EventEngine::Cancel.
  /// Must be called from within the libuv thread.
  /// Precondition: the EventEngine must be tracking this task.
  void Cancel(Promise<bool>& will_be_cancelled);
  // A callback passed to uv_close to erase the timer from the EventEngine
  static void Erase(uv_handle_t* handle);
  intptr_t Key() { return key_; }

 private:
  std::function<void()> fn_;
  bool ran_ = false;
  uv_timer_t timer_;
  const intptr_t key_;
};

// TODO(hork): keys need to be recycled.
LibuvTask::LibuvTask(LibuvEventEngine* engine, std::function<void()>&& fn)
    : fn_(std::move(fn)), key_(engine->task_key_.fetch_add(1)) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
    gpr_log(GPR_DEBUG, "LibuvTask@%p, created: key = %" PRIiPTR, this, key_);
  }
  timer_.data = this;
}

// Must be called from libuv thread
void LibuvTask::Start(LibuvEventEngine* engine, uint64_t timeout) {
  uv_timer_init(&engine->loop_, &timer_);
  uv_timer_start(
      &timer_,
      [](uv_timer_t* timer) {
        uv_timer_stop(timer);
        LibuvTask* task = reinterpret_cast<LibuvTask*>(timer->data);
        if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
          gpr_log(GPR_DEBUG, "LibuvTask@%p, triggered: key = %" PRIiPTR, task,
                  task->Key());
        }
        uv_close(reinterpret_cast<uv_handle_t*>(timer), &LibuvTask::Erase);
        task->ran_ = true;
        task->fn_();
      },
      timeout, 0);
}

// Must be called from libuv thread
// Precondition: the EventEngine must be tracking this timer.
void LibuvTask::Cancel(Promise<bool>& will_be_cancelled) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
    gpr_log(GPR_DEBUG, "LibuvTask@%p, cancelled: key = %" PRIiPTR, this, key_);
  }
  if (uv_is_closing(reinterpret_cast<uv_handle_t*>(&timer_)) != 0) {
    // TODO(hork): check if this can be called on uv shutdown
    GPR_ASSERT(ran_);
    will_be_cancelled.Set(false);
    return;
  }
  will_be_cancelled.Set(true);
  uv_timer_stop(&timer_);
  uv_close(reinterpret_cast<uv_handle_t*>(&timer_), &LibuvTask::Erase);
}

void LibuvTask::Erase(uv_handle_t* handle) {
  uv_timer_t* timer = reinterpret_cast<uv_timer_t*>(handle);
  LibuvTask* task = reinterpret_cast<LibuvTask*>(timer->data);
  LibuvEventEngine* engine =
      reinterpret_cast<LibuvEventEngine*>(timer->loop->data);
  engine->EraseTask(task->key_);
}

LibuvEventEngine::LibuvEventEngine() {
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
        engine->RunThread();
      },
      this, &success, options);
  thread_.Start();
  GPR_ASSERT(success);
  // This promise will be set to true once the thread has fully started and is
  // operational, so let's wait on it.
  success = ready_.Get();
  GPR_ASSERT(success);
}

LibuvEventEngine::~LibuvEventEngine() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
    gpr_log(GPR_DEBUG, "LibuvEventEngine@%p::~LibuvEventEngine", this);
  }
  RunInLibuvThread([](LibuvEventEngine* engine) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
      gpr_log(GPR_DEBUG,
              "LibuvEventEngine@%p shutting down, unreferencing Kicker now",
              engine);
    }
    // Shutting down at this point is essentially just this unref call here.
    // After it, the libuv loop will continue working until it has no more
    // events to monitor. It means that scheduling new work becomes essentially
    // undefined behavior, which is in line with our surface API contracts,
    // which stipulate the same thing.
    uv_unref(reinterpret_cast<uv_handle_t*>(&engine->kicker_));
    uv_stop(&engine->loop_);
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
                    "in shutdown, handle %p type %s has references: %s", handle,
                    name, uv_has_ref(handle) ? "yes" : "no");
            uv_close(handle, nullptr);
          },
          nullptr);
    }
  });
  GPR_ASSERT(perish_.Get());
}

// Since libuv is single-threaded and not thread-safe, we will be running all
// operations in a multi-producer/single-consumer manner, where all of the
// surface API of the EventEngine will only just schedule work to be executed on
// the libuv thread. This structure holds one of these piece of work to execute.
// Each "work" is just a standard functor that takes the LibuvEventEngine
// pointer as an argument, in an attempt to lower capture costs.
void LibuvEventEngine::RunInLibuvThread(SchedulingRequest::functor&& f) {
  SchedulingRequest* request = new SchedulingRequest(std::move(f));
  if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
    gpr_log(GPR_ERROR, "LibuvEventEngine@%p::RunInLibuvThread, created %p",
            this, request);
  }
  queue_.Push(request);
  uv_async_send(&kicker_);
}

// This is the callback that libuv will call on its thread once the
// uv_async_send call above is being processed. The kick is only guaranteed
// to be called once per loop iteration, even if we sent the event multiple
// times, so we have to process as many events from the queue as possible.
void LibuvEventEngine::Kicker() {
  bool empty = false;
  while (!empty) {
    SchedulingRequest* node =
        reinterpret_cast<SchedulingRequest*>(queue_.PopAndCheckEnd(&empty));
    if (node == nullptr) continue;
    SchedulingRequest::functor f = std::move(node->f);
    if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
      gpr_log(GPR_ERROR, "LibuvEventEngine@%p::Kicker, got %p", this, node);
    }
    delete node;
    f(this);
  }
}

void LibuvEventEngine::RunThread() {
  // Ugh. LibUV doesn't take upon itself to mask SIGPIPE on its own on Unix
  // systems. If a connection gets broken, we will get killed, unless we
  // mask it out. These 4 lines of code likely need to be enclosed in a
  // non-Windows #ifdef check, although I'm not certain what platforms will
  // or will not have the necessary function calls here.
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
  // because some of the callbacks we will call will depend on them
  // existing.
  //
  // Calling uv_run with UV_RUN_ONCE will stall until there is any sort of
  // event to process whatsoever, and then will return 0 if it needs to
  // shutdown. The libuv loop will shutdown naturally when there's no more
  // event to process. Since we created the async event for kick, there will
  // always be at least one event holding the loop, until we explicitly
  // weaken its reference to permit a graceful shutdown.
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
    if (!uv_loop_alive(&loop_)) {
      break;
    }
  }

  if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
    gpr_log(GPR_DEBUG, "LibuvEventEngine@%p::Thread, shutting down", this);
  }
  perish_.Set(true);
}

void LibuvEventEngine::Run(std::function<void()> fn) {
  RunAt(absl::Now(), std::move(fn));
}

EventEngine::TaskHandle LibuvEventEngine::RunAt(absl::Time when,
                                                std::function<void()> fn) {
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
    task->Start(engine, timeout);
  });

  return {task->Key()};
}

bool LibuvEventEngine::Cancel(EventEngine::TaskHandle handle) {
  Promise<bool> will_be_cancelled;
  RunInLibuvThread([handle, &will_be_cancelled](LibuvEventEngine* engine) {
    auto it = engine->task_map_.find(handle.keys[0]);
    if (it == engine->task_map_.end()) {
      will_be_cancelled.Set(false);
      return;
    }
    auto* task = it->second;
    task->Cancel(will_be_cancelled);
  });
  return will_be_cancelled.Get();
}

void LibuvEventEngine::EraseTask(intptr_t taskKey) {
  auto it = task_map_.find(taskKey);
  GPR_ASSERT(it != task_map_.end());
  delete it->second;
  task_map_.erase(it);
}

////////////////////////////////////////////////////////////////////////////////
/// Unimplemented
////////////////////////////////////////////////////////////////////////////////

absl::StatusOr<std::unique_ptr<EventEngine::Listener>>
LibuvEventEngine::CreateListener(
    /*on_accept=*/Listener::AcceptCallback,
    /*on_shutdown=*/std::function<void(absl::Status)>,
    /*args=*/const EndpointConfig&,
    /*slice_allocator_factory=*/
    std::unique_ptr<SliceAllocatorFactory>) {
  GPR_ASSERT(false && "LibuvEventEngine::CreateListener is unimplemented.");
  return absl::UnimplementedError(
      "LibuvEventEngine::CreateListener is unimplemented.");
}

absl::Status LibuvEventEngine::Connect(
    /*on_connect=*/OnConnectCallback,
    /*addr=*/const ResolvedAddress&,
    /*args=*/const EndpointConfig&,
    /*slice_allocator=*/std::unique_ptr<SliceAllocator>,
    /*deadline=*/absl::Time) {
  GPR_ASSERT(false && "LibuvEventEngine::Connect is unimplemented.");
  return absl::UnimplementedError(
      "LibuvEventEngine::Connect is unimplemented.");
}

std::unique_ptr<EventEngine::DNSResolver> LibuvEventEngine::GetDNSResolver() {
  GPR_ASSERT(false && "LibuvEventEngine::GetDNSResolver not implemented");
  return nullptr;
}

void LibuvEventEngine::Run(/*closure=*/Closure*) {
  GPR_ASSERT(false && "LibuvEventEngine::Run(Closure*) not implemented");
}

EventEngine::TaskHandle LibuvEventEngine::RunAt(/*when=*/absl::Time,
                                                /*closure=*/Closure*) {
  GPR_ASSERT(false &&
             "LibuvEventEngine::RunAt(absl::Time, Closure*) not implemented");
}

////////////////////////////////////////////////////////////////////////////////
/// LibuvTask
////////////////////////////////////////////////////////////////////////////////

}  // namespace experimental
}  // namespace grpc_event_engine

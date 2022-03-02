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

#include <grpc/support/port_platform.h>

#include "src/core/lib/event_engine/uv/libuv_event_engine.h"

#include <cmath>
#include <functional>

#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_format.h"
#include "uv.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/thd_id.h>

#include "src/core/lib/gprpp/mpscq.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/thd.h"
#include "src/core/lib/iomgr/event_engine/promise.h"
#include "src/core/lib/iomgr/exec_ctx.h"

extern grpc_core::TraceFlag grpc_tcp_trace;

namespace grpc_event_engine {
namespace experimental {

struct LibuvEventEngine::SchedulingRequest
    : grpc_core::MultiProducerSingleConsumerQueue::Node {
  typedef std::function<void(LibuvEventEngine*)> functor;
  explicit SchedulingRequest(functor&& f) : f(std::move(f)) {}
  SchedulingRequest(std::unique_ptr<LibuvTask> task, uint64_t timeout)
      : task(std::move(task)), timeout(timeout) {}
  functor f;
  std::unique_ptr<LibuvEventEngine::LibuvTask> task;
  uint64_t timeout = 0;
  // TODO(hork): this is two Node types bundled together. Either add an enum
  // flag, or use different Node variants on an interface and decide which is
  // correct in the kicker.
};

LibuvEventEngine::LibuvTask::LibuvTask(LibuvEventEngine* engine,
                                       std::function<void()>&& fn)
    : fn_(std::move(fn)), handle_tag_(engine->task_key_.fetch_add(1)) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
    gpr_log(GPR_DEBUG, "LibuvTask@%p, created task: key = %s", this,
            ToString().c_str());
  }
  timer_.data = this;
}

void LibuvEventEngine::LibuvTask::Start(LibuvEventEngine* engine,
                                        uint64_t timeout) {
  uv_update_time(&engine->uv_state_->loop);
  uv_timer_init(&engine->uv_state_->loop, &timer_);
  uv_timer_start(
      &timer_,
      [](uv_timer_t* timer) {
        uv_timer_stop(timer);
        LibuvTask* task = reinterpret_cast<LibuvTask*>(timer->data);
        if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
          gpr_log(GPR_DEBUG, "LibuvTask@%p, triggered: key = %s", task,
                  task->ToString().c_str());
        }
        // TODO(hork): Timer callbacks will be delayed by one iteration of the
        // uv_loop to avoid race conditions around EventEngine destruction.
        // Before the timer callback has run, the uv state for that timer is
        // destroyed. This is delay is not ideal, we should find a way to avoid
        // it.
        uv_close(reinterpret_cast<uv_handle_t*>(timer),
                 &LibuvTask::RunAndErase);
      },
      timeout, 0);
}

void LibuvEventEngine::LibuvTask::Cancel(Promise<bool>& will_be_cancelled) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
    gpr_log(GPR_DEBUG, "LibuvTask@%p, cancelled: key = %s", this,
            ToString().c_str());
  }
  if (uv_is_closing(reinterpret_cast<uv_handle_t*>(&timer_)) != 0) {
    will_be_cancelled.Notify(false);
    return;
  }
  will_be_cancelled.Notify(true);
  uv_timer_stop(&timer_);
  uv_close(reinterpret_cast<uv_handle_t*>(&timer_), &LibuvTask::Erase);
}

void LibuvEventEngine::LibuvTask::Erase(uv_handle_t* uv_handle) {
  uv_timer_t* timer = reinterpret_cast<uv_timer_t*>(uv_handle);
  LibuvTask* task = reinterpret_cast<LibuvTask*>(timer->data);
  LibuvEventEngine* engine =
      reinterpret_cast<LibuvEventEngine*>(timer->loop->data);
  engine->task_set_.erase(task->GetHandle());
}

void LibuvEventEngine::LibuvTask::RunAndErase(uv_handle_t* handle) {
  uv_timer_t* timer = reinterpret_cast<uv_timer_t*>(handle);
  LibuvTask* task = reinterpret_cast<LibuvTask*>(timer->data);
  std::function<void()> fn = std::move(task->fn_);
  LibuvEventEngine* engine =
      reinterpret_cast<LibuvEventEngine*>(timer->loop->data);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
    gpr_log(GPR_DEBUG, "LibuvTask@%p, executing", task->ToString().c_str());
  }
  engine->task_set_.erase(task->GetHandle());
  fn();
}

LibuvEventEngine::LibuvEventEngine() {
  bool success = false;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
    gpr_log(GPR_DEBUG, "LibuvEventEngine:%p created", this);
  }
  uv_state_ = new UvState();
  thread_ = grpc_core::Thread(
      "uv loop",
      [](void* arg) {
        LibuvEventEngine* engine = reinterpret_cast<LibuvEventEngine*>(arg);
        engine->RunThread();
      },
      this, &success, grpc_core::Thread::Options().set_joinable(false));
  thread_.Start();
  GPR_ASSERT(GPR_LIKELY(success));
  // This promise will be set to true once the thread has fully started and is
  // operational, so let's wait on it.
  success = uv_state_->ready.Wait();
  GPR_ASSERT(GPR_LIKELY(success));
}

void LibuvEventEngine::DestroyInLibuvThread(
    grpc_event_engine::experimental::Promise<bool>& destruction_done) {
  GPR_ASSERT(IsWorkerThread());
  if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
    gpr_log(GPR_DEBUG,
            "LibuvEventEngine@%p shutting down, unreferencing Kicker now",
            this);
  }
  // Shutting down at this point is essentially just this unref call here.
  // After it, the libuv loop will continue working until it has no more
  // events to monitor. It means that scheduling new work becomes essentially
  // undefined behavior, which is in line with our surface API contracts,
  // which stipulate the same thing.
  uv_unref(reinterpret_cast<uv_handle_t*>(&uv_state_->kicker));
  uv_close(reinterpret_cast<uv_handle_t*>(&uv_state_->kicker), nullptr);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
    gpr_log(GPR_DEBUG, "LibuvEventEngine@%p::task_set_.size=%zu", this,
            task_set_.size());
    for (const LibuvTask::Handle& handle : task_set_) {
      gpr_log(GPR_DEBUG, " - %s", handle.Task()->ToString().c_str());
    }
    // This is an unstable API from libuv that we use for its intended
    // purpose: debugging. This can tell us if there's lingering handles
    // that are still going to hold up the loop at this point.
    uv_walk(
        &uv_state_->loop,
        [](uv_handle_t* handle, /*arg=*/void*) {
          uv_handle_type type = uv_handle_get_type(handle);
          const char* name = uv_handle_type_name(type);
          gpr_log(GPR_DEBUG,
                  "in shutdown, handle %p type %s has references: %s", handle,
                  name, uv_has_ref(handle) ? "yes" : "no");
        },
        nullptr);
  }
  destruction_done.Notify(true);
}

LibuvEventEngine::~LibuvEventEngine() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
    gpr_log(GPR_DEBUG, "LibuvEventEngine@%p::~LibuvEventEngine", this);
  }
  grpc_event_engine::experimental::Promise<bool> destruction_done;
  if (IsWorkerThread()) {
    // Run the shutdown code inline
    DestroyInLibuvThread(destruction_done);
  } else {
    // Run the shutdown code in the libuv thread
    RunInLibuvThread([&destruction_done](LibuvEventEngine* engine) {
      engine->DestroyInLibuvThread(destruction_done);
    });
  }
  destruction_done.Wait();
  GPR_ASSERT(GPR_LIKELY(task_set_.empty()));
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
    gpr_log(GPR_ERROR,
            "LibuvEventEngine@%p::RunInLibuvThread functor, created %p", this,
            request);
  }
  scheduling_request_queue_.Push(request);
  uv_async_send(&uv_state_->kicker);
}

void LibuvEventEngine::RunInLibuvThread(std::unique_ptr<LibuvTask> task,
                                        uint64_t timeout) {
  SchedulingRequest* request = new SchedulingRequest(std::move(task), timeout);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
    gpr_log(GPR_ERROR, "LibuvEventEngine@%p::RunInLibuvThread task, created %p",
            this, request);
  }
  scheduling_request_queue_.Push(request);
  uv_async_send(&uv_state_->kicker);
}

// This is the callback that libuv will call on its thread once the
// uv_async_send call above is being processed. The kick is only guaranteed
// to be called once per loop iteration, even if we sent the event multiple
// times, so we have to process as many events from the queue as possible.
void LibuvEventEngine::Kicker() {
  bool empty_schedule_queue = false;
  while (!empty_schedule_queue) {
    SchedulingRequest* node = reinterpret_cast<SchedulingRequest*>(
        scheduling_request_queue_.PopAndCheckEnd(&empty_schedule_queue));
    // TODO(hork): use the type system
    if (node != nullptr) {
      if (node->task == nullptr) {
        SchedulingRequest::functor f = std::move(node->f);
        delete node;
        f(this);
      } else {
        auto weak_task = node->task.get();
        task_set_.emplace(std::move(node->task));
        weak_task->Start(this, node->timeout);
        delete node;
      }
    }
  }
}

void LibuvEventEngine::RunThread() {
#ifndef GPR_WINDOWS
  // LibUV doesn't take upon itself to mask SIGPIPE on its own on Unix systems.
  // If a connection gets broken, we will get killed, unless we mask it out.
  // These 4 lines of code need to be enclosed in a non-Windows #ifdef check,
  // although I'm not certain what platforms will or will not have the necessary
  // function calls here.
  sigset_t set;
  sigemptyset(&set);
  sigaddset(&set, SIGPIPE);
  pthread_sigmask(SIG_BLOCK, &set, nullptr);
#endif
  // Pointer will outlive the EventEngine that created it, for deferred
  // destruction of the uv state.
  UvState* uv_state = uv_state_;
  // Setting up the loop.
  worker_thread_id_ = gpr_thd_currentid();
  int r = uv_loop_init(&uv_state->loop);
  uv_state->loop.data = this;
  r |= uv_async_init(&uv_state->loop, &uv_state->kicker, [](uv_async_t* async) {
    LibuvEventEngine* engine =
        reinterpret_cast<LibuvEventEngine*>(async->loop->data);
    if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
      gpr_log(GPR_DEBUG, "LibuvEventEngine@%p::kicker_(%p) initialized", engine,
              async);
    }
    engine->Kicker();
  });
  if (r != 0) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
      gpr_log(GPR_ERROR, "LibuvEventEngine@%p::Thread, failed to start: %i",
              this, r);
    }
    uv_state->ready.Notify(false);
    return;
  }
  uv_state->ready.Notify(true);

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
  while (uv_run(&uv_state->loop, UV_RUN_ONCE) != 0) {
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
  GPR_ASSERT(GPR_LIKELY(uv_loop_close(&uv_state->loop) != UV_EBUSY));
  delete uv_state;
}

void LibuvEventEngine::Run(std::function<void()> fn) {
  RunAt(absl::Now(), std::move(fn));
}

EventEngine::TaskHandle LibuvEventEngine::RunAt(absl::Time when,
                                                std::function<void()> fn) {
  auto task = absl::make_unique<LibuvTask>(this, std::move(fn));
  // To avoid a thread race if task erasure happens before this method returns.
  absl::Time now = absl::Now();
  uint64_t timeout;
  // Since libuv doesn't have a concept of negative timeout, we need to clamp
  // this to avoid almost-infinite timers.
  if (now >= when) {
    timeout = 0;
  } else {
    // absl tends to round down time conversions, so we add 1ms to the timeout
    // for safety. Better to err on the side of a timer firing late.
    timeout = std::ceil(absl::ToUnixMicros(when) / 1000.0) -
              absl::ToUnixMillis(now) + 1;
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
    gpr_log(GPR_DEBUG, "LibuvTask@%p::RunAt, scheduled, timeout=%" PRIu64,
            task->ToString().c_str(), timeout);
  }
  EventEngine::TaskHandle handle = task->GetHandle();
  RunInLibuvThread(std::move(task), timeout);
  uv_async_send(&uv_state_->kicker);
  return handle;
}

bool LibuvEventEngine::Cancel(EventEngine::TaskHandle handle) {
  Promise<bool> will_be_cancelled;
  RunInLibuvThread([&handle, &will_be_cancelled](LibuvEventEngine* engine) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
      gpr_log(GPR_DEBUG, "LibuvEventEnginE@%p::Cancel, attempting %s", engine,
              LibuvTask::Handle::Accessor::Task(handle)->ToString().c_str());
    }
    if (!engine->task_set_.contains(handle)) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
        gpr_log(GPR_DEBUG, "LibuvEventEnginE@%p::Cancel, %s not found", engine,
                LibuvTask::Handle::Accessor::Task(handle)->ToString().c_str());
      }
      will_be_cancelled.Notify(false);
      return;
    }
    gpr_log(GPR_DEBUG, "LibuvEventEnginE@%p::Cancel, cancelling %s", engine,
            LibuvTask::Handle::Accessor::Task(handle)->ToString().c_str());
    LibuvTask::Handle::Accessor::Task(handle)->Cancel(will_be_cancelled);
  });
  return will_be_cancelled.Wait();
}

std::unique_ptr<EventEngine> LibuvEventEngine::Create() {
  return absl::make_unique<LibuvEventEngine>();
}

////////////////////////////////////////////////////////////////////////////////
/// Unimplemented
////////////////////////////////////////////////////////////////////////////////

absl::StatusOr<std::unique_ptr<EventEngine::Listener>>
LibuvEventEngine::CreateListener(
    Listener::AcceptCallback /* on_accept */,
    std::function<void(absl::Status)> /* on_shutdown */,
    const EndpointConfig& /* args */,
    std::unique_ptr<MemoryAllocatorFactory> /* memory_allocator_factory */) {
  // TODO(hork): implement
  GPR_ASSERT(false && "LibuvEventEngine::CreateListener is unimplemented.");
  return absl::UnimplementedError(
      "LibuvEventEngine::CreateListener is unimplemented.");
}

EventEngine::ConnectionHandle LibuvEventEngine::Connect(
    OnConnectCallback /* on_connect */, const ResolvedAddress& /* addr */,
    const EndpointConfig& /* args */, MemoryAllocator /* memory_allocator */,
    absl::Time /* deadline */) {
  // TODO(hork): implement
  GPR_ASSERT(false && "LibuvEventEngine::Connect is unimplemented.");
}

bool LibuvEventEngine::CancelConnect(
    EventEngine::ConnectionHandle /* handle */) {
  // TODO(hork): implement
  GPR_ASSERT(false && "LibuvEventEngine::CancelConnect is unimplemented.");
}

std::unique_ptr<EventEngine::DNSResolver> LibuvEventEngine::GetDNSResolver() {
  // TODO(hork): implement
  GPR_ASSERT(false && "LibuvEventEngine::GetDNSResolver not implemented");
  return nullptr;
}

void LibuvEventEngine::Run(Closure* /* closure */) {
  // TODO(hork): implement
  GPR_ASSERT(false && "LibuvEventEngine::Run(Closure*) not implemented");
}

EventEngine::TaskHandle LibuvEventEngine::RunAt(absl::Time /* when */,
                                                Closure* /* closure */) {
  // TODO(hork): implement
  GPR_ASSERT(false &&
             "LibuvEventEngine::RunAt(absl::Time, Closure*) not implemented");
}

}  // namespace experimental
}  // namespace grpc_event_engine

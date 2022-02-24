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

#ifndef GRPC_CORE_LIB_EVENT_ENGINE_UV_LIBUV_EVENT_ENGINE_H
#define GRPC_CORE_LIB_EVENT_ENGINE_UV_LIBUV_EVENT_ENGINE_H

#include <grpc/support/port_platform.h>

#include <functional>
#include <cinttypes>

#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_format.h"
#include "uv.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/thd_id.h>

#include "src/core/lib/gprpp/mpscq.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/thd.h"
#include "src/core/lib/iomgr/event_engine/promise.h"

namespace grpc_event_engine {
namespace experimental {

////////////////////////////////////////////////////////////////////////////////
/// The LibUV Event Engine itself. It implements an EventEngine class.
////////////////////////////////////////////////////////////////////////////////
class LibuvEventEngine final
    : public grpc_event_engine::experimental::EventEngine {
 public:
  /// Default EventEngine factory method
  static std::unique_ptr<EventEngine> Create();

  LibuvEventEngine();
  ~LibuvEventEngine() override;

  void Run(std::function<void()> fn) override;
  TaskHandle RunAt(absl::Time when, std::function<void()> fn) override;
  bool Cancel(TaskHandle handle) override;

  // Unimplemented methods
  absl::StatusOr<std::unique_ptr<Listener>> CreateListener(
      Listener::AcceptCallback on_accept,
      std::function<void(absl::Status)> on_shutdown, const EndpointConfig& args,
      std::unique_ptr<MemoryAllocatorFactory> memory_allocator_factory)
      override;
  ConnectionHandle Connect(OnConnectCallback on_connect,
                           const ResolvedAddress& addr,
                           const EndpointConfig& args,
                           MemoryAllocator memory_allocator,
                           absl::Time deadline) override;
  bool CancelConnect(ConnectionHandle handle) override;
  std::unique_ptr<DNSResolver> GetDNSResolver() override;
  void Run(Closure* fn) override;
  TaskHandle RunAt(absl::Time when, Closure* fn) override;
  bool IsWorkerThread() override {
    return worker_thread_id_ == gpr_thd_currentid();
  }

 private:
  class LibuvTask;
  ////////////////////////////////////////////////////////////////////////////////
  /// A LibuvEventEngine-specific TaskHandle.
  ///
  /// This enables conversion to and from EventEngine::TaskHandles, and hides
  /// details of intptr_t key meanings. This type is copyable and movable.
  /// Destruction does not delete the underlying task.
  ////////////////////////////////////////////////////////////////////////////////
  class LibuvTaskHandle {
   public:
    static LibuvTaskHandle CreateFromEngineTaskHandle(
        const EventEngine::TaskHandle& handle) {
      return LibuvTaskHandle(reinterpret_cast<LibuvTask*>(handle.keys[0]),
                             handle.keys[1]);
    }
    LibuvTaskHandle() = delete;
    LibuvTaskHandle(LibuvTask* task, intptr_t tag) : task_(task), tag_(tag) {}
    // Does not delete the underlying task
    ~LibuvTaskHandle() = default;
    EventEngine::TaskHandle ToEventEngineTaskHandle() const {
      return EventEngine::TaskHandle{reinterpret_cast<intptr_t>(task_), tag_};
    }
    std::string ToString() const {
      return absl::StrFormat("{%p, %" PRIdPTR "}", task_, tag_);
    }
    LibuvTask* Task() { return task_; }
    // Compatible with absl::flat_hash_set
    template <typename H>
    friend H AbslHashValue(H h, const LibuvTaskHandle& handle) {
      return H::combine(std::move(h), handle.task_, handle.tag_);
    }
    bool operator==(const LibuvTaskHandle& handle) const {
      return &handle == this || handle.task_ == task_ && handle.tag_ == tag_;
    }

   private:
    LibuvTask* task_;
    intptr_t tag_;
  };

  // The main logic in the uv event loop
  void RunThread();
  // Schedules one lambda to be executed on the libuv thread. Our libuv loop
  // will have a special async event which is the only piece of API that's
  // marked as thread-safe.
  void RunInLibuvThread(std::function<void(LibuvEventEngine*)>&& f);
  void Kicker();
  uv_loop_t* GetLoop() { return &loop_; }
  // Erases a task from the set of known tasks.
  // Must only be called after the task is know to exist in the EventEngine task
  // set.
  void EraseTask(LibuvTaskHandle taskKey);
  // Destructor logic that must be executed in the libuv thread before the
  // engine can be destroyed (from any thread).
  void DestroyInLibuvThread(Promise<bool>& uv_shutdown_can_proceed);

  uv_loop_t loop_;
  uv_async_t kicker_;
  // This should be set only once to true by the thread when it's done setting
  // itself up.
  grpc_event_engine::experimental::Promise<bool> ready_;
  grpc_core::Thread thread_;
  grpc_core::MultiProducerSingleConsumerQueue queue_;

  // We keep a list of all of the tasks here. The atomics will serve as a
  // simple counter mechanism, with the assumption that if it ever rolls over,
  // the colliding tasks will have long been completed.
  std::atomic<intptr_t> task_key_;
  absl::flat_hash_set<LibuvTaskHandle> task_set_;

  // Hopefully temporary until we can solve shutdown from the main grpc code.
  // Used by IsWorkerThread.
  gpr_thd_id worker_thread_id_;
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_CORE_LIB_EVENT_ENGINE_UV_LIBUV_EVENT_ENGINE_H

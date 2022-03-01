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
#include "absl/hash/hash.h"
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
  ////////////////////////////////////////////////////////////////////////////////
  /// The LibuvTask class is used for Run and RunAt from LibuvEventEngine, and
  /// is allocated internally for the returned TaskHandle.
  ///
  /// Its API is used solely by the Run and RunAt functions, while in the libuv
  /// loop thread.
  ////////////////////////////////////////////////////////////////////////////////
  class LibuvTaskHandle;
  struct SchedulingRequest;
  class LibuvTask {
   public:
    LibuvTask(LibuvEventEngine* engine, std::function<void()>&& fn);
    /// Executes the held \a fn_ and removes itself from EventEngine's
    /// accounting. Must be called from within the libuv thread.
    void Start(LibuvEventEngine* engine, uint64_t timeout);
    /// Cancel this task.
    /// The promise meanings are the same as in \a EventEngine::Cancel.
    /// Must be called from within the libuv thread.
    /// Precondition: the EventEngine must be tracking this task.
    void Cancel(Promise<bool>& will_be_cancelled);
    EventEngine::TaskHandle Handle() {
      return {reinterpret_cast<intptr_t>(this), handle_tag_};
    }
    std::string ToString() const {
      return absl::StrFormat("{%p, %" PRIdPTR "}", this, handle_tag_);
    }

   private:
    /// A callback passed to uv_close to erase the timer from the EventEngine
    static void Erase(uv_handle_t* handle);
    /// A callback passed to uv_close to coordinate running the task then
    /// erasing the timer from the EventEngine. This helps avoid race conditions
    /// where the timer handle is open after the function is run and the
    /// EventEngine is being destroyed.
    static void RunAndErase(uv_handle_t* handle);

    std::function<void()> fn_;
    uv_timer_t timer_;
    intptr_t handle_tag_;

    friend class LibuvEventEngine::LibuvTaskHandle;
  };

  ////////////////////////////////////////////////////////////////////////////////
  /// A LibuvEventEngine-specific TaskHandle.
  ///
  /// This enables conversion to and from EventEngine::TaskHandles, and hides
  /// details of intptr_t key meanings. This type is copyable and movable.
  /// Destruction does not delete the underlying task.
  ////////////////////////////////////////////////////////////////////////////////
  class LibuvTaskHandle {
   public:
    // Accessor methods for EventEngine::TaskHandles
    class Accessor {
     public:
      static LibuvTask* Task(const EventEngine::TaskHandle& handle) {
        return reinterpret_cast<LibuvTask*>(handle.keys[0]);
      }
      static intptr_t Tag(const EventEngine::TaskHandle& handle) {
        return handle.keys[1];
      }
    };
    // Custom hashing for EventEngine::TaskHandle comparison in absl containers.
    using HashType = std::pair<const LibuvTask*, const intptr_t>;
    struct Comparator {
      struct Hash {
        using is_transparent = void;
        size_t operator()(const LibuvTaskHandle& handle) const {
          return absl::Hash<HashType>()({handle.task_.get(), handle.tag_});
        }
        size_t operator()(const EventEngine::TaskHandle& handle) const {
          return absl::Hash<HashType>()(
              {Accessor::Task(handle), Accessor::Tag(handle)});
        }
      };
      struct Eq {
        using is_transparent = void;
        bool operator()(const LibuvTaskHandle& lhs,
                        const LibuvTaskHandle& rhs) const {
          return lhs == rhs;
        }
        bool operator()(const LibuvTaskHandle& lhs,
                        const EventEngine::TaskHandle& rhs) const {
          return lhs == rhs;
        }
        bool operator()(const EventEngine::TaskHandle& lhs,
                        const LibuvTaskHandle& rhs) const {
          return rhs == lhs;
        }
      };
    };
    // (Con|De)struction
    LibuvTaskHandle() = delete;
    LibuvTaskHandle(std::unique_ptr<LibuvTask> task)
        : task_(std::move(task)), tag_(task_->handle_tag_) {}
    LibuvTaskHandle(const LibuvTaskHandle&) = delete;
    LibuvTaskHandle& operator=(const LibuvTaskHandle&) = delete;
    LibuvTaskHandle(LibuvTaskHandle&& other) noexcept
        : task_(std::move(other.task_)), tag_(other.tag_) {}
    LibuvTaskHandle& operator=(LibuvTaskHandle&& other) noexcept {
      std::swap(task_, other.task_);
      std::swap(tag_, other.tag_);
      return *this;
    }
    ~LibuvTaskHandle() = default;
    // Members
    LibuvTask* Task() { return task_.get(); }
    const LibuvTask* Task() const { return task_.get(); }
    // Equality
    constexpr bool operator==(const LibuvTaskHandle& handle) const {
      return &handle == this ||
             (handle.task_.get() == task_.get() && handle.tag_ == tag_);
    }
    constexpr bool operator==(const EventEngine::TaskHandle& handle) const {
      return Accessor::Task(handle) == task_.get() &&
             Accessor::Tag(handle) == tag_;
    }

   private:
    std::unique_ptr<LibuvTask> task_;
    intptr_t tag_;
  };

  // The main logic in the uv event loop
  void RunThread();
  // Schedules one lambda to be executed on the libuv thread. Our libuv loop
  // will have a special async event which is the only piece of API that's
  // marked as thread-safe.
  void RunInLibuvThread(std::function<void(LibuvEventEngine*)>&& f);
  // Overload to start a uv_task in the uv thread. This is used to usher
  // ownership of the LibuvTask to the engine.
  void RunInLibuvThread(std::unique_ptr<LibuvTask> task, uint64_t timeout);
  void Kicker();
  uv_loop_t* GetLoop() { return &loop_; }
  // Destructor logic that must be executed in the libuv thread before the
  // engine can be destroyed (from any thread).
  void DestroyInLibuvThread(Promise<bool>& uv_shutdown_can_proceed);

  uv_loop_t loop_;
  uv_async_t kicker_;
  // This should be set only once to true by the thread when it's done setting
  // itself up.
  grpc_event_engine::experimental::Promise<bool> ready_;
  grpc_core::Thread thread_;
  grpc_core::MultiProducerSingleConsumerQueue scheduling_request_queue_;

  // We keep a list of all of the tasks here. The atomics will serve as a
  // simple counter mechanism, with the assumption that if it ever rolls over,
  // the colliding tasks will have long been completed.
  std::atomic<intptr_t> task_key_;
  absl::flat_hash_set<LibuvTaskHandle, LibuvTaskHandle::Comparator::Hash,
                      LibuvTaskHandle::Comparator::Eq>
      task_set_;

  // Hopefully temporary until we can solve shutdown from the main grpc code.
  // Used by IsWorkerThread.
  gpr_thd_id worker_thread_id_;
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_CORE_LIB_EVENT_ENGINE_UV_LIBUV_EVENT_ENGINE_H

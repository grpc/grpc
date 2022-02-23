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
#include <unordered_map>

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
  // An internal task representation
  class LibuvTask;

  // The main logic in the uv event loop
  void RunThread();
  // Schedules one lambda to be executed on the libuv thread. Our libuv loop
  // will have a special async event which is the only piece of API that's
  // marked as thread-safe.
  void RunInLibuvThread(std::function<void(LibuvEventEngine*)>&& f);
  void Kicker();
  uv_loop_t* GetLoop() { return &loop_; }
  void EraseTask(intptr_t taskKey);

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
  //
  // NOTE: now that we're returning two intptr_t instead of just one for the
  // keys, this can be improved, as we can hold the pointer in one
  // key, and a tag in the other, to avoid the ABA problem. We'll keep the
  // atomics as tags in the second key slot, but we can get rid of the maps.
  //
  // TODO(nnoble): remove the maps, and fold the pointers into the keys,
  // alongside the ABA tag.
  std::atomic<intptr_t> task_key_;
  std::unordered_map<intptr_t, LibuvTask*> task_map_;

  // Hopefully temporary until we can solve shutdown from the main grpc code.
  // Used by IsWorkerThread.
  gpr_thd_id worker_thread_id_;
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_CORE_LIB_EVENT_ENGINE_UV_LIBUV_EVENT_ENGINE_H

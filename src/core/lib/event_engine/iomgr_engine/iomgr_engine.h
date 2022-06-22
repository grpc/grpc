// Copyright 2022 The gRPC Authors
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
#ifndef GRPC_CORE_LIB_EVENT_ENGINE_IOMGR_ENGINE_IOMGR_ENGINE_H
#define GRPC_CORE_LIB_EVENT_ENGINE_IOMGR_ENGINE_IOMGR_ENGINE_H
#include <grpc/support/port_platform.h>

#include <stdint.h>

#include <atomic>
#include <functional>
#include <memory>

#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

#include <grpc/event_engine/endpoint_config.h>
#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/memory_allocator.h>
#include <grpc/event_engine/slice_buffer.h>

#include "src/core/lib/event_engine/handle_containers.h"
#include "src/core/lib/event_engine/iomgr_engine/thread_pool.h"
#include "src/core/lib/event_engine/iomgr_engine/timer_manager.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/time.h"

namespace grpc_event_engine {
namespace experimental {

class GrpcFd;

class EventPoller {
 public:
  virtual GrpcFd* FdCreate(int fd, absl::string_view name, bool track_err) = 0;
  virtual int FdWrappedFd(GrpcFd* fd) = 0;
  virtual void FdOrphan(GrpcFd* fd, EventEngine::Closure* on_done,
                        int* release_fd, absl::string_view reason) = 0;
  virtual void FdShutdown(GrpcFd* fd, grpc_error_handle why);
  virtual void FdNotifyOnRead(GrpcFd* fd,
                              std::function<void(absl::Status)> on_read) = 0;
  virtual void FdNotifyOnWrite(GrpcFd* fd,
                               std::function<void(absl::Status)> on_write) = 0;
  virtual void FdNotifyOnError(GrpcFd* fd,
                               std::function<void(absl::Status)> on_write) = 0;
  virtual void FdSetReadable(GrpcFd* fd) = 0;
  virtual void FdSetWritable(GrpcFd* fd) = 0;
  virtual void FdSetError(GrpcFd* fd) = 0;
  virtual bool FdIsShutdown(GrpcFd* fd) = 0;

  void (*pollset_init)(grpc_pollset* pollset, gpr_mu** mu);
  void (*pollset_shutdown)(grpc_pollset* pollset, grpc_closure* closure);
  void (*pollset_destroy)(grpc_pollset* pollset);
  grpc_error_handle (*pollset_work)(grpc_pollset* pollset,
                                    grpc_pollset_worker** worker,
                                    grpc_core::Timestamp deadline);
  grpc_error_handle (*pollset_kick)(grpc_pollset* pollset,
                                    grpc_pollset_worker* specific_worker);
  void (*pollset_add_fd)(grpc_pollset* pollset, struct grpc_fd* fd);

  grpc_pollset_set* (*pollset_set_create)(void);
  void (*pollset_set_destroy)(grpc_pollset_set* pollset_set);
  void (*pollset_set_add_pollset)(grpc_pollset_set* pollset_set,
                                  grpc_pollset* pollset);
  void (*pollset_set_del_pollset)(grpc_pollset_set* pollset_set,
                                  grpc_pollset* pollset);
  void (*pollset_set_add_pollset_set)(grpc_pollset_set* bag,
                                      grpc_pollset_set* item);
  void (*pollset_set_del_pollset_set)(grpc_pollset_set* bag,
                                      grpc_pollset_set* item);
  void (*pollset_set_add_fd)(grpc_pollset_set* pollset_set, grpc_fd* fd);
  void (*pollset_set_del_fd)(grpc_pollset_set* pollset_set, grpc_fd* fd);

  bool (*is_any_background_poller_thread)(void);
  void (*shutdown_background_closure)(void);
  void (*shutdown_engine)(void);
  bool (*add_closure_to_background_poller)(grpc_closure* closure,
                                           grpc_error_handle error);
};

// An iomgr-based EventEngine implementation.
// All methods require an ExecCtx to already exist on the thread's stack.
class IomgrEventEngine final : public EventEngine {
 public:
  class IomgrEndpoint : public EventEngine::Endpoint {
   public:
    ~IomgrEndpoint() override;
    void Read(std::function<void(absl::Status)> on_read, SliceBuffer* buffer,
              const ReadArgs* args) override;
    void Write(std::function<void(absl::Status)> on_writable, SliceBuffer* data,
               const WriteArgs* args) override;
    const ResolvedAddress& GetPeerAddress() const override;
    const ResolvedAddress& GetLocalAddress() const override;
  };
  class IomgrListener : public EventEngine::Listener {
   public:
    ~IomgrListener() override;
    absl::StatusOr<int> Bind(const ResolvedAddress& addr) override;
    absl::Status Start() override;
  };
  class IomgrDNSResolver : public EventEngine::DNSResolver {
   public:
    ~IomgrDNSResolver() override;
    LookupTaskHandle LookupHostname(LookupHostnameCallback on_resolve,
                                    absl::string_view name,
                                    absl::string_view default_port,
                                    Duration timeout) override;
    LookupTaskHandle LookupSRV(LookupSRVCallback on_resolve,
                               absl::string_view name,
                               Duration timeout) override;
    LookupTaskHandle LookupTXT(LookupTXTCallback on_resolve,
                               absl::string_view name,
                               Duration timeout) override;
    bool CancelLookup(LookupTaskHandle handle) override;
  };

  IomgrEventEngine();
  ~IomgrEventEngine() override;

  absl::StatusOr<std::unique_ptr<Listener>> CreateListener(
      Listener::AcceptCallback on_accept,
      std::function<void(absl::Status)> on_shutdown,
      const EndpointConfig& config,
      std::unique_ptr<MemoryAllocatorFactory> memory_allocator_factory)
      override;

  ConnectionHandle Connect(OnConnectCallback on_connect,
                           const ResolvedAddress& addr,
                           const EndpointConfig& args,
                           MemoryAllocator memory_allocator,
                           Duration timeout) override;

  bool CancelConnect(ConnectionHandle handle) override;
  bool IsWorkerThread() override;
  std::unique_ptr<DNSResolver> GetDNSResolver(
      const DNSResolver::ResolverOptions& options) override;
  void Run(Closure* closure) override;
  void Run(std::function<void()> closure) override;
  TaskHandle RunAfter(Duration when, Closure* closure) override;
  TaskHandle RunAfter(Duration when, std::function<void()> closure) override;
  bool Cancel(TaskHandle handle) override;

 private:
  struct ClosureData;
  EventEngine::TaskHandle RunAfterInternal(Duration when,
                                           std::function<void()> cb);
  grpc_core::Timestamp ToTimestamp(EventEngine::Duration when);

  iomgr_engine::TimerManager timer_manager_;
  iomgr_engine::ThreadPool thread_pool_{2};

  grpc_core::Mutex mu_;
  TaskHandleSet known_handles_ ABSL_GUARDED_BY(mu_);
  std::atomic<intptr_t> aba_token_{0};
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_CORE_LIB_EVENT_ENGINE_IOMGR_ENGINE_IOMGR_ENGINE_H

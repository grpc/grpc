// Copyright 2023 The gRPC Authors
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
#ifndef GRPC_SRC_CORE_LIB_EVENT_ENGINE_ARES_RESOLVER_H
#define GRPC_SRC_CORE_LIB_EVENT_ENGINE_ARES_RESOLVER_H

#include <grpc/support/port_platform.h>

#include <utility>

#include "absl/status/status.h"

#if GRPC_ARES == 1

#include <ares.h>
#include <grpc/event_engine/event_engine.h>

#include <list>
#include <memory>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "src/core/lib/event_engine/grpc_polled_fd.h"
#include "src/core/lib/event_engine/ref_counted_dns_resolver_interface.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/sync.h"

namespace grpc_event_engine::experimental {

class AresResolver : public RefCountedDNSResolverInterface {
 public:
  // A handle allowing  to trigger the reinitialization of a specific
  // AresResolver instance after a fork. This approach avoids maintaining a
  // global collection of resolvers and allows them to come and go. It also
  // prevents the AresResolver class itself from depending on POSIX-specific
  // fork APIs.
  class ReinitHandle {
   public:
    explicit ReinitHandle(AresResolver* resolver);
    // Only pass around as a pointer
    ReinitHandle(ReinitHandle&& other) = delete;
    ReinitHandle(const ReinitHandle& other) = delete;
    void OnResolverGone();
    void Reinit();

   private:
    grpc_core::Mutex mutex_;
    AresResolver* resolver_ ABSL_GUARDED_BY(&mutex_);
  };

  static absl::StatusOr<grpc_core::OrphanablePtr<AresResolver>>
  CreateAresResolver(absl::string_view dns_server,
                     std::unique_ptr<GrpcPolledFdFactory> polled_fd_factory,
                     std::shared_ptr<EventEngine> event_engine);

  // Do not instantiate directly -- use CreateAresResolver() instead.
  AresResolver(std::unique_ptr<GrpcPolledFdFactory> polled_fd_factory,
               std::shared_ptr<EventEngine> event_engine, ares_channel channel,
               absl::string_view dns_server);
  ~AresResolver() override;
  void Orphan() override ABSL_LOCKS_EXCLUDED(mutex_);

  void LookupHostname(EventEngine::DNSResolver::LookupHostnameCallback callback,
                      absl::string_view name, absl::string_view default_port)
      ABSL_LOCKS_EXCLUDED(mutex_) override;
  void LookupSRV(EventEngine::DNSResolver::LookupSRVCallback callback,
                 absl::string_view name) ABSL_LOCKS_EXCLUDED(mutex_) override;
  void LookupTXT(EventEngine::DNSResolver::LookupTXTCallback callback,
                 absl::string_view name) ABSL_LOCKS_EXCLUDED(mutex_) override;

#ifdef GRPC_ENABLE_FORK_SUPPORT
  std::weak_ptr<ReinitHandle> GetReinitHandle();
  void Reinitialize();
#endif  // GRPC_ENABLE_FORK_SUPPORT
 private:
  // A FdNode saves (not owns) a live socket/fd which c-ares creates, and owns a
  // GrpcPolledFd object which has a platform-agnostic interface to interact
  // with the poller. The liveness of the socket means that c-ares needs us to
  // monitor r/w events on this socket and notifies c-ares when such events have
  // happened which we achieve through the GrpcPolledFd object. FdNode also
  // handles the shutdown (maybe due to socket no longer used, finished request,
  // cancel or timeout) and the destruction of the poller handle. Note that
  // FdNode does not own the socket and it's the c-ares' responsibility to
  // close the socket (possibly through ares_destroy).
  struct FdNode {
    FdNode() = default;
    FdNode(ares_socket_t as, std::unique_ptr<GrpcPolledFd> pf)
        : as(as), polled_fd(std::move(pf)) {}
    ares_socket_t as;
    std::unique_ptr<GrpcPolledFd> polled_fd;
    // true if the readable closure has been registered
    bool readable_registered = false;
    // true if the writable closure has been registered
    bool writable_registered = false;
    bool already_shutdown = false;
  };
  using FdNodeList = std::list<std::unique_ptr<FdNode>>;

  using CallbackType =
      std::variant<EventEngine::DNSResolver::LookupHostnameCallback,
                   EventEngine::DNSResolver::LookupSRVCallback,
                   EventEngine::DNSResolver::LookupTXTCallback>;

  void CheckSocketsLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  void MaybeStartTimerLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  void OnReadable(FdNode* fd_node, absl::Status status)
      ABSL_LOCKS_EXCLUDED(mutex_);
  void OnWritable(FdNode* fd_node, absl::Status status)
      ABSL_LOCKS_EXCLUDED(mutex_);
  void OnAresBackupPollAlarm() ABSL_LOCKS_EXCLUDED(mutex_);

  // Handles the critical failure when c-ares requires sockets but none
  // can be monitored by the event engine.
  void HandleNoMonitorableSocketsLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // These callbacks are invoked from the c-ares library, so disable thread
  // safety analysis. We are guaranteed to be holding mutex_.
  static void OnHostbynameDoneLocked(void* arg, int status, int /*timeouts*/,
                                     struct hostent* hostent)
      ABSL_NO_THREAD_SAFETY_ANALYSIS;
  static void OnSRVQueryDoneLocked(void* arg, int status, int /*timeouts*/,
                                   unsigned char* abuf,
                                   int alen) ABSL_NO_THREAD_SAFETY_ANALYSIS;
  static void OnTXTDoneLocked(void* arg, int status, int /*timeouts*/,
                              unsigned char* buf,
                              int len) ABSL_NO_THREAD_SAFETY_ANALYSIS;

  grpc_core::Mutex mutex_;
  bool shutting_down_ ABSL_GUARDED_BY(mutex_) = false;
  ares_channel channel_ ABSL_GUARDED_BY(mutex_);
  FdNodeList fd_node_list_ ABSL_GUARDED_BY(mutex_);
  int id_ ABSL_GUARDED_BY(mutex_) = 0;
  absl::flat_hash_map<int, CallbackType> callback_map_ ABSL_GUARDED_BY(mutex_);
  std::optional<EventEngine::TaskHandle> ares_backup_poll_alarm_handle_
      ABSL_GUARDED_BY(mutex_);
  std::unique_ptr<GrpcPolledFdFactory> polled_fd_factory_;
  std::shared_ptr<EventEngine> event_engine_;
  std::string dns_server_;
#ifdef GRPC_ENABLE_FORK_SUPPORT
  grpc_core::Mutex reinit_handle_mu_;
  std::shared_ptr<ReinitHandle> reinit_handle_
      ABSL_GUARDED_BY(reinit_handle_mu_);
#endif  // GRPC_ENABLE_FORK_SUPPORT
};

}  // namespace grpc_event_engine::experimental

// Exposed in this header for C-core tests only
extern void (*event_engine_grpc_ares_test_only_inject_config)(
    ares_channel* channel);

// Exposed in this header for C-core tests only
extern bool g_event_engine_grpc_ares_test_only_force_tcp;

#endif  // GRPC_ARES == 1

bool ShouldUseAresDnsResolver();
absl::Status AresInit();
void AresShutdown();

#endif  // GRPC_SRC_CORE_LIB_EVENT_ENGINE_ARES_RESOLVER_H

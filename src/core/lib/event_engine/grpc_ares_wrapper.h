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
#ifndef GRPC_SRC_CORE_LIB_EVENT_ENGINE_GRPC_ARES_WRAPPER_H
#define GRPC_SRC_CORE_LIB_EVENT_ENGINE_GRPC_ARES_WRAPPER_H

#include <grpc/support/port_platform.h>

#include "src/core/lib/gprpp/debug_location.h"

#if GRPC_ARES == 1

#include <stddef.h>

#include <algorithm>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include <ares.h>

#include "absl/base/thread_annotations.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/log.h>

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/event_engine/grpc_polled_fd.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/sync.h"

namespace grpc_event_engine {
namespace experimental {

extern grpc_core::TraceFlag grpc_trace_ares_wrapper;

#define GRPC_ARES_WRAPPER_TRACE_LOG(format, ...)                              \
  do {                                                                        \
    if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_ares_wrapper)) {                   \
      gpr_log(GPR_INFO, "(EventEngine c-ares wrapper) " format, __VA_ARGS__); \
    }                                                                         \
  } while (0)

// Base class for the c-ares based DNS lookup functionality. This class is
// inherited by the concrete leaf classes below which each represents a
// different type of lookup (i.e. A/AAAA, SRV, TXT). One request object
// represents one lookup request for a specific name. GrpcAresRequest
// encapsulates the common logics to initialize and destroy the c-ares channel
// (one channel for one request). Sets the name servers configuration for the
// channel. And interacts with c-ares sockets/fds and the EventEngine poller. It
// also encapsulates logics to start and cancel timers.
class GrpcAresRequest
    : public grpc_core::InternallyRefCounted<GrpcAresRequest> {
 public:
  ~GrpcAresRequest() override;

  // Cancels an inflight request. Returns true if cancel succeeds and will start
  // the shutdown process. Returns false if the request is already finishing,
  // cancelled or shutdown.
  bool Cancel() ABSL_LOCKS_EXCLUDED(mu());
  void Orphan() override { Unref(DEBUG_LOCATION, "Orphan"); }

 protected:
  GrpcAresRequest(absl::string_view name, EventEngine::Duration timeout,
                  std::unique_ptr<GrpcPolledFdFactory> polled_fd_factory,
                  EventEngine* event_engine);

  // getter/setter
  grpc_core::Mutex& mu() const { return mu_; }
  bool initialized() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu()) {
    return initialized_;
  }
  ares_channel channel() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu()) {
    return channel_;
  }
  const std::string& name() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu()) {
    return name_;
  }
  const std::string& host() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu()) {
    return host_;
  }
  bool cancelled() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu()) {
    return cancelled_;
  }
  bool shutting_down() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu()) {
    return shutting_down_;
  }
  void set_shutting_down() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu()) {
    shutting_down_ = true;
  }

  // Implemented by each child classes so that the base class can add error to
  // child's result.
  virtual void AddError(absl::Status status)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu()) = 0;

  // The workhorse function. Gets the live sockets/fds used by c-ares, creates
  // FdNode if it is not currently tracked in fd_node_list_. Registers the
  // socket with the poller for read and/or write based on c-ares's demand. And
  // shutdown and destroys the poller handles whose sockets are no longer in use
  // by c-ares.
  // This function is called in every opportunities when there might be a change
  // to c-ares' sockets for the channel.
  void Work() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu());
  // Common logic to start the query timeout timer and the ares backup poll
  // timer. This is only called in each leaf class' Start method.
  void StartTimers() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu());
  // Common logic to cancel the query timeout timer and the ares backup poll
  // timer. This is called when the request is cancelled or shutting down.
  void CancelTimers() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu());
  // These 2 methods are deliberately thread-unsafe and should only be called in
  // the factory methods of the leaf classes as part of initialization.
  absl::StatusOr<std::string> ParseNameToResolve();
  absl::Status InitializeAresOptions(absl::string_view dns_server);

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
    FdNode(ares_socket_t as, GrpcPolledFd* polled_fd)
        : as(as), polled_fd(polled_fd) {}
    ares_socket_t as;
    std::unique_ptr<GrpcPolledFd> polled_fd;
    // true if the readable closure has been registered
    bool readable_registered = false;
    // true if the writable closure has been registered
    bool writable_registered = false;
    bool already_shutdown = false;
  };
  using FdNodeList = std::list<std::unique_ptr<FdNode>>;

  friend class GrpcPolledFdFactory;

  // Requires mu_ to be locked before calling.
  // Unlock mu_ before returns.
  //
  // Ideally this could take a lock object by reference or something like a
  // std::unique_ptr<grpc_core::ReleasableMutexLock>. But Thread Safety Analysis
  // does not work in these scenarios, see for example:
  // https://github.com/llvm/llvm-project/issues/59662.
  virtual void OnResolveLockedAndUnlock() ABSL_UNLOCK_FUNCTION(mu()) = 0;

  absl::Status SetRequestDNSServer(absl::string_view dns_server)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu());
  void OnReadable(FdNode* fd_node, absl::Status status)
      ABSL_LOCKS_EXCLUDED(mu());
  void OnWritable(FdNode* fd_node, absl::Status status)
      ABSL_LOCKS_EXCLUDED(mu());
  void OnQueryTimeout() ABSL_LOCKS_EXCLUDED(mu());
  void OnAresBackupPollAlarm() ABSL_LOCKS_EXCLUDED(mu());
  void ShutdownPolledFdsLocked(absl::Status status)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu());

  mutable grpc_core::Mutex mu_;
  bool initialized_ ABSL_GUARDED_BY(mu()) = false;
  // ares channel
  ares_channel channel_ ABSL_GUARDED_BY(mu()) = nullptr;
  // name to resolve
  const std::string name_ ABSL_GUARDED_BY(mu());
  // host to resolve, parsed from name_
  std::string host_ ABSL_GUARDED_BY(mu());
  // State machine:
  // timeout -> shutting_down_
  // cancel  -> cancelled_|shutting_down_
  // ares callback fired -> shutting_down_
  bool cancelled_ ABSL_GUARDED_BY(mu()) = false;
  bool shutting_down_ ABSL_GUARDED_BY(mu()) = false;

  const EventEngine::Duration timeout_ ABSL_GUARDED_BY(mu());
  FdNodeList fd_node_list_ ABSL_GUARDED_BY(mu());
  absl::optional<EventEngine::TaskHandle> query_timeout_handle_
      ABSL_GUARDED_BY(mu());
  absl::optional<EventEngine::TaskHandle> ares_backup_poll_alarm_handle_
      ABSL_GUARDED_BY(mu());
  std::unique_ptr<GrpcPolledFdFactory> polled_fd_factory_ ABSL_GUARDED_BY(mu());

  EventEngine* event_engine_;
};

// Lookup the "A" and/or "AAAA" records for a name.
class GrpcAresHostnameRequest final : public GrpcAresRequest {
 public:
  using Result = std::vector<EventEngine::ResolvedAddress>;

  static absl::StatusOr<GrpcAresHostnameRequest*> Create(
      absl::string_view name, absl::string_view default_port,
      absl::string_view dns_server, EventEngine::Duration timeout,
      std::unique_ptr<GrpcPolledFdFactory> polled_fd_factory,
      EventEngine* event_engine);

  // Starting a request, on_resolve will be called with Result or failure status
  // unless the request was successfully cancelled.
  void Start(absl::AnyInvocable<void(absl::StatusOr<Result>)> on_resolve)
      ABSL_LOCKS_EXCLUDED(mu());

 private:
  // This callback is invoked from the c-ares library, so disable thread
  // safety analysis. Note that we are guaranteed to be holding mu_ though.
  static void OnHostbynameDoneLocked(void* arg, int status, int /*timeouts*/,
                                     struct hostent* hostent)
      ABSL_NO_THREAD_SAFETY_ANALYSIS;

  GrpcAresHostnameRequest(
      absl::string_view name, absl::string_view default_port,
      EventEngine::Duration timeout,
      std::unique_ptr<GrpcPolledFdFactory> polled_fd_factory,
      EventEngine* event_engine);
  ~GrpcAresHostnameRequest() override;

  void AddError(absl::Status error) override
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu());
  void OnResolveLockedAndUnlock() override ABSL_UNLOCK_FUNCTION(mu());

  bool ResolveAsIPLiteralLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu());
  void LogResolvedAddressesListLocked(absl::string_view input_output_str)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu());
  void SortResolvedAddressesLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu());
  // This method is deliberately thread-unsafe and should only be called in
  // Create as part of initialization.
  absl::Status ParsePort(absl::string_view port);

  // port, parsed from name_ or default_port_
  int port_ ABSL_GUARDED_BY(mu()) = 0;
  const std::string default_port_ ABSL_GUARDED_BY(mu());
  size_t pending_queries_ ABSL_GUARDED_BY(mu()) = 0;
  absl::StatusOr<Result> result_ ABSL_GUARDED_BY(mu());
  absl::AnyInvocable<void(absl::StatusOr<Result>)> on_resolve_
      ABSL_GUARDED_BY(mu());
  bool resolved_ ABSL_GUARDED_BY(mu()) = false;

  EventEngine* event_engine_;
};

// Lookup the "SRV" records for a name.
class GrpcAresSRVRequest final : public GrpcAresRequest {
 public:
  using Result = std::vector<EventEngine::DNSResolver::SRVRecord>;

  static absl::StatusOr<GrpcAresSRVRequest*> Create(
      absl::string_view name, EventEngine::Duration timeout,
      absl::string_view dns_server,
      std::unique_ptr<GrpcPolledFdFactory> polled_fd_factory,
      EventEngine* event_engine);

  // Starting a request, on_resolve will be called with Result or failure status
  // unless the request was successfully cancelled.
  void Start(absl::AnyInvocable<void(absl::StatusOr<Result>)> on_resolve)
      ABSL_LOCKS_EXCLUDED(mu());

 private:
  // This callback is invoked from the c-ares library, so disable thread
  // safety analysis. Note that we are guaranteed to be holding mu_ though.
  static void OnSRVQueryDoneLocked(void* arg, int status, int /*timeouts*/,
                                   unsigned char* abuf,
                                   int alen) ABSL_NO_THREAD_SAFETY_ANALYSIS;

  GrpcAresSRVRequest(absl::string_view name, EventEngine::Duration timeout,
                     std::unique_ptr<GrpcPolledFdFactory> polled_fd_factory,
                     EventEngine* event_engine);
  ~GrpcAresSRVRequest() override;

  void AddError(absl::Status error) override
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu());
  void OnResolveLockedAndUnlock() override ABSL_UNLOCK_FUNCTION(mu());

  absl::StatusOr<Result> result_ ABSL_GUARDED_BY(mu());
  absl::AnyInvocable<void(absl::StatusOr<Result>)> on_resolve_
      ABSL_GUARDED_BY(mu());
  bool resolved_ ABSL_GUARDED_BY(mu()) = false;

  EventEngine* event_engine_;
};

// Lookup the "TXT" records for a name.
class GrpcAresTXTRequest final : public GrpcAresRequest {
 public:
  using Result = std::vector<std::string>;

  static absl::StatusOr<GrpcAresTXTRequest*> Create(
      absl::string_view name, EventEngine::Duration timeout,
      absl::string_view dns_server,
      std::unique_ptr<GrpcPolledFdFactory> polled_fd_factory,
      EventEngine* event_engine);

  // Starting a request, on_resolve will be called with Result or failure status
  // unless the request was successfully cancelled.
  void Start(absl::AnyInvocable<void(absl::StatusOr<Result>)> on_resolve)
      ABSL_LOCKS_EXCLUDED(mu());

 private:
  // This callback is invoked from the c-ares library, so disable thread
  // safety analysis. Note that we are guaranteed to be holding mu_ though.
  static void OnTXTDoneLocked(void* arg, int status, int /*timeouts*/,
                              unsigned char* buf,
                              int len) ABSL_NO_THREAD_SAFETY_ANALYSIS;

  GrpcAresTXTRequest(absl::string_view name, EventEngine::Duration timeout,
                     std::unique_ptr<GrpcPolledFdFactory> polled_fd_factory,
                     EventEngine* event_engine);
  ~GrpcAresTXTRequest() override;

  void AddError(absl::Status error) override
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu());
  void OnResolveLockedAndUnlock() override ABSL_UNLOCK_FUNCTION(mu());

  absl::StatusOr<Result> result_ ABSL_GUARDED_BY(mu());
  absl::AnyInvocable<void(absl::StatusOr<Result>)> on_resolve_
      ABSL_GUARDED_BY(mu());
  bool resolved_ ABSL_GUARDED_BY(mu()) = false;

  EventEngine* event_engine_;
};

}  // namespace experimental
}  // namespace grpc_event_engine

// Exposed in this header for C-core tests only
extern void (*event_engine_grpc_ares_test_only_inject_config)(
    ares_channel channel);

#endif  // GRPC_ARES == 1
#endif  // GRPC_SRC_CORE_LIB_EVENT_ENGINE_GRPC_ARES_WRAPPER_H

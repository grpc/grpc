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
#ifndef GRPC_SRC_CORE_LIB_EVENT_ENGINE_ARES_DRIVER_H
#define GRPC_SRC_CORE_LIB_EVENT_ENGINE_ARES_DRIVER_H

#include <grpc/support/port_platform.h>

#include <stddef.h>  // for size_t

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
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/port.h"

namespace grpc_event_engine {
namespace experimental {

#ifdef GRPC_POSIX_SOCKET_ARES_EV_DRIVER
class EventHandle;
using PollerHandle = EventHandle*;
#elif defined(GRPC_WINDOWS_SOCKET_ARES_EV_DRIVER)
class WinSocket;
using PollerHandle = std::unique_ptr<WinSocket>;
#else
#error "Unsupported platform"
#endif
using RegisterAresSocketWithPollerCallback =
    absl::AnyInvocable<PollerHandle(ares_socket_t)>;

extern grpc_core::TraceFlag grpc_trace_ares_driver;

#define GRPC_ARES_DRIVER_TRACE_LOG(format, ...)                \
  do {                                                         \
    if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_ares_driver)) {     \
      gpr_log(GPR_INFO, "(ares driver) " format, __VA_ARGS__); \
    }                                                          \
  } while (0)

class GrpcPolledFd;
class GrpcPolledFdFactory;

class GrpcAresRequest
    : public grpc_core::InternallyRefCounted<GrpcAresRequest> {
 public:
  ~GrpcAresRequest() override;

  bool Cancel() ABSL_LOCKS_EXCLUDED(mu_);
  void Orphan() override {}

 protected:
  absl::Status Initialize(absl::string_view dns_server, bool check_port)
      ABSL_LOCKS_EXCLUDED(mu_);

  GrpcAresRequest(absl::string_view name,
                  absl::optional<absl::string_view> default_port,
                  EventEngine::Duration timeout,
                  RegisterAresSocketWithPollerCallback register_cb,
                  EventEngine* event_engine);

  void Work() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  void StartTimers() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  void CancelTimers() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

 private:
  struct FdNode {
    FdNode() = default;
    FdNode(ares_socket_t as, GrpcPolledFd* polled_fd);
    ares_socket_t as;
    std::unique_ptr<GrpcPolledFd> polled_fd;
    // next fd node
    FdNode* next = nullptr;
    /// if the readable closure has been registered
    bool readable_registered = false;
    /// if the writable closure has been registered
    bool writable_registered = false;
    bool already_shutdown = false;
  };

  // Per ares_channel linked-list of FdNodes
  class FdNodeList {
   public:
    class FdNodeListIterator {
     public:
      bool operator!=(const FdNodeListIterator& other) {
        return node_ != other.node_;
      }
      bool operator==(const FdNodeListIterator& other) {
        return node_ == other.node_;
      }
      FdNodeListIterator& operator++(int) {
        node_ = node_->next;
        return *this;
      }
      FdNode* operator*() { return node_; }
      static FdNodeListIterator universal_end() {
        return FdNodeListIterator(nullptr);
      }

     private:
      friend class FdNodeList;
      explicit FdNodeListIterator(FdNode* node) : node_(node) {}
      FdNode* node_;
    };
    using Iterator = FdNodeListIterator;

    ~FdNodeList() { GPR_ASSERT(IsEmpty()); }

    Iterator begin() { return Iterator(head_); }
    Iterator end() { return Iterator::universal_end(); }

    bool IsEmpty() const { return head_ == nullptr; }

    void PushFdNode(FdNode* fd_node) ABSL_EXCLUSIVE_LOCKS_REQUIRED(&mu_) {
      fd_node->next = head_;
      head_ = fd_node;
    }

    FdNode* PopFdNode() ABSL_EXCLUSIVE_LOCKS_REQUIRED(&mu_) {
      GPR_ASSERT(!IsEmpty());
      FdNode* ret = head_;
      head_ = head_->next;
      return ret;
    }

    // Search for as in the FdNode list. This is an O(n) search, the max
    // possible value of n is ARES_GETSOCK_MAXNUM (16). n is typically 1 - 2
    // in our tests.
    FdNode* PopFdNode(ares_socket_t as) ABSL_EXCLUSIVE_LOCKS_REQUIRED(&mu_);

   private:
    FdNode* head_ = nullptr;
  };
  friend class GrpcPolledFdFactory;

  absl::Status SetRequestDNSServer(absl::string_view dns_server)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  void OnReadable(FdNode* fd_node, absl::Status status)
      ABSL_LOCKS_EXCLUDED(mu_);
  void OnWritable(FdNode* fd_node, absl::Status status)
      ABSL_LOCKS_EXCLUDED(mu_);
  void OnHandleDestroyed(FdNode* fd_node, absl::Status status)
      ABSL_LOCKS_EXCLUDED(mu_);
  void OnQueryTimeout() ABSL_LOCKS_EXCLUDED(mu_);
  void OnAresBackupPollAlarm() ABSL_LOCKS_EXCLUDED(mu_);
  void ShutdownPollerHandlesLocked(absl::Status status)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

 protected:
  grpc_core::Mutex mu_;
  bool initialized_ ABSL_GUARDED_BY(mu_) = false;
  /// name to resolve
  const std::string name_ ABSL_GUARDED_BY(mu_);
  const std::string default_port_ ABSL_GUARDED_BY(mu_);
  // ares channel
  ares_channel channel_ ABSL_GUARDED_BY(mu_) = nullptr;
  /// host to resolve, parsed from name_
  absl::string_view host_ ABSL_GUARDED_BY(mu_);
  /// port, parsed from name_ or is default_port_
  int port_ ABSL_GUARDED_BY(mu_) = 0;
  bool shutting_down_ ABSL_GUARDED_BY(mu_) = false;
  bool cancelled_ ABSL_GUARDED_BY(mu_) = false;
  std::unique_ptr<FdNodeList> fd_node_list_ ABSL_GUARDED_BY(mu_);

  const EventEngine::Duration timeout_ ABSL_GUARDED_BY(mu_);
  absl::optional<EventEngine::TaskHandle> query_timeout_handle_
      ABSL_GUARDED_BY(mu_);
  absl::optional<EventEngine::TaskHandle> ares_backup_poll_alarm_handle_
      ABSL_GUARDED_BY(mu_);
  EventEngine* event_engine_;

  std::unique_ptr<GrpcPolledFdFactory> polled_fd_factory_ ABSL_GUARDED_BY(mu_);
};

// A GrpcAresHostnameRequest represents both "A" and "AAAA" (if available)
// lookup
class GrpcAresHostnameRequest final : public GrpcAresRequest {
 public:
  using Result = std::vector<EventEngine::ResolvedAddress>;

  static absl::StatusOr<GrpcAresHostnameRequest*> Create(
      absl::string_view name, absl::string_view default_port,
      absl::string_view dns_server, bool check_port,
      EventEngine::Duration timeout,
      RegisterAresSocketWithPollerCallback register_cb,
      EventEngine* event_engine);

  // Starting a request, on_resolve is guaranteed to be called with Result or
  // failure status unless the request was cancelled.
  void Start(absl::AnyInvocable<void(absl::StatusOr<Result>)> on_resolve)
      ABSL_LOCKS_EXCLUDED(mu_);

 private:
  // This callback is invoked from the c-ares library, so disable thread
  // safety analysis. Note that we are guaranteed to be holding mu_ though.
  static void OnHostbynameDoneLocked(void* arg, int status, int /*timeouts*/,
                                     struct hostent* hostent)
      ABSL_NO_THREAD_SAFETY_ANALYSIS;

  GrpcAresHostnameRequest(absl::string_view name,
                          absl::string_view default_port,
                          EventEngine::Duration timeout,
                          RegisterAresSocketWithPollerCallback register_cb,
                          EventEngine* event_engine);
  ~GrpcAresHostnameRequest() override;

  bool ResolveAsIPLiteralLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  void LogResolvedAddressesList(const char* input_output_str)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  void SortResolvedAddresses() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  void OnResolve(
      absl::StatusOr<std::vector<EventEngine::ResolvedAddress>> result)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  size_t pending_queries_ ABSL_GUARDED_BY(mu_) = 0;
  std::vector<EventEngine::ResolvedAddress> result_;
  absl::Status error_ ABSL_GUARDED_BY(mu_);
  absl::AnyInvocable<void(absl::StatusOr<Result>)> on_resolve_
      ABSL_GUARDED_BY(mu_);
};

class GrpcAresSRVRequest final : public GrpcAresRequest {
 public:
  using Result = std::vector<EventEngine::DNSResolver::SRVRecord>;

  static absl::StatusOr<GrpcAresSRVRequest*> Create(
      absl::string_view name, EventEngine::Duration timeout,
      absl::string_view dns_server, bool check_port,
      RegisterAresSocketWithPollerCallback register_cb,
      EventEngine* event_engine);

  // Starting a request, on_resolve is guaranteed to be called with Result or
  // failure status unless the request was cancelled.
  void Start(absl::AnyInvocable<void(absl::StatusOr<Result>)> on_resolve)
      ABSL_LOCKS_EXCLUDED(mu_);

 private:
  // This callback is invoked from the c-ares library, so disable thread
  // safety analysis. Note that we are guaranteed to be holding mu_ though.
  static void OnSRVQueryDoneLocked(void* arg, int status, int /*timeouts*/,
                                   unsigned char* abuf,
                                   int alen) ABSL_NO_THREAD_SAFETY_ANALYSIS;

  GrpcAresSRVRequest(absl::string_view name, EventEngine::Duration timeout,
                     RegisterAresSocketWithPollerCallback register_cb,
                     EventEngine* event_engine);
  ~GrpcAresSRVRequest() override;

  void OnResolve(absl::StatusOr<Result> result)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  std::string service_name_;
  absl::AnyInvocable<void(absl::StatusOr<Result>)> on_resolve_;
};

class GrpcAresTXTRequest final : public GrpcAresRequest {
 public:
  using Result = std::string;

  static absl::StatusOr<GrpcAresTXTRequest*> Create(
      absl::string_view name, EventEngine::Duration timeout,
      absl::string_view dns_server, bool check_port,
      RegisterAresSocketWithPollerCallback register_cb,
      EventEngine* event_engine);

  // Starting a request, on_resolve is guaranteed to be called with Result or
  // failure status unless the request was cancelled.
  void Start(absl::AnyInvocable<void(absl::StatusOr<Result>)> on_resolve)
      ABSL_LOCKS_EXCLUDED(mu_);

 private:
  // This callback is invoked from the c-ares library, so disable thread
  // safety analysis. Note that we are guaranteed to be holding mu_ though.
  static void OnTXTDoneLocked(void* arg, int status, int /*timeouts*/,
                              unsigned char* buf,
                              int len) ABSL_NO_THREAD_SAFETY_ANALYSIS;

  GrpcAresTXTRequest(absl::string_view name, EventEngine::Duration timeout,
                     RegisterAresSocketWithPollerCallback register_cb,
                     EventEngine* event_engine);
  ~GrpcAresTXTRequest() override;

  void OnResolve(absl::StatusOr<Result> result)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  std::string config_name_;
  absl::AnyInvocable<void(absl::StatusOr<Result>)> on_resolve_;
};

}  // namespace experimental
}  // namespace grpc_event_engine

// Exposed in this header for C-core tests only
extern void (*ares_driver_test_only_inject_config)(ares_channel channel);

#endif  // GRPC_SRC_CORE_LIB_EVENT_ENGINE_ARES_DRIVER_H

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
#ifndef GRPC_SRC_CORE_LIB_EVENT_ENGINE_ARES_H
#define GRPC_SRC_CORE_LIB_EVENT_ENGINE_ARES_H

#include <grpc/support/port_platform.h>

#include <arpa/inet.h>
#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <ares.h>

#include "absl/base/thread_annotations.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/optional.h"

#include "include/grpc/event_engine/event_engine.h"
#include <grpc/support/log.h>

#include "src/core/lib/gprpp/orphanable.h"

#ifdef _WIN32
#include "src/core/lib/event_engine/windows/iocp.h"
#else
#include "src/core/lib/event_engine/posix_engine/event_poller.h"
#endif

namespace grpc_event_engine {
namespace experimental {

#ifdef _WIN32
// TODO(yijiem): this will not be a WinSocket but a wrapper type (virtual
// socket) which bridges the IOCP model with c-ares socket operations.
using PollerHandle = std::unique_ptr<WinSocket>;
#else
using PollerHandle = EventHandle*;
#endif

using AresSocket = ares_socket_t;

// TODO(yijiem): see if we can use std::list
// per ares-channel linked-list of FdNodes
class FdNodeList {
 public:
  class FdNode {
   public:
    FdNode() = default;
    explicit FdNode(ares_socket_t as, PollerHandle handle)
        : as_(as), handle_(std::move(handle)) {}

    bool readable_registered() const { return readable_registered_; }
    bool writable_registered() const { return writable_registered_; }
    void set_readable_registered(bool rr) { readable_registered_ = rr; }
    void set_writable_registered(bool wr) { writable_registered_ = wr; }

    int WrappedFd() const { return static_cast<int>(as_); }
    PollerHandle& handle() { return handle_; }

   private:
    friend class FdNodeList;

    // ares socket
    ares_socket_t as_;
    // Poller event handle
    PollerHandle handle_;
    // next fd node
    FdNode* next_ = nullptr;
    /// if the readable closure has been registered
    bool readable_registered_ = false;
    /// if the writable closure has been registered
    bool writable_registered_ = false;
  };

  FdNodeList() = default;
  ~FdNodeList() {
    for (FdNode *node = head_, *next = nullptr; node != nullptr; node = next) {
      next = node->next_;
      GPR_ASSERT(!node->readable_registered());
      GPR_ASSERT(!node->writable_registered());
      delete node;
    }
  }

  bool IsEmpty() const { return head_ == nullptr; }

  void PushFdNode(FdNode* fd_node) {
    fd_node->next_ = head_;
    head_ = fd_node;
  }

  FdNode* PopFdNode() {
    GPR_ASSERT(!IsEmpty());
    FdNode* ret = head_;
    head_ = head_->next_;
    return ret;
  }

  // Search for as in the FdNode list. This is an O(n) search, the max
  // possible value of n is ARES_GETSOCK_MAXNUM (16). n is typically 1 - 2
  // in our tests.
  FdNode* PopFdNode(ares_socket_t as) {
    FdNode phony_head;
    phony_head.next_ = head_;
    FdNode* node = &phony_head;
    while (node->next_ != nullptr) {
      if (node->next_->as_ == as) {
        FdNode* ret = node->next_;
        node->next_ = node->next_->next_;
        head_ = phony_head.next_;
        return ret;
      }
      node = node->next_;
    }
    return nullptr;
  }

 private:
  FdNode* head_ = nullptr;
};

// An inflight name service lookup request
class GrpcAresRequest
    : public grpc_core::InternallyRefCounted<GrpcAresRequest> {
 protected:
  using RegisterSocketWithPollerCallback =
      absl::AnyInvocable<PollerHandle(AresSocket)>;

 public:
  explicit GrpcAresRequest(
      absl::string_view name, absl::optional<absl::string_view> default_port,
      EventEngine::Duration timeout,
      RegisterSocketWithPollerCallback register_socket_with_poller_cb,
      EventEngine* event_engine);
  ~GrpcAresRequest() override;

  absl::Status Initialize(absl::string_view dns_server, bool check_port);
  virtual void Start() = 0;
  // Cancel the lookup and start the shutdown process
  void Cancel();
  void Orphan() ABSL_LOCKS_EXCLUDED(mu_) override;

  ares_channel channel() const { return channel_; }
  absl::string_view host() const { return host_; }
  uint16_t port() const { return port_; }
  bool shutting_down() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    return shutting_down_;
  }
  void set_shutting_down(bool shutting_down)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    shutting_down_ = shutting_down;
  }
  const FdNodeList* fd_node_list() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    return fd_node_list_.get();
  }

  std::string ToString() const {
    std::ostringstream s;
    s << "[channel: " << channel_ << "; host: " << host_
      << "; port: " << ntohs(port_) << "; timeout: " << timeout_.count()
      << "ns]";
    return s.str();
  }

 protected:
  void Work() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

 private:
  absl::Status SetRequestDNSServer(absl::string_view dns_server)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  void OnReadable(FdNodeList::FdNode* fd_node, absl::Status status)
      ABSL_LOCKS_EXCLUDED(mu_);
  void OnWritable(FdNodeList::FdNode* fd_node, absl::Status status)
      ABSL_LOCKS_EXCLUDED(mu_);
  void OnHandleDestroyed(FdNodeList::FdNode* fd_node, absl::Status status)
      ABSL_LOCKS_EXCLUDED(mu_);

 protected:
  template <typename T>
  using OnResolveCallback =
      absl::AnyInvocable<void(absl::StatusOr<T>, intptr_t)>;

  bool initialized_ = false;
  /// synchronizes access to this request, and also to associated
  /// ev_driver and fd_node objects
  absl::Mutex mu_;
  const std::string name_;
  const std::string default_port_;
  // ares channel
  ares_channel channel_ = nullptr;
  /// host to resolve, parsed from the name to resolve
  absl::string_view host_;
  /// port to fill in sockaddr_in, parsed from the name to resolve
  uint16_t port_ = 0;
  const EventEngine::Duration timeout_;
  struct ares_addr_port_node dns_server_addr_ ABSL_GUARDED_BY(mu_);
  size_t pending_queries_ ABSL_GUARDED_BY(mu_) = 0;
  bool shutting_down_ ABSL_GUARDED_BY(mu_) = false;
  absl::Status error_ = absl::OkStatus();
  RegisterSocketWithPollerCallback register_socket_with_poller_cb_;
  std::unique_ptr<FdNodeList> fd_node_list_ ABSL_GUARDED_BY(mu_);
  EventEngine* event_engine_;
};

// A GrpcAresHostnameRequest represents both "A" and "AAAA" (if available)
// lookup
class GrpcAresHostnameRequest : public GrpcAresRequest {
 private:
  using Result = std::vector<EventEngine::ResolvedAddress>;

 public:
  explicit GrpcAresHostnameRequest(
      absl::string_view name, absl::string_view default_port,
      EventEngine::Duration timeout, bool is_balancer,
      RegisterSocketWithPollerCallback register_socket_with_poller_cb,
      OnResolveCallback<Result> on_resolve, EventEngine* event_engine)
      : GrpcAresRequest(name, default_port, timeout,
                        std::move(register_socket_with_poller_cb),
                        event_engine),
        is_balancer_(is_balancer),
        on_resolve_(std::move(on_resolve)) {}

  bool is_balancer() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    return is_balancer_;
  }
  const char* qtype() const { return "Unimplemented"; }
  void Start() ABSL_LOCKS_EXCLUDED(mu_) override;
  void OnResolve(
      absl::StatusOr<std::vector<EventEngine::ResolvedAddress>> result)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

 private:
  ~GrpcAresHostnameRequest() override;

  std::vector<EventEngine::ResolvedAddress> result_;
  /// is it a grpclb address
  const bool is_balancer_ ABSL_GUARDED_BY(mu_);
  OnResolveCallback<Result> on_resolve_ ABSL_GUARDED_BY(mu_);
};

class GrpcAresSRVRequest : public GrpcAresRequest {
 private:
  using Result = std::vector<EventEngine::DNSResolver::SRVRecord>;

 public:
  explicit GrpcAresSRVRequest(
      absl::string_view name, EventEngine::Duration timeout,
      RegisterSocketWithPollerCallback register_socket_with_poller_cb,
      OnResolveCallback<Result> on_resolve, EventEngine* event_engine)
      : GrpcAresRequest(name, absl::nullopt, timeout,
                        std::move(register_socket_with_poller_cb),
                        event_engine),
        on_resolve_(std::move(on_resolve)) {}
  const char* service_name() const { return service_name_.c_str(); }
  void Start() ABSL_LOCKS_EXCLUDED(mu_) override;
  void OnResolve(absl::StatusOr<Result> result)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

 private:
  std::string service_name_;
  OnResolveCallback<Result> on_resolve_;
};

class GrpcAresTXTRequest : public GrpcAresRequest {
 private:
  using Result = std::basic_string<unsigned char>;

 public:
  explicit GrpcAresTXTRequest(
      absl::string_view name, EventEngine::Duration timeout,
      RegisterSocketWithPollerCallback register_socket_with_poller_cb,
      OnResolveCallback<Result> on_resolve, EventEngine* event_engine)
      : GrpcAresRequest(name, absl::nullopt, timeout,
                        std::move(register_socket_with_poller_cb),
                        event_engine),
        on_resolve_(std::move(on_resolve)) {}
  const char* config_name() const { return config_name_.c_str(); }
  void Start() ABSL_LOCKS_EXCLUDED(mu_) override;
  void OnResolve(absl::StatusOr<Result> result)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

 private:
  std::string config_name_;
  OnResolveCallback<Result> on_resolve_;
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_SRC_CORE_LIB_EVENT_ENGINE_ARES_H

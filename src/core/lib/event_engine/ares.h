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

#include <ares.h>

#include "include/grpc/event_engine/event_engine.h"

#include "src/core/lib/gprpp/orphanable.h"

namespace grpc_event_engine {
namespace experimental {

// TODO(yijiem): see if we can use std::list
// per ares-channel linked-list of FdNodes
class FdNodeList {
 public:
  class FdNode {
   public:
    FdNode() = default;
    explicit FdNode(ares_socket_t as, EventHandle* ev_handle)
        : as_(as), ev_handle_(ev_handle) {}

    bool readable_registered() const { return readable_registered_; }
    bool writable_registered() const { return writable_registered_; }
    void set_readable_registered(bool rr) { readable_registered_ = rr; }
    void set_writable_registered(bool wr) { writable_registered_ = wr; }

    int WrappedFd() const { return static_cast<int>(as_); }
    EventHandle* event_handle() const { return ev_handle_; }

   private:
    friend class FdNodeList;

    // ares socket
    ares_socket_t as_;
    // Poller event handle
    EventHandle* ev_handle_;
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
 public:
  explicit GrpcAresRequest(absl::string_view host, uint16_t port,
                           Duration timeout)
      : channel_(channel), host_(host), port_(port), timeout_(timeout) {}

  bool Initialize();
  virtual void Start() = 0;
  // Starts the shutdown process
  void Orphan() override;

  ares_channel channel() const { return channel_; }
  const char* host() const { return host_.c_str(); }
  uint16_t port() const { return port_; }
  bool shutting_down() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    return shutting_down_;
  }
  void set_shutting_down(bool shutting_down)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    shutting_down_ = shutting_down;
  }
  std::unique_ptr<FdNodeList>& fd_node_list()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    return fd_node_list_;
  }

  std::string ToString() const {
    std::ostringstream s;
    s << "[channel: " << channel_ << "; host: " << host_
      << "; port: " << ntohs(port_) << "; timeout: " << timeout_.count()
      << "ns]";
    return s.str();
  }

 private:
  ~GrpcAresRequest() override;

  void Work();
  void OnReadable(FdNodeList::FdNode* fd_node, absl::Status status);
  void OnWritable(FdNodeList::FdNode* fd_node, absl::Status status);
  void OnHandleDestroyed(FdNodeList::FdNode* fd_node, absl::Status status);

 protected:
  bool initialized_ = false;
  /// synchronizes access to this request, and also to associated
  /// ev_driver and fd_node objects
  grpc_core::Mutex mu_;
  // ares channel
  ares_channel channel_;
  /// host to resolve, parsed from the name to resolve
  const std::string host_;
  /// port to fill in sockaddr_in, parsed from the name to resolve
  const uint16_t port_;
  const Duration timeout_;
  bool shutting_down_ ABSL_GUARDED_BY(mu_) = false;
  std::unique_ptr<FdNodeList> fd_node_list_ ABSL_GUARDED_BY(mu_) =
      std::make_unique<FdNodeList>();
};

// A GrpcAresHostnameRequest represents both "A" and "AAAA" (if available)
// lookup
class GrpcAresHostnameRequest : public GrpcAresRequest {
 public:
  explicit GrpcAresHostnameRequest(absl::string_view host, uint16_t port,
                                   Duration timeout,
                                   LookupHostnameCallback on_resolve);

  bool is_balancer() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    return is_balancer_;
  }
  const char* qtype() const { return "Unimplemented"; }

  void Start() override;
  void OnResolve(absl::StatusOr<std::vector<ResolvedAddress>> result);

 private:
  ~GrpcAresHostnameRequest() override = default;

  size_t pending_queries_ = 0;
  std::vector<ResolvedAddress> result_;
  /// is it a grpclb address
  bool is_balancer_ ABSL_GUARDED_BY(mu_) = false;
  LookupHostnameCallback on_resolve_ ABSL_GUARDED_BY(mu_);
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_SRC_CORE_LIB_EVENT_ENGINE_ARES_H

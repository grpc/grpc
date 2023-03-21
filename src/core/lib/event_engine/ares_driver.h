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

#include <arpa/inet.h>
#include <stddef.h>
#include <stdint.h>

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

// An inflight name service lookup request
class GrpcAresRequest
    : public grpc_core::InternallyRefCounted<GrpcAresRequest> {
 public:
  ~GrpcAresRequest() override;

  absl::Status Initialize(absl::string_view dns_server, bool check_port)
      ABSL_LOCKS_EXCLUDED(mu_);
  void Cancel() ABSL_LOCKS_EXCLUDED(mu_);
  absl::string_view host() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    return host_;
  }
  uint16_t port() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) { return port_; }

 protected:
  using CreateEventHandleCallback =
      absl::AnyInvocable<PollerHandle(AresSocket)>;

  explicit GrpcAresRequest(absl::string_view name,
                           absl::optional<absl::string_view> default_port,
                           EventEngine::Duration timeout,
                           CreateEventHandleCallback create_event_handle_cb,
                           EventEngine* event_engine);

  // Cancel the lookup and start the shutdown process
  void Orphan() ABSL_LOCKS_EXCLUDED(mu_) override;
  std::string ToString() const {
    std::ostringstream s;
    s << "[channel: " << channel_ << "; host: " << host_
      << "; port: " << ntohs(port_) << "; timeout: " << timeout_.count()
      << "ns]";
    return s.str();
  }

  void Work() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

 private:
  struct FdNode;
  class FdNodeList;
  absl::Status SetRequestDNSServer(absl::string_view dns_server)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  void OnReadable(FdNode* fd_node, absl::Status status)
      ABSL_LOCKS_EXCLUDED(mu_);
  void OnWritable(FdNode* fd_node, absl::Status status)
      ABSL_LOCKS_EXCLUDED(mu_);
  void OnHandleDestroyed(FdNode* fd_node, absl::Status status)
      ABSL_LOCKS_EXCLUDED(mu_);

 protected:
  template <typename T>
  using OnResolveCallback =
      absl::AnyInvocable<void(absl::StatusOr<T>, intptr_t)>;

  bool initialized_ = false;
  /// synchronizes access to this request, and also to associated
  /// ev_driver and fd_node objects
  absl::Mutex mu_;
  /// name to resolve
  const std::string name_;
  const std::string default_port_;
  // ares channel
  ares_channel channel_ = nullptr;
  /// host to resolve, parsed from the name to resolve
  absl::string_view host_;
  /// port to fill in sockaddr_in, parsed from the name to resolve
  /// This is in network byte order.
  uint16_t port_ = 0;
  const EventEngine::Duration timeout_;
  struct ares_addr_port_node dns_server_addr_ ABSL_GUARDED_BY(mu_);
  size_t pending_queries_ ABSL_GUARDED_BY(mu_) = 0;
  bool shutting_down_ ABSL_GUARDED_BY(mu_) = false;
  bool cancelled_ ABSL_GUARDED_BY(mu_) = false;
  absl::Status error_ = absl::OkStatus();
  CreateEventHandleCallback create_event_handle_cb_;
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
      EventEngine::Duration timeout,
      CreateEventHandleCallback create_event_handle_cb,
      EventEngine* event_engine);

  void Start(OnResolveCallback<Result> on_resolve) ABSL_LOCKS_EXCLUDED(mu_);
  void OnResolve(
      absl::StatusOr<std::vector<EventEngine::ResolvedAddress>> result)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

 private:
  ~GrpcAresHostnameRequest() override;
  bool ResolveAsIPLiteralLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  void LogResolvedAddressesList(const char* input_output_str)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  void SortResolvedAddresses() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  std::vector<EventEngine::ResolvedAddress> result_;
  OnResolveCallback<Result> on_resolve_ ABSL_GUARDED_BY(mu_);
};

class GrpcAresSRVRequest : public GrpcAresRequest {
 private:
  using Result = std::vector<EventEngine::DNSResolver::SRVRecord>;

 public:
  explicit GrpcAresSRVRequest(absl::string_view name,
                              EventEngine::Duration timeout,
                              CreateEventHandleCallback create_event_handle_cb,
                              EventEngine* event_engine);
  const char* service_name() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    return service_name_.c_str();
  }
  void Start(OnResolveCallback<Result> on_resolve) ABSL_LOCKS_EXCLUDED(mu_);
  void OnResolve(absl::StatusOr<Result> result)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

 private:
  std::string service_name_;
  OnResolveCallback<Result> on_resolve_;
};

class GrpcAresTXTRequest : public GrpcAresRequest {
 private:
  using Result = std::string;

 public:
  explicit GrpcAresTXTRequest(absl::string_view name,
                              EventEngine::Duration timeout,
                              CreateEventHandleCallback create_event_handle_cb,
                              EventEngine* event_engine);
  const char* config_name() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    return config_name_.c_str();
  }
  void Start(OnResolveCallback<Result> on_resolve) ABSL_LOCKS_EXCLUDED(mu_);
  void OnResolve(absl::StatusOr<Result> result)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

 private:
  std::string config_name_;
  OnResolveCallback<Result> on_resolve_;
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_SRC_CORE_LIB_EVENT_ENGINE_ARES_DRIVER_H

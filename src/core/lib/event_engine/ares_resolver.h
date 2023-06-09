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

#if GRPC_ARES == 1

#include <list>
#include <memory>
#include <variant>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/variant.h"

#include "src/core/lib/event_engine/event_engine.h"
#include "src/core/lib/event_engine/grpc_polled_fd.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/sync.h"

namespace grpc_event_engine {
namespace experimental {

class AresResolver : public grpc_core::InternallyRefCounted<AresResolver> {
 public:
  using Result = absl::variant<std::vector<EventEngine::ResolvedAddress>,
                               std::vector<EventEngine::DNSResolver::SRVRecord>,
                               std::vector<std::string>>;

  AresResolver(std::unique_ptr<GrpcPolledFdFactory> polled_fd_factory,
               EventEngine* event_engine)
      : polled_fd_factory_(std::move(polled_fd_factory)),
        event_engine_(event_engine) {}

  bool Initialize(absl::string_view dns_server);
  void Lookup(LookupType type, absl::string_view name,
              absl::AnyInvocable<void(absl::StatusOr<Result>)> on_complete);

  void Orphan() override;

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

  absl::Status SetRequestDNSServer(absl::string_view dns_server);
  void WorkLocked();
  void OnReadable(FdNode* fd_node, absl::Status status);
  void OnWritable(FdNode* fd_node, absl::Status status);

  static void OnHostbynameDoneLocked(void* arg, int status, int /*timeouts*/,
                                     struct hostent* hostent);
  static void OnSRVQueryDoneLocked(void* arg, int status, int /*timeouts*/,
                                   unsigned char* abuf, int alen);
  static void OnTXTDoneLocked(void* arg, int status, int /*timeouts*/,
                              unsigned char* buf, int len);

 private:
  grpc_core::Mutex mutex_;
  bool initialized_ = false;
  bool done_ = false;
  ares_channel channel_;
  FdNodeList fd_node_list_;
  std::unique_ptr<GrpcPolledFdFactory> polled_fd_factory_;
  EventEngine* event_engine_;
};

class HostnameQuery : public grpc_core::RefCounted<HostnameQuery> {
 public:
  using Result = std::vector<EventEngine::ResolvedAddress>;

  HostnameQuery(EventEngine* event_engine, AresResolver* ares_resolver)
      : event_engine_(event_engine), ares_resolver_(ares_resolver) {}

  void Lookup(absl::string_view name, absl::string_view default_port,
              LookupHostnameCallback on_resolve);

 private:
  grpc_core::Mutex mutex_;
  EventEngine* event_engine_;
  AresResolver* ares_resolver_;
  int pending_requests_ = 0;
  absl::StatusOr<Result> result_;
  LookupHostnameCallback on_resolve_;
};

class SRVQuery : public grpc_core::RefCounted<SRVQuery> {
 public:
  using Result = std::vector<EventEngine::DNSResolver::SRVRecord>;

  SRVQuery(EventEngine* event_engine, AresResolver* ares_resolver)
      : event_engine_(event_engine), ares_resolver_(ares_resolver) {}

  void Lookup(absl::string_view name, LookupSRVCallback on_resolve);

 private:
  EventEngine* event_engine_;
  AresResolver* ares_resolver_;
  LookupSRVCallback on_resolve_;
};

class TXTQuery : public grpc_core::RefCounted<TXTQuery> {
 public:
  using Result = std::vector<std::string>;

  TXTQuery(EventEngine* event_engine, AresResolver* ares_resolver)
      : event_engine_(event_engine), ares_resolver_(ares_resolver) {}

  void Lookup(absl::string_view name, LookupTXTCallback on_resolve);

 private:
  EventEngine* event_engine_;
  AresResolver* ares_resolver_;
  LookupTXTCallback on_resolve_;
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_ARES == 1
#endif  // GRPC_SRC_CORE_LIB_EVENT_ENGINE_ARES_RESOLVER_H
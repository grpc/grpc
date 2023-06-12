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

#include <ares.h>

#include "absl/container/flat_hash_map.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "absl/types/variant.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/log.h>

#include "src/core/lib/event_engine/grpc_polled_fd.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/sync.h"

namespace grpc_event_engine {
namespace experimental {

extern grpc_core::TraceFlag grpc_trace_ares_resolver;

#define GRPC_ARES_RESOLVER_TRACE_LOG(format, ...)                            \
  do {                                                                       \
    if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_ares_resolver)) {                 \
      gpr_log(GPR_INFO, "(EventEngine ares_resolver) " format, __VA_ARGS__); \
    }                                                                        \
  } while (0)

class AresResolver;

class HostnameQuery : public grpc_core::RefCounted<HostnameQuery> {
 public:
  using Result = std::vector<EventEngine::ResolvedAddress>;

  HostnameQuery(EventEngine* event_engine, AresResolver* ares_resolver);

  void Lookup(absl::string_view name, absl::string_view default_port,
              EventEngine::DNSResolver::LookupHostnameCallback on_resolve);

 private:
  void MaybeOnResolve(absl::StatusOr<Result> result_or);
  void LogResolvedAddressesListLocked(absl::string_view input_output_str);
  void MaybeSortResolvedAddresses();

  grpc_core::Mutex mutex_;
  EventEngine* event_engine_;
  AresResolver* ares_resolver_;
  int pending_requests_ = 0;
  absl::StatusOr<Result> result_;
  EventEngine::DNSResolver::LookupHostnameCallback on_resolve_;
};

class SRVQuery : public grpc_core::RefCounted<SRVQuery> {
 public:
  using Result = std::vector<EventEngine::DNSResolver::SRVRecord>;

  SRVQuery(EventEngine* event_engine, AresResolver* ares_resolver);

  void Lookup(absl::string_view name,
              EventEngine::DNSResolver::LookupSRVCallback on_resolve);

 private:
  EventEngine* event_engine_;
  AresResolver* ares_resolver_;
  EventEngine::DNSResolver::LookupSRVCallback on_resolve_;
};

class TXTQuery : public grpc_core::RefCounted<TXTQuery> {
 public:
  using Result = std::vector<std::string>;

  TXTQuery(EventEngine* event_engine, AresResolver* ares_resolver);

  void Lookup(absl::string_view name,
              EventEngine::DNSResolver::LookupTXTCallback on_resolve);

 private:
  EventEngine* event_engine_;
  AresResolver* ares_resolver_;
  EventEngine::DNSResolver::LookupTXTCallback on_resolve_;
};

class AresResolver : public grpc_core::InternallyRefCounted<AresResolver> {
 public:
  using Result = absl::variant<std::vector<EventEngine::ResolvedAddress>,
                               std::vector<EventEngine::DNSResolver::SRVRecord>,
                               std::vector<std::string>>;

  AresResolver(std::unique_ptr<GrpcPolledFdFactory> polled_fd_factory,
               EventEngine* event_engine);

  ~AresResolver() override;

  absl::Status Initialize(absl::string_view dns_server);
  void Orphan() override;

 private:
  friend class HostnameQuery;
  friend class SRVQuery;
  friend class TXTQuery;

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

  absl::Status SetRequestDNSServer(absl::string_view dns_server);
  void WorkLocked();
  void MaybeStartTimerLocked();
  void OnReadable(FdNode* fd_node, absl::Status status);
  void OnWritable(FdNode* fd_node, absl::Status status);
  void OnAresBackupPollAlarm();

  void LookupHostname(
      absl::string_view name, int port, int family,
      absl::AnyInvocable<void(absl::StatusOr<Result>)> callback);
  void LookupSRV(absl::string_view name,
                 absl::AnyInvocable<void(absl::StatusOr<Result>)> callback);
  void LookupTXT(absl::string_view name,
                 absl::AnyInvocable<void(absl::StatusOr<Result>)> callback);
  static void OnHostbynameDoneLocked(void* arg, int status, int /*timeouts*/,
                                     struct hostent* hostent);
  static void OnSRVQueryDoneLocked(void* arg, int status, int /*timeouts*/,
                                   unsigned char* abuf, int alen);
  static void OnTXTDoneLocked(void* arg, int status, int /*timeouts*/,
                              unsigned char* buf, int len);

 private:
  grpc_core::Mutex mutex_;
  bool initialized_ = false;
  bool shutting_down_ = false;
  ares_channel channel_;
  FdNodeList fd_node_list_;
  int id_ = 0;
  absl::flat_hash_map<
      int, absl::AnyInvocable<void(absl::StatusOr<AresResolver::Result>)>>
      callback_map_;
  absl::optional<EventEngine::TaskHandle> ares_backup_poll_alarm_handle_;
  std::unique_ptr<GrpcPolledFdFactory> polled_fd_factory_;
  EventEngine* event_engine_;
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_ARES == 1
#endif  // GRPC_SRC_CORE_LIB_EVENT_ENGINE_ARES_RESOLVER_H
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
#include <grpc/support/port_platform.h>

#include "src/core/lib/event_engine/ares_resolver.h"

#include <stdint.h>

#include <string>
#include <vector>

#include "src/core/lib/iomgr/port.h"

// IWYU pragma: no_include <ares_version.h>
// IWYU pragma: no_include <arpa/inet.h>
// IWYU pragma: no_include <arpa/nameser.h>
// IWYU pragma: no_include <inttypes.h>
// IWYU pragma: no_include <netdb.h>
// IWYU pragma: no_include <netinet/in.h>
// IWYU pragma: no_include <stdlib.h>
// IWYU pragma: no_include <sys/socket.h>
// IWYU pragma: no_include <ratio>

#if GRPC_ARES == 1

#include <ares.h>

#if ARES_VERSION >= 0x011200
// c-ares 1.18.0 or later starts to provide ares_nameser.h as a public header.
#include <ares_nameser.h>
#else
#include "src/core/lib/event_engine/nameser.h"  // IWYU pragma: keep
#endif

#include <string.h>

#include <algorithm>
#include <chrono>
#include <initializer_list>
#include <memory>
#include <type_traits>
#include <utility>

#include "absl/functional/any_invocable.h"
#include "absl/hash/hash.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/types/optional.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/log.h>

#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/event_engine/grpc_polled_fd.h"
#include "src/core/lib/event_engine/time_util.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/resolved_address.h"
#include "src/core/lib/iomgr/sockaddr.h"
#ifdef GRPC_POSIX_SOCKET_ARES_EV_DRIVER
#include "src/core/lib/event_engine/posix_engine/tcp_socket_utils.h"
#endif

namespace grpc_event_engine {
namespace experimental {

grpc_core::TraceFlag grpc_trace_ares_resolver(false, "cares_resolver");

namespace {

absl::Status AresStatusToAbslStatus(int status, absl::string_view error_msg) {
  switch (status) {
    case ARES_ECANCELLED:
      return absl::CancelledError(error_msg);
    case ARES_ENOTIMP:
      return absl::UnimplementedError(error_msg);
    case ARES_ENOTFOUND:
      return absl::NotFoundError(error_msg);
    default:
      return absl::UnknownError(error_msg);
  }
}

// An alternative here could be to use ares_timeout to try to be more
// accurate, but that would require using "struct timeval"'s, which just
// makes things a bit more complicated. So just poll every second, as
// suggested by the c-ares code comments.
constexpr EventEngine::Duration kAresBackupPollAlarmDuration =
    std::chrono::seconds(1);

bool IsIpv6LoopbackAvailable() {
#ifdef GRPC_POSIX_SOCKET_ARES_EV_DRIVER
  return PosixSocketWrapper::IsIpv6LoopbackAvailable();
#elif defined(GRPC_WINDOWS_SOCKET_ARES_EV_DRIVER)
  // TODO(yijiem): implement this for Windows
  return true;
#else
#error "Unsupported platform"
#endif
}

absl::Status SetRequestDNSServer(absl::string_view dns_server,
                                 ares_channel* channel) {
  GRPC_ARES_RESOLVER_TRACE_LOG("Using DNS server %s", dns_server.data());
  grpc_resolved_address addr;
  struct ares_addr_port_node dns_server_addr = {};
  if (grpc_parse_ipv4_hostport(dns_server, &addr, /*log_errors=*/false)) {
    dns_server_addr.family = AF_INET;
    struct sockaddr_in* in = reinterpret_cast<struct sockaddr_in*>(addr.addr);
    memcpy(&dns_server_addr.addr.addr4, &in->sin_addr, sizeof(struct in_addr));
    dns_server_addr.tcp_port = grpc_sockaddr_get_port(&addr);
    dns_server_addr.udp_port = grpc_sockaddr_get_port(&addr);
  } else if (grpc_parse_ipv6_hostport(dns_server, &addr,
                                      /*log_errors=*/false)) {
    dns_server_addr.family = AF_INET6;
    struct sockaddr_in6* in6 =
        reinterpret_cast<struct sockaddr_in6*>(addr.addr);
    memcpy(&dns_server_addr.addr.addr6, &in6->sin6_addr,
           sizeof(struct in6_addr));
    dns_server_addr.tcp_port = grpc_sockaddr_get_port(&addr);
    dns_server_addr.udp_port = grpc_sockaddr_get_port(&addr);
  } else {
    return absl::InvalidArgumentError(
        absl::StrCat("Cannot parse authority: ", dns_server));
  }
  int status = ares_set_servers_ports(*channel, &dns_server_addr);
  if (status != ARES_SUCCESS) {
    return AresStatusToAbslStatus(status, ares_strerror(status));
  }
  return absl::OkStatus();
}

struct QueryArg {
  QueryArg(AresResolver* ar, int id, absl::string_view name)
      : ares_resolver(ar), callback_map_id(id), query_name(name) {}
  AresResolver* ares_resolver;
  int callback_map_id;
  std::string query_name;
};

struct HostnameQueryArg : public QueryArg {
  HostnameQueryArg(AresResolver* ar, int id, absl::string_view name, int p)
      : QueryArg(ar, id, name), port(p) {}
  int port;
};

}  // namespace

absl::StatusOr<grpc_core::OrphanablePtr<AresResolver>>
AresResolver::CreateAresResolver(
    absl::string_view dns_server,
    std::unique_ptr<GrpcPolledFdFactory> polled_fd_factory,
    std::shared_ptr<EventEngine> event_engine) {
  ares_options opts = {};
  opts.flags |= ARES_FLAG_STAYOPEN;
  if (g_event_engine_grpc_ares_test_only_force_tcp) {
    opts.flags |= ARES_FLAG_USEVC;
  }
  ares_channel channel;
  int status = ares_init_options(&channel, &opts, ARES_OPT_FLAGS);
  if (status != ARES_SUCCESS) {
    gpr_log(GPR_ERROR, "ares_init_options failed, status: %d", status);
    return AresStatusToAbslStatus(
        status,
        absl::StrCat("Failed to init c-ares channel: ", ares_strerror(status)));
  }
  event_engine_grpc_ares_test_only_inject_config(&channel);
  polled_fd_factory->ConfigureAresChannelLocked(channel);
  if (!dns_server.empty()) {
    absl::Status status = SetRequestDNSServer(dns_server, &channel);
    if (!status.ok()) {
      return status;
    }
  }
  return grpc_core::MakeOrphanable<AresResolver>(
      std::move(polled_fd_factory), std::move(event_engine), channel);
}

AresResolver::~AresResolver() {
  GPR_ASSERT(fd_node_list_.empty());
  GPR_ASSERT(callback_map_.empty());
  ares_destroy(channel_);
}

void AresResolver::Orphan() {
  {
    grpc_core::MutexLock lock(&mutex_);
    shutting_down_ = true;
    if (ares_backup_poll_alarm_handle_.has_value()) {
      event_engine_->Cancel(*ares_backup_poll_alarm_handle_);
      ares_backup_poll_alarm_handle_.reset();
    }
    for (const auto& fd_node : fd_node_list_) {
      if (!fd_node->already_shutdown) {
        GRPC_ARES_RESOLVER_TRACE_LOG("resolver: %p shutdown fd: %s", this,
                                     fd_node->polled_fd->GetName());
        fd_node->polled_fd->ShutdownLocked(
            absl::CancelledError("AresResolver::Orphan"));
        fd_node->already_shutdown = true;
      }
    }
  }
  Unref(DEBUG_LOCATION, "Orphan");
}

void AresResolver::LookupHostname(
    absl::string_view name, absl::string_view default_port,
    EventEngine::DNSResolver::LookupHostnameCallback callback) {
  absl::string_view host;
  absl::string_view port_string;
  if (!grpc_core::SplitHostPort(name, &host, &port_string)) {
    event_engine_->Run(
        [callback = std::move(callback),
         status = absl::InvalidArgumentError(absl::StrCat(
             "Unparseable name: ", name))]() mutable { callback(status); });
    return;
  }
  GPR_ASSERT(!host.empty());
  if (port_string.empty()) {
    if (default_port.empty()) {
      event_engine_->Run([callback = std::move(callback),
                          status = absl::InvalidArgumentError(absl::StrFormat(
                              "No port in name %s or default_port argument",
                              name))]() mutable { callback(status); });
      return;
    }
    port_string = default_port;
  }
  int port = 0;
  if (port_string == "http") {
    port = 80;
  } else if (port_string == "https") {
    port = 443;
  } else if (!absl::SimpleAtoi(port_string, &port)) {
    event_engine_->Run([callback = std::move(callback),
                        status = absl::InvalidArgumentError(absl::StrCat(
                            "Failed to parse port in name: ",
                            name))]() mutable { callback(status); });
    return;
  }
  // TODO(yijiem): Change this when refactoring code in
  // src/core/lib/address_utils to use EventEngine::ResolvedAddress.
  grpc_resolved_address addr;
  const std::string hostport = grpc_core::JoinHostPort(host, port);
  if (grpc_parse_ipv4_hostport(hostport.c_str(), &addr,
                               false /* log errors */) ||
      grpc_parse_ipv6_hostport(hostport.c_str(), &addr,
                               false /* log errors */)) {
    // Early out if the target is an ipv4 or ipv6 literal.
    std::vector<EventEngine::ResolvedAddress> result;
    result.emplace_back(reinterpret_cast<sockaddr*>(addr.addr), addr.len);
    event_engine_->Run(
        [callback = std::move(callback), result = std::move(result)]() mutable {
          callback(std::move(result));
        });
    return;
  }
  grpc_core::MutexLock lock(&mutex_);
  callback_map_.emplace(++id_, std::move(callback));
  auto* resolver_arg = new HostnameQueryArg(this, id_, name, port);
  if (IsIpv6LoopbackAvailable()) {
    ares_gethostbyname(channel_, std::string(host).c_str(), AF_UNSPEC,
                       &AresResolver::OnHostbynameDoneLocked, resolver_arg);
  } else {
    ares_gethostbyname(channel_, std::string(host).c_str(), AF_INET,
                       &AresResolver::OnHostbynameDoneLocked, resolver_arg);
  }
  CheckSocketsLocked();
  MaybeStartTimerLocked();
}

void AresResolver::LookupSRV(
    absl::string_view name,
    EventEngine::DNSResolver::LookupSRVCallback callback) {
  absl::string_view host;
  absl::string_view port;
  if (!grpc_core::SplitHostPort(name, &host, &port)) {
    event_engine_->Run(
        [callback = std::move(callback),
         status = absl::InvalidArgumentError(absl::StrCat(
             "Unparseable name: ", name))]() mutable { callback(status); });
    return;
  }
  GPR_ASSERT(!host.empty());
  // Don't query for SRV records if the target is "localhost"
  if (absl::EqualsIgnoreCase(host, "localhost")) {
    event_engine_->Run([callback = std::move(callback)]() mutable {
      callback(std::vector<EventEngine::DNSResolver::SRVRecord>());
    });
    return;
  }
  grpc_core::MutexLock lock(&mutex_);
  callback_map_.emplace(++id_, std::move(callback));
  auto* resolver_arg = new QueryArg(this, id_, host);
  ares_query(channel_, std::string(host).c_str(), ns_c_in, ns_t_srv,
             &AresResolver::OnSRVQueryDoneLocked, resolver_arg);
  CheckSocketsLocked();
  MaybeStartTimerLocked();
}

void AresResolver::LookupTXT(
    absl::string_view name,
    EventEngine::DNSResolver::LookupTXTCallback callback) {
  absl::string_view host;
  absl::string_view port;
  if (!grpc_core::SplitHostPort(name, &host, &port)) {
    event_engine_->Run(
        [callback = std::move(callback),
         status = absl::InvalidArgumentError(absl::StrCat(
             "Unparseable name: ", name))]() mutable { callback(status); });
    return;
  }
  GPR_ASSERT(!host.empty());
  // Don't query for TXT records if the target is "localhost"
  if (absl::EqualsIgnoreCase(host, "localhost")) {
    event_engine_->Run([callback = std::move(callback)]() mutable {
      callback(std::vector<std::string>());
    });
    return;
  }
  grpc_core::MutexLock lock(&mutex_);
  callback_map_.emplace(++id_, std::move(callback));
  auto* resolver_arg = new QueryArg(this, id_, host);
  ares_search(channel_, std::string(host).c_str(), ns_c_in, ns_t_txt,
              &AresResolver::OnTXTDoneLocked, resolver_arg);
  CheckSocketsLocked();
  MaybeStartTimerLocked();
}

AresResolver::AresResolver(
    std::unique_ptr<GrpcPolledFdFactory> polled_fd_factory,
    std::shared_ptr<EventEngine> event_engine, ares_channel channel)
    : grpc_core::InternallyRefCounted<AresResolver>(
          GRPC_TRACE_FLAG_ENABLED(grpc_trace_ares_resolver) ? "AresResolver"
                                                            : nullptr),
      channel_(channel),
      polled_fd_factory_(std::move(polled_fd_factory)),
      event_engine_(std::move(event_engine)) {}

void AresResolver::CheckSocketsLocked() {
  FdNodeList new_list;
  if (!shutting_down_) {
    ares_socket_t socks[ARES_GETSOCK_MAXNUM];
    int socks_bitmask = ares_getsock(channel_, socks, ARES_GETSOCK_MAXNUM);
    for (size_t i = 0; i < ARES_GETSOCK_MAXNUM; i++) {
      if (ARES_GETSOCK_READABLE(socks_bitmask, i) ||
          ARES_GETSOCK_WRITABLE(socks_bitmask, i)) {
        auto iter = std::find_if(
            fd_node_list_.begin(), fd_node_list_.end(),
            [sock = socks[i]](const auto& node) { return node->as == sock; });
        if (iter == fd_node_list_.end()) {
          GRPC_ARES_RESOLVER_TRACE_LOG("resolver:%p new fd: %d", this,
                                       socks[i]);
          new_list.push_back(std::make_unique<FdNode>(
              socks[i], polled_fd_factory_->NewGrpcPolledFdLocked(socks[i])));
        } else {
          new_list.splice(new_list.end(), fd_node_list_, iter);
        }
        FdNode* fd_node = new_list.back().get();
        if (ARES_GETSOCK_READABLE(socks_bitmask, i) &&
            !fd_node->readable_registered) {
          fd_node->readable_registered = true;
          if (fd_node->polled_fd->IsFdStillReadableLocked()) {
            // If c-ares is interested to read and the socket already has data
            // available for read, schedules OnReadable directly here. This is
            // to cope with the edge-triggered poller not getting an event if no
            // new data arrives and c-ares hasn't read all the data in the
            // previous ares_process_fd.
            GRPC_ARES_RESOLVER_TRACE_LOG(
                "resolver:%p schedule read directly on: %d", this, fd_node->as);
            event_engine_->Run(
                [self = Ref(DEBUG_LOCATION, "CheckSocketsLocked"),
                 fd_node]() mutable {
                  self->OnReadable(fd_node, absl::OkStatus());
                });
          } else {
            // Otherwise register with the poller for readable event.
            GRPC_ARES_RESOLVER_TRACE_LOG("resolver:%p notify read on: %d", this,
                                         fd_node->as);
            fd_node->polled_fd->RegisterForOnReadableLocked(
                [self = Ref(DEBUG_LOCATION, "CheckSocketsLocked"),
                 fd_node](absl::Status status) mutable {
                  self->OnReadable(fd_node, status);
                });
          }
        }
        // Register write_closure if the socket is writable and write_closure
        // has not been registered with this socket.
        if (ARES_GETSOCK_WRITABLE(socks_bitmask, i) &&
            !fd_node->writable_registered) {
          GRPC_ARES_RESOLVER_TRACE_LOG("resolver:%p notify write on: %d", this,
                                       fd_node->as);
          fd_node->writable_registered = true;
          fd_node->polled_fd->RegisterForOnWriteableLocked(
              [self = Ref(DEBUG_LOCATION, "CheckSocketsLocked"),
               fd_node](absl::Status status) mutable {
                self->OnWritable(fd_node, status);
              });
        }
      }
    }
  }
  // Any remaining fds in fd_node_list_ were not returned by ares_getsock()
  // and are therefore no longer in use, so they can be shut down and removed
  // from the list.
  while (!fd_node_list_.empty()) {
    FdNode* fd_node = fd_node_list_.front().get();
    if (!fd_node->already_shutdown) {
      GRPC_ARES_RESOLVER_TRACE_LOG("resolver: %p shutdown fd: %s", this,
                                   fd_node->polled_fd->GetName());
      fd_node->polled_fd->ShutdownLocked(absl::OkStatus());
      fd_node->already_shutdown = true;
    }
    if (!fd_node->readable_registered && !fd_node->writable_registered) {
      GRPC_ARES_RESOLVER_TRACE_LOG("resolver: %p delete fd: %s", this,
                                   fd_node->polled_fd->GetName());
      fd_node_list_.pop_front();
    } else {
      new_list.splice(new_list.end(), fd_node_list_, fd_node_list_.begin());
    }
  }
  fd_node_list_ = std::move(new_list);
}

void AresResolver::MaybeStartTimerLocked() {
  if (ares_backup_poll_alarm_handle_.has_value()) {
    return;
  }
  // Initialize the backup poll alarm
  GRPC_ARES_RESOLVER_TRACE_LOG(
      "request:%p MaybeStartTimerLocked next ares process poll time in %zu ms",
      this, Milliseconds(kAresBackupPollAlarmDuration));
  ares_backup_poll_alarm_handle_ = event_engine_->RunAfter(
      kAresBackupPollAlarmDuration,
      [self = Ref(DEBUG_LOCATION, "MaybeStartTimerLocked")]() {
        self->OnAresBackupPollAlarm();
      });
}

void AresResolver::OnReadable(FdNode* fd_node, absl::Status status) {
  grpc_core::MutexLock lock(&mutex_);
  GPR_ASSERT(fd_node->readable_registered);
  fd_node->readable_registered = false;
  GRPC_ARES_RESOLVER_TRACE_LOG("OnReadable: fd: %d; request: %p; status: %s",
                               fd_node->as, this, status.ToString().c_str());
  if (status.ok() && !shutting_down_) {
    ares_process_fd(channel_, fd_node->as, ARES_SOCKET_BAD);
  } else {
    // If error is not absl::OkStatus() or the resolution was cancelled, it
    // means the fd has been shutdown or timed out. The pending lookups made
    // on this request will be cancelled by the following ares_cancel(). The
    // remaining file descriptors in this request will be cleaned up in the
    // following Work() method.
    ares_cancel(channel_);
  }
  CheckSocketsLocked();
}

void AresResolver::OnWritable(FdNode* fd_node, absl::Status status) {
  grpc_core::MutexLock lock(&mutex_);
  GPR_ASSERT(fd_node->writable_registered);
  fd_node->writable_registered = false;
  GRPC_ARES_RESOLVER_TRACE_LOG("OnWritable: fd: %d; request:%p; status: %s",
                               fd_node->as, this, status.ToString().c_str());
  if (status.ok() && !shutting_down_) {
    ares_process_fd(channel_, ARES_SOCKET_BAD, fd_node->as);
  } else {
    // If error is not absl::OkStatus() or the resolution was cancelled, it
    // means the fd has been shutdown or timed out. The pending lookups made
    // on this request will be cancelled by the following ares_cancel(). The
    // remaining file descriptors in this request will be cleaned up in the
    // following Work() method.
    ares_cancel(channel_);
  }
  CheckSocketsLocked();
}

// In case of non-responsive DNS servers, dropped packets, etc., c-ares has
// intelligent timeout and retry logic, which we can take advantage of by
// polling ares_process_fd on time intervals. Overall, the c-ares library is
// meant to be called into and given a chance to proceed name resolution:
//   a) when fd events happen
//   b) when some time has passed without fd events having happened
// For the latter, we use this backup poller. Also see
// https://github.com/grpc/grpc/pull/17688 description for more details.
void AresResolver::OnAresBackupPollAlarm() {
  grpc_core::MutexLock lock(&mutex_);
  ares_backup_poll_alarm_handle_.reset();
  GRPC_ARES_RESOLVER_TRACE_LOG(
      "request:%p OnAresBackupPollAlarm shutting_down=%d.", this,
      shutting_down_);
  if (!shutting_down_) {
    for (const auto& fd_node : fd_node_list_) {
      if (!fd_node->already_shutdown) {
        GRPC_ARES_RESOLVER_TRACE_LOG(
            "request:%p OnAresBackupPollAlarm; ares_process_fd. fd=%s", this,
            fd_node->polled_fd->GetName());
        ares_socket_t as = fd_node->polled_fd->GetWrappedAresSocketLocked();
        ares_process_fd(channel_, as, as);
      }
    }
    MaybeStartTimerLocked();
    CheckSocketsLocked();
  }
}

void AresResolver::OnHostbynameDoneLocked(void* arg, int status,
                                          int /*timeouts*/,
                                          struct hostent* hostent) {
  std::unique_ptr<HostnameQueryArg> hostname_qa(
      static_cast<HostnameQueryArg*>(arg));
  auto* ares_resolver = hostname_qa->ares_resolver;
  auto nh = ares_resolver->callback_map_.extract(hostname_qa->callback_map_id);
  GPR_ASSERT(!nh.empty());
  GPR_ASSERT(
      absl::holds_alternative<EventEngine::DNSResolver::LookupHostnameCallback>(
          nh.mapped()));
  auto callback = absl::get<EventEngine::DNSResolver::LookupHostnameCallback>(
      std::move(nh.mapped()));
  if (status != ARES_SUCCESS) {
    std::string error_msg =
        absl::StrFormat("address lookup failed for %s: %s",
                        hostname_qa->query_name, ares_strerror(status));
    GRPC_ARES_RESOLVER_TRACE_LOG("resolver:%p OnHostbynameDoneLocked: %s",
                                 ares_resolver, error_msg.c_str());
    ares_resolver->event_engine_->Run(
        [callback = std::move(callback),
         status = AresStatusToAbslStatus(status, error_msg)]() mutable {
          callback(status);
        });
    return;
  }
  GRPC_ARES_RESOLVER_TRACE_LOG(
      "resolver:%p OnHostbynameDoneLocked name=%s ARES_SUCCESS", ares_resolver,
      hostname_qa->query_name.c_str());
  std::vector<EventEngine::ResolvedAddress> result;
  for (size_t i = 0; hostent->h_addr_list[i] != nullptr; i++) {
    switch (hostent->h_addrtype) {
      case AF_INET6: {
        size_t addr_len = sizeof(struct sockaddr_in6);
        struct sockaddr_in6 addr;
        memset(&addr, 0, addr_len);
        memcpy(&addr.sin6_addr, hostent->h_addr_list[i],
               sizeof(struct in6_addr));
        addr.sin6_family = static_cast<unsigned char>(hostent->h_addrtype);
        addr.sin6_port = htons(hostname_qa->port);
        result.emplace_back(reinterpret_cast<const sockaddr*>(&addr), addr_len);
        char output[INET6_ADDRSTRLEN];
        ares_inet_ntop(AF_INET6, &addr.sin6_addr, output, INET6_ADDRSTRLEN);
        GRPC_ARES_RESOLVER_TRACE_LOG(
            "resolver:%p c-ares resolver gets a AF_INET6 result: \n"
            "  addr: %s\n  port: %d\n  sin6_scope_id: %d\n",
            ares_resolver, output, hostname_qa->port, addr.sin6_scope_id);
        break;
      }
      case AF_INET: {
        size_t addr_len = sizeof(struct sockaddr_in);
        struct sockaddr_in addr;
        memset(&addr, 0, addr_len);
        memcpy(&addr.sin_addr, hostent->h_addr_list[i], sizeof(struct in_addr));
        addr.sin_family = static_cast<unsigned char>(hostent->h_addrtype);
        addr.sin_port = htons(hostname_qa->port);
        result.emplace_back(reinterpret_cast<const sockaddr*>(&addr), addr_len);
        char output[INET_ADDRSTRLEN];
        ares_inet_ntop(AF_INET, &addr.sin_addr, output, INET_ADDRSTRLEN);
        GRPC_ARES_RESOLVER_TRACE_LOG(
            "resolver:%p c-ares resolver gets a AF_INET result: \n"
            "  addr: %s\n  port: %d\n",
            ares_resolver, output, hostname_qa->port);
        break;
      }
    }
  }
  ares_resolver->event_engine_->Run(
      [callback = std::move(callback), result = std::move(result)]() mutable {
        callback(std::move(result));
      });
}

void AresResolver::OnSRVQueryDoneLocked(void* arg, int status, int /*timeouts*/,
                                        unsigned char* abuf, int alen) {
  std::unique_ptr<QueryArg> qa(static_cast<QueryArg*>(arg));
  auto* ares_resolver = qa->ares_resolver;
  auto nh = ares_resolver->callback_map_.extract(qa->callback_map_id);
  GPR_ASSERT(!nh.empty());
  GPR_ASSERT(
      absl::holds_alternative<EventEngine::DNSResolver::LookupSRVCallback>(
          nh.mapped()));
  auto callback = absl::get<EventEngine::DNSResolver::LookupSRVCallback>(
      std::move(nh.mapped()));
  auto fail = [&](absl::string_view prefix) {
    std::string error_message = absl::StrFormat(
        "%s for %s: %s", prefix, qa->query_name, ares_strerror(status));
    GRPC_ARES_RESOLVER_TRACE_LOG("OnSRVQueryDoneLocked: %s",
                                 error_message.c_str());
    ares_resolver->event_engine_->Run(
        [callback = std::move(callback),
         status = AresStatusToAbslStatus(status, error_message)]() mutable {
          callback(status);
        });
  };
  if (status != ARES_SUCCESS) {
    fail("SRV lookup failed");
    return;
  }
  GRPC_ARES_RESOLVER_TRACE_LOG(
      "resolver:%p OnSRVQueryDoneLocked name=%s ARES_SUCCESS", ares_resolver,
      qa->query_name.c_str());
  struct ares_srv_reply* reply = nullptr;
  status = ares_parse_srv_reply(abuf, alen, &reply);
  GRPC_ARES_RESOLVER_TRACE_LOG("resolver:%p ares_parse_srv_reply: %d",
                               ares_resolver, status);
  if (status != ARES_SUCCESS) {
    fail("Failed to parse SRV reply");
    return;
  }
  std::vector<EventEngine::DNSResolver::SRVRecord> result;
  for (struct ares_srv_reply* srv_it = reply; srv_it != nullptr;
       srv_it = srv_it->next) {
    EventEngine::DNSResolver::SRVRecord record;
    record.host = srv_it->host;
    record.port = srv_it->port;
    record.priority = srv_it->priority;
    record.weight = srv_it->weight;
    result.push_back(std::move(record));
  }
  if (reply != nullptr) {
    ares_free_data(reply);
  }
  ares_resolver->event_engine_->Run(
      [callback = std::move(callback), result = std::move(result)]() mutable {
        callback(std::move(result));
      });
}

void AresResolver::OnTXTDoneLocked(void* arg, int status, int /*timeouts*/,
                                   unsigned char* buf, int len) {
  std::unique_ptr<QueryArg> qa(static_cast<QueryArg*>(arg));
  auto* ares_resolver = qa->ares_resolver;
  auto nh = ares_resolver->callback_map_.extract(qa->callback_map_id);
  GPR_ASSERT(!nh.empty());
  GPR_ASSERT(
      absl::holds_alternative<EventEngine::DNSResolver::LookupTXTCallback>(
          nh.mapped()));
  auto callback = absl::get<EventEngine::DNSResolver::LookupTXTCallback>(
      std::move(nh.mapped()));
  auto fail = [&](absl::string_view prefix) {
    std::string error_message = absl::StrFormat(
        "%s for %s: %s", prefix, qa->query_name, ares_strerror(status));
    GRPC_ARES_RESOLVER_TRACE_LOG("resolver:%p OnTXTDoneLocked: %s",
                                 ares_resolver, error_message.c_str());
    ares_resolver->event_engine_->Run(
        [callback = std::move(callback),
         status = AresStatusToAbslStatus(status, error_message)]() mutable {
          callback(status);
        });
  };
  if (status != ARES_SUCCESS) {
    fail("TXT lookup failed");
    return;
  }
  GRPC_ARES_RESOLVER_TRACE_LOG(
      "resolver:%p OnTXTDoneLocked name=%s ARES_SUCCESS", ares_resolver,
      qa->query_name.c_str());
  struct ares_txt_ext* reply = nullptr;
  status = ares_parse_txt_reply_ext(buf, len, &reply);
  if (status != ARES_SUCCESS) {
    fail("Failed to parse TXT result");
    return;
  }
  std::vector<std::string> result;
  for (struct ares_txt_ext* part = reply; part != nullptr; part = part->next) {
    if (part->record_start) {
      result.emplace_back(reinterpret_cast<char*>(part->txt), part->length);
    } else {
      absl::StrAppend(
          &result.back(),
          std::string(reinterpret_cast<char*>(part->txt), part->length));
    }
  }
  GRPC_ARES_RESOLVER_TRACE_LOG("resolver:%p Got %zu TXT records", ares_resolver,
                               result.size());
  if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_ares_resolver)) {
    for (const auto& record : result) {
      gpr_log(GPR_INFO, "%s", record.c_str());
    }
  }
  // Clean up.
  ares_free_data(reply);
  ares_resolver->event_engine_->Run(
      [callback = std::move(callback), result = std::move(result)]() mutable {
        callback(std::move(result));
      });
}

}  // namespace experimental
}  // namespace grpc_event_engine

void noop_inject_channel_config(ares_channel* /*channel*/) {}

void (*event_engine_grpc_ares_test_only_inject_config)(ares_channel* channel) =
    noop_inject_channel_config;

bool g_event_engine_grpc_ares_test_only_force_tcp = false;

#endif  // GRPC_ARES == 1

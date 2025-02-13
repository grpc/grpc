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
#include "src/core/lib/event_engine/ares_resolver.h"

#include <grpc/support/port_platform.h>

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

#include <address_sorting/address_sorting.h>
#include <ares.h>

#if ARES_VERSION >= 0x011200
// c-ares 1.18.0 or later starts to provide ares_nameser.h as a public header.
#include <ares_nameser.h>
#else
#include "src/core/lib/event_engine/nameser.h"  // IWYU pragma: keep
#endif

#include <grpc/event_engine/event_engine.h>
#include <string.h>

#include <algorithm>
#include <chrono>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

#include "absl/functional/any_invocable.h"
#include "absl/hash/hash.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "src/core/config/config_vars.h"
#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/event_engine/grpc_polled_fd.h"
#include "src/core/lib/event_engine/time_util.h"
#include "src/core/lib/iomgr/resolved_address.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/host_port.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/ref_counted_ptr.h"
#ifdef GRPC_POSIX_SOCKET_ARES_EV_DRIVER
#include "src/core/lib/event_engine/posix_engine/tcp_socket_utils.h"
#endif

namespace grpc_event_engine::experimental {

namespace {

// A hard limit on the number of records (A/AAAA or SRV) we may get from a
// single response. This is to be defensive to prevent a bad DNS response from
// OOMing the process.
constexpr int kMaxRecordSize = 65536;

absl::Status AresStatusToAbslStatus(int status, absl::string_view error_msg) {
  switch (status) {
    case ARES_ECANCELLED:
      return absl::CancelledError(error_msg);
    case ARES_ENOTIMP:
      return absl::UnimplementedError(error_msg);
    case ARES_ENOTFOUND:
      return absl::NotFoundError(error_msg);
    case ARES_ECONNREFUSED:
      return absl::UnavailableError(error_msg);
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
  GRPC_TRACE_LOG(cares_resolver, INFO)
      << "(EventEngine c-ares resolver) Using DNS server " << dns_server;
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

std::vector<EventEngine::ResolvedAddress> SortAddresses(
    const std::vector<EventEngine::ResolvedAddress>& addresses) {
  address_sorting_sortable* sortables = static_cast<address_sorting_sortable*>(
      gpr_zalloc(sizeof(address_sorting_sortable) * addresses.size()));
  for (size_t i = 0; i < addresses.size(); i++) {
    sortables[i].user_data =
        const_cast<EventEngine::ResolvedAddress*>(&addresses[i]);
    memcpy(&sortables[i].dest_addr.addr, addresses[i].address(),
           addresses[i].size());
    sortables[i].dest_addr.len = addresses[i].size();
  }
  address_sorting_rfc_6724_sort(sortables, addresses.size());
  std::vector<EventEngine::ResolvedAddress> sorted_addresses;
  sorted_addresses.reserve(addresses.size());
  for (size_t i = 0; i < addresses.size(); ++i) {
    sorted_addresses.emplace_back(
        *static_cast<EventEngine::ResolvedAddress*>(sortables[i].user_data));
  }
  gpr_free(sortables);
  return sorted_addresses;
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
  int pending_requests;
  absl::Status error_status;
  std::vector<EventEngine::ResolvedAddress> result;
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
    LOG(ERROR) << "ares_init_options failed, status: " << status;
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

AresResolver::AresResolver(
    std::unique_ptr<GrpcPolledFdFactory> polled_fd_factory,
    std::shared_ptr<EventEngine> event_engine, ares_channel channel)
    : RefCountedDNSResolverInterface(
          GRPC_TRACE_FLAG_ENABLED(cares_resolver) ? "AresResolver" : nullptr),
      channel_(channel),
      polled_fd_factory_(std::move(polled_fd_factory)),
      event_engine_(std::move(event_engine)) {
  polled_fd_factory_->Initialize(&mutex_, event_engine_.get());
}

AresResolver::~AresResolver() {
  CHECK(fd_node_list_.empty());
  CHECK(callback_map_.empty());
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
        GRPC_TRACE_LOG(cares_resolver, INFO)
            << "(EventEngine c-ares resolver) resolver: " << this
            << " shutdown fd: " << fd_node->polled_fd->GetName();
        CHECK(fd_node->polled_fd->ShutdownLocked(
            absl::CancelledError("AresResolver::Orphan")));
        fd_node->already_shutdown = true;
      }
    }
  }
  Unref(DEBUG_LOCATION, "Orphan");
}

void AresResolver::LookupHostname(
    EventEngine::DNSResolver::LookupHostnameCallback callback,
    absl::string_view name, absl::string_view default_port) {
  absl::string_view host;
  absl::string_view port_string;
  if (!grpc_core::SplitHostPort(name, &host, &port_string)) {
    event_engine_->Run(
        [callback = std::move(callback),
         status = absl::InvalidArgumentError(absl::StrCat(
             "Unparsable name: ", name))]() mutable { callback(status); });
    return;
  }
  if (host.empty()) {
    event_engine_->Run([callback = std::move(callback),
                        status = absl::InvalidArgumentError(absl::StrCat(
                            "host must not be empty in name: ",
                            name))]() mutable { callback(status); });
    return;
  }
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
    // Note that using AF_UNSPEC for both IPv6 and IPv4 queries does not work in
    // all cases, e.g. for localhost:<> it only gets back the IPv6 result (i.e.
    // ::1).
    resolver_arg->pending_requests = 2;
    ares_gethostbyname(channel_, std::string(host).c_str(), AF_INET,
                       &AresResolver::OnHostbynameDoneLocked, resolver_arg);
    ares_gethostbyname(channel_, std::string(host).c_str(), AF_INET6,
                       &AresResolver::OnHostbynameDoneLocked, resolver_arg);
  } else {
    resolver_arg->pending_requests = 1;
    ares_gethostbyname(channel_, std::string(host).c_str(), AF_INET,
                       &AresResolver::OnHostbynameDoneLocked, resolver_arg);
  }
  CheckSocketsLocked();
  MaybeStartTimerLocked();
}

void AresResolver::LookupSRV(
    EventEngine::DNSResolver::LookupSRVCallback callback,
    absl::string_view name) {
  absl::string_view host;
  absl::string_view port;
  if (!grpc_core::SplitHostPort(name, &host, &port)) {
    event_engine_->Run(
        [callback = std::move(callback),
         status = absl::InvalidArgumentError(absl::StrCat(
             "Unparsable name: ", name))]() mutable { callback(status); });
    return;
  }
  if (host.empty()) {
    event_engine_->Run([callback = std::move(callback),
                        status = absl::InvalidArgumentError(absl::StrCat(
                            "host must not be empty in name: ",
                            name))]() mutable { callback(status); });
    return;
  }
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
    EventEngine::DNSResolver::LookupTXTCallback callback,
    absl::string_view name) {
  absl::string_view host;
  absl::string_view port;
  if (!grpc_core::SplitHostPort(name, &host, &port)) {
    event_engine_->Run(
        [callback = std::move(callback),
         status = absl::InvalidArgumentError(absl::StrCat(
             "Unparsable name: ", name))]() mutable { callback(status); });
    return;
  }
  if (host.empty()) {
    event_engine_->Run([callback = std::move(callback),
                        status = absl::InvalidArgumentError(absl::StrCat(
                            "host must not be empty in name: ",
                            name))]() mutable { callback(status); });
    return;
  }
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

void AresResolver::CheckSocketsLocked() {
  FdNodeList new_list;
  if (!shutting_down_) {
    ares_socket_t socks[ARES_GETSOCK_MAXNUM] = {};
    int socks_bitmask = ares_getsock(channel_, socks, ARES_GETSOCK_MAXNUM);
    for (size_t i = 0; i < ARES_GETSOCK_MAXNUM; i++) {
      if (ARES_GETSOCK_READABLE(socks_bitmask, i) ||
          ARES_GETSOCK_WRITABLE(socks_bitmask, i)) {
        auto iter = std::find_if(
            fd_node_list_.begin(), fd_node_list_.end(),
            [sock = socks[i]](const auto& node) { return node->as == sock; });
        if (iter == fd_node_list_.end()) {
          GRPC_TRACE_LOG(cares_resolver, INFO)
              << "(EventEngine c-ares resolver) resolver:" << this
              << " new fd: " << socks[i];
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
            GRPC_TRACE_LOG(cares_resolver, INFO)
                << "(EventEngine c-ares resolver) resolver:" << this
                << " schedule read directly on: " << fd_node->as;
            event_engine_->Run(
                [self = Ref(DEBUG_LOCATION, "CheckSocketsLocked"),
                 fd_node]() mutable {
                  static_cast<AresResolver*>(self.get())
                      ->OnReadable(fd_node, absl::OkStatus());
                });
          } else {
            // Otherwise register with the poller for readable event.
            GRPC_TRACE_LOG(cares_resolver, INFO)
                << "(EventEngine c-ares resolver) resolver:" << this
                << " notify read on: " << fd_node->as;
            fd_node->polled_fd->RegisterForOnReadableLocked(
                [self = Ref(DEBUG_LOCATION, "CheckSocketsLocked"),
                 fd_node](absl::Status status) mutable {
                  static_cast<AresResolver*>(self.get())
                      ->OnReadable(fd_node, status);
                });
          }
        }
        // Register write_closure if the socket is writable and write_closure
        // has not been registered with this socket.
        if (ARES_GETSOCK_WRITABLE(socks_bitmask, i) &&
            !fd_node->writable_registered) {
          GRPC_TRACE_LOG(cares_resolver, INFO)
              << "(EventEngine c-ares resolver) resolver:" << this
              << " notify write on: " << fd_node->as;
          fd_node->writable_registered = true;
          fd_node->polled_fd->RegisterForOnWriteableLocked(
              [self = Ref(DEBUG_LOCATION, "CheckSocketsLocked"),
               fd_node](absl::Status status) mutable {
                static_cast<AresResolver*>(self.get())
                    ->OnWritable(fd_node, status);
              });
        }
      }
    }
  }
  // Any remaining fds in fd_node_list_ were not returned by ares_getsock()
  // and are therefore no longer in use, so they can be shut down and removed
  // from the list.
  // TODO(yijiem): Since we are keeping the underlying socket opened for both
  // Posix and Windows, it might be reasonable to also keep the FdNodes alive
  // till the end. But we need to change the state management of FdNodes in this
  // file. This may simplify the code a bit.
  while (!fd_node_list_.empty()) {
    FdNode* fd_node = fd_node_list_.front().get();
    if (!fd_node->already_shutdown) {
      GRPC_TRACE_LOG(cares_resolver, INFO)
          << "(EventEngine c-ares resolver) resolver: " << this
          << " shutdown fd: " << fd_node->polled_fd->GetName();
      fd_node->already_shutdown =
          fd_node->polled_fd->ShutdownLocked(absl::OkStatus());
    }
    if (!fd_node->readable_registered && !fd_node->writable_registered) {
      GRPC_TRACE_LOG(cares_resolver, INFO)
          << "(EventEngine c-ares resolver) resolver: " << this
          << " delete fd: " << fd_node->polled_fd->GetName();
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
  GRPC_TRACE_LOG(cares_resolver, INFO)
      << "(EventEngine c-ares resolver) request:" << this
      << " MaybeStartTimerLocked next ares process poll time in "
      << Milliseconds(kAresBackupPollAlarmDuration) << " ms";
  ares_backup_poll_alarm_handle_ = event_engine_->RunAfter(
      kAresBackupPollAlarmDuration,
      [self = Ref(DEBUG_LOCATION, "MaybeStartTimerLocked")]() {
        static_cast<AresResolver*>(self.get())->OnAresBackupPollAlarm();
      });
}

void AresResolver::OnReadable(FdNode* fd_node, absl::Status status) {
  grpc_core::MutexLock lock(&mutex_);
  CHECK(fd_node->readable_registered);
  fd_node->readable_registered = false;
  GRPC_TRACE_LOG(cares_resolver, INFO)
      << "(EventEngine c-ares resolver) OnReadable: fd: " << fd_node->as
      << "; request: " << this << "; status: " << status;
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
  CHECK(fd_node->writable_registered);
  fd_node->writable_registered = false;
  GRPC_TRACE_LOG(cares_resolver, INFO)
      << "(EventEngine c-ares resolver) OnWritable: fd: " << fd_node->as
      << "; request:" << this << "; status: " << status;
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
  GRPC_TRACE_LOG(cares_resolver, INFO)
      << "(EventEngine c-ares resolver) request:" << this
      << " OnAresBackupPollAlarm shutting_down=" << shutting_down_;
  if (!shutting_down_) {
    for (const auto& fd_node : fd_node_list_) {
      if (!fd_node->already_shutdown) {
        GRPC_TRACE_LOG(cares_resolver, INFO)
            << "(EventEngine c-ares resolver) request:" << this
            << " OnAresBackupPollAlarm; ares_process_fd. fd="
            << fd_node->polled_fd->GetName();
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
  auto* hostname_qa = static_cast<HostnameQueryArg*>(arg);
  CHECK_GT(hostname_qa->pending_requests--, 0);
  auto* ares_resolver = hostname_qa->ares_resolver;
  if (status != ARES_SUCCESS) {
    std::string error_msg =
        absl::StrFormat("address lookup failed for %s: %s",
                        hostname_qa->query_name, ares_strerror(status));
    GRPC_TRACE_LOG(cares_resolver, INFO)
        << "(EventEngine c-ares resolver) resolver:" << ares_resolver
        << " OnHostbynameDoneLocked: " << error_msg;
    hostname_qa->error_status = AresStatusToAbslStatus(status, error_msg);
  } else {
    GRPC_TRACE_LOG(cares_resolver, INFO)
        << "(EventEngine c-ares resolver) resolver:" << ares_resolver
        << " OnHostbynameDoneLocked name=" << hostname_qa->query_name
        << " ARES_SUCCESS";
    for (size_t i = 0; hostent->h_addr_list[i] != nullptr; i++) {
      if (hostname_qa->result.size() == kMaxRecordSize) {
        LOG(ERROR) << "A/AAAA response exceeds maximum record size of 65536";
        break;
      }
      switch (hostent->h_addrtype) {
        case AF_INET6: {
          size_t addr_len = sizeof(struct sockaddr_in6);
          struct sockaddr_in6 addr;
          memset(&addr, 0, addr_len);
          memcpy(&addr.sin6_addr, hostent->h_addr_list[i],
                 sizeof(struct in6_addr));
          addr.sin6_family = static_cast<unsigned char>(hostent->h_addrtype);
          addr.sin6_port = htons(hostname_qa->port);
          hostname_qa->result.emplace_back(
              reinterpret_cast<const sockaddr*>(&addr), addr_len);
          char output[INET6_ADDRSTRLEN];
          ares_inet_ntop(AF_INET6, &addr.sin6_addr, output, INET6_ADDRSTRLEN);
          GRPC_TRACE_LOG(cares_resolver, INFO)
              << "(EventEngine c-ares resolver) resolver:" << ares_resolver
              << " c-ares resolver gets a AF_INET6 result: \n  addr: " << output
              << "\n  port: " << hostname_qa->port
              << "\n  sin6_scope_id: " << addr.sin6_scope_id;
          break;
        }
        case AF_INET: {
          size_t addr_len = sizeof(struct sockaddr_in);
          struct sockaddr_in addr;
          memset(&addr, 0, addr_len);
          memcpy(&addr.sin_addr, hostent->h_addr_list[i],
                 sizeof(struct in_addr));
          addr.sin_family = static_cast<unsigned char>(hostent->h_addrtype);
          addr.sin_port = htons(hostname_qa->port);
          hostname_qa->result.emplace_back(
              reinterpret_cast<const sockaddr*>(&addr), addr_len);
          char output[INET_ADDRSTRLEN];
          ares_inet_ntop(AF_INET, &addr.sin_addr, output, INET_ADDRSTRLEN);
          GRPC_TRACE_LOG(cares_resolver, INFO)
              << "(EventEngine c-ares resolver) resolver:" << ares_resolver
              << " c-ares resolver gets a AF_INET result: \n  addr: " << output
              << "\n  port: " << hostname_qa->port;
          break;
        }
        default:
          grpc_core::Crash(
              absl::StrFormat("resolver:%p Received invalid type of address %d",
                              ares_resolver, hostent->h_addrtype));
      }
    }
  }
  if (hostname_qa->pending_requests == 0) {
    auto nh =
        ares_resolver->callback_map_.extract(hostname_qa->callback_map_id);
    CHECK(!nh.empty());
    CHECK(std::holds_alternative<
          EventEngine::DNSResolver::LookupHostnameCallback>(nh.mapped()));
    auto callback = std::get<EventEngine::DNSResolver::LookupHostnameCallback>(
        std::move(nh.mapped()));
    if (!hostname_qa->result.empty() || hostname_qa->error_status.ok()) {
      ares_resolver->event_engine_->Run(
          [callback = std::move(callback),
           result = SortAddresses(hostname_qa->result)]() mutable {
            callback(std::move(result));
          });
    } else {
      ares_resolver->event_engine_->Run(
          [callback = std::move(callback),
           result = std::move(hostname_qa->error_status)]() mutable {
            callback(std::move(result));
          });
    }
    delete hostname_qa;
  }
}

void AresResolver::OnSRVQueryDoneLocked(void* arg, int status, int /*timeouts*/,
                                        unsigned char* abuf, int alen) {
  std::unique_ptr<QueryArg> qa(static_cast<QueryArg*>(arg));
  auto* ares_resolver = qa->ares_resolver;
  auto nh = ares_resolver->callback_map_.extract(qa->callback_map_id);
  CHECK(!nh.empty());
  CHECK(std::holds_alternative<EventEngine::DNSResolver::LookupSRVCallback>(
      nh.mapped()));
  auto callback = std::get<EventEngine::DNSResolver::LookupSRVCallback>(
      std::move(nh.mapped()));
  auto fail = [&](absl::string_view prefix) {
    std::string error_message = absl::StrFormat(
        "%s for %s: %s", prefix, qa->query_name, ares_strerror(status));
    GRPC_TRACE_LOG(cares_resolver, INFO)
        << "(EventEngine c-ares resolver) OnSRVQueryDoneLocked: "
        << error_message;
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
  GRPC_TRACE_LOG(cares_resolver, INFO)
      << "(EventEngine c-ares resolver) resolver:" << ares_resolver
      << " OnSRVQueryDoneLocked name=" << qa->query_name << " ARES_SUCCESS";
  struct ares_srv_reply* reply = nullptr;
  status = ares_parse_srv_reply(abuf, alen, &reply);
  GRPC_TRACE_LOG(cares_resolver, INFO)
      << "(EventEngine c-ares resolver) resolver:" << ares_resolver
      << " ares_parse_srv_reply: " << status;
  if (status != ARES_SUCCESS) {
    fail("Failed to parse SRV reply");
    return;
  }
  std::vector<EventEngine::DNSResolver::SRVRecord> result;
  for (struct ares_srv_reply* srv_it = reply; srv_it != nullptr;
       srv_it = srv_it->next) {
    if (result.size() == kMaxRecordSize) {
      LOG(ERROR) << "SRV response exceeds maximum record size of 65536";
      break;
    }
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
  CHECK(!nh.empty());
  CHECK(std::holds_alternative<EventEngine::DNSResolver::LookupTXTCallback>(
      nh.mapped()));
  auto callback = std::get<EventEngine::DNSResolver::LookupTXTCallback>(
      std::move(nh.mapped()));
  auto fail = [&](absl::string_view prefix) {
    std::string error_message = absl::StrFormat(
        "%s for %s: %s", prefix, qa->query_name, ares_strerror(status));
    GRPC_TRACE_LOG(cares_resolver, INFO)
        << "(EventEngine c-ares resolver) resolver:" << ares_resolver
        << " OnTXTDoneLocked: " << error_message;
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
  GRPC_TRACE_LOG(cares_resolver, INFO)
      << "(EventEngine c-ares resolver) resolver:" << ares_resolver
      << " OnTXTDoneLocked name=" << qa->query_name << " ARES_SUCCESS";
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
  GRPC_TRACE_LOG(cares_resolver, INFO)
      << "(EventEngine c-ares resolver) resolver:" << ares_resolver << " Got "
      << result.size() << " TXT records";
  if (GRPC_TRACE_FLAG_ENABLED(cares_resolver)) {
    for (const auto& record : result) {
      LOG(INFO) << record;
    }
  }
  // Clean up.
  ares_free_data(reply);
  ares_resolver->event_engine_->Run(
      [callback = std::move(callback), result = std::move(result)]() mutable {
        callback(std::move(result));
      });
}

}  // namespace grpc_event_engine::experimental

void noop_inject_channel_config(ares_channel* /*channel*/) {}

void (*event_engine_grpc_ares_test_only_inject_config)(ares_channel* channel) =
    noop_inject_channel_config;

bool g_event_engine_grpc_ares_test_only_force_tcp = false;

bool ShouldUseAresDnsResolver() {
#if defined(GRPC_POSIX_SOCKET_ARES_EV_DRIVER) || \
    defined(GRPC_WINDOWS_SOCKET_ARES_EV_DRIVER)
  auto resolver_env = grpc_core::ConfigVars::Get().DnsResolver();
  return resolver_env.empty() || absl::EqualsIgnoreCase(resolver_env, "ares");
#else   // defined(GRPC_POSIX_SOCKET_ARES_EV_DRIVER) ||
        // defined(GRPC_WINDOWS_SOCKET_ARES_EV_DRIVER)
  return false;
#endif  // defined(GRPC_POSIX_SOCKET_ARES_EV_DRIVER) ||
        // defined(GRPC_WINDOWS_SOCKET_ARES_EV_DRIVER)
}

absl::Status AresInit() {
  if (ShouldUseAresDnsResolver()) {
    // ares_library_init and ares_library_cleanup are currently no-op except
    // under Windows. Calling them may cause race conditions when other parts of
    // the binary calls these functions concurrently.
#ifdef GPR_WINDOWS
    int status = ares_library_init(ARES_LIB_INIT_ALL);
    if (status != ARES_SUCCESS) {
      return GRPC_ERROR_CREATE(
          absl::StrCat("ares_library_init failed: ", ares_strerror(status)));
    }
#endif  // GPR_WINDOWS
  }
  return absl::OkStatus();
}
void AresShutdown() {
  if (ShouldUseAresDnsResolver()) {
    // ares_library_init and ares_library_cleanup are currently no-op except
    // under Windows. Calling them may cause race conditions when other parts of
    // the binary calls these functions concurrently.
#ifdef GPR_WINDOWS
    ares_library_cleanup();
#endif  // GPR_WINDOWS
  }
}

#else  // GRPC_ARES == 1

bool ShouldUseAresDnsResolver() { return false; }
absl::Status AresInit() { return absl::OkStatus(); }
void AresShutdown() {}

#endif  // GRPC_ARES == 1

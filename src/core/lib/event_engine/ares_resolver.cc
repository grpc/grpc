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

#include "src/core/lib/iomgr/port.h"

#if GRPC_ARES == 1

#include <string.h>

#include <algorithm>
#include <chrono>
#include <initializer_list>
#include <memory>
#include <type_traits>
#include <utility>

#include <address_sorting/address_sorting.h>

#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/types/optional.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/event_engine/grpc_polled_fd.h"
#include "src/core/lib/event_engine/nameser.h"  // IWYU pragma: keep
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/event_engine/time_util.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/examine_stack.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/iomgr/resolved_address.h"
#include "src/core/lib/iomgr/sockaddr.h"
#ifdef GRPC_POSIX_SOCKET_ARES_EV_DRIVER
#include "src/core/lib/event_engine/posix_engine/tcp_socket_utils.h"
#elif defined(GRPC_WINDOWS_SOCKET_ARES_EV_DRIVER)
#endif

namespace grpc_event_engine {
namespace experimental {

grpc_core::TraceFlag grpc_trace_ares_resolver(false,
                                              "event_engine_ares_resolver");

grpc_core::TraceFlag grpc_trace_ares_resolver_address_sorting(
    false, "ares_resolver_address_sorting");

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

template <typename T>
bool IsDefaultStatusOr(const absl::StatusOr<T>& status_or) {
  return !status_or.ok() && absl::IsUnknown(status_or.status()) &&
         status_or.status().message().empty();
}

EventEngine::Duration calculate_next_ares_backup_poll_alarm_duration() {
  // An alternative here could be to use ares_timeout to try to be more
  // accurate, but that would require using "struct timeval"'s, which just
  // makes things a bit more complicated. So just poll every second, as
  // suggested by the c-ares code comments.
  return std::chrono::seconds(1);
}

struct QueryArg {
  QueryArg(AresResolver* ar, int id, absl::string_view name)
      : ares_resolver(ar), callback_map_id(id), qname(name) {}
  AresResolver* ares_resolver;
  int callback_map_id;
  std::string qname;
};

struct HostnameQueryArg : public QueryArg {
  HostnameQueryArg(AresResolver* ar, int id, absl::string_view name, int p,
                   int family)
      : QueryArg(ar, id, name),
        port(p),
        type(family == AF_INET ? "A" : "AAAA") {}
  int port;
  const char* type;
};

}  // namespace

AresResolver::AresResolver(
    std::unique_ptr<GrpcPolledFdFactory> polled_fd_factory,
    EventEngine* event_engine)
    : grpc_core::InternallyRefCounted<AresResolver>(
          GRPC_TRACE_FLAG_ENABLED(grpc_trace_ares_resolver) ? "AresResolver"
                                                            : nullptr),
      polled_fd_factory_(std::move(polled_fd_factory)),
      event_engine_(event_engine) {}

AresResolver::~AresResolver() {
  GPR_ASSERT(fd_node_list_.empty());
  GPR_ASSERT(callback_map_.empty());
  if (initialized_) {
    ares_destroy(channel_);
  }
}

absl::Status AresResolver::Initialize(absl::string_view dns_server) {
  if (initialized_) {
    return absl::OkStatus();
  }
  ares_options opts = {};
  opts.flags |= ARES_FLAG_STAYOPEN;
  int status = ares_init_options(&channel_, &opts, ARES_OPT_FLAGS);
  if (status != ARES_SUCCESS) {
    gpr_log(GPR_ERROR, "ares_init_options failed, status: %d", status);
    return AresStatusToAbslStatus(
        status, absl::StrCat("Failed to init ares channel. c-ares error: ",
                             ares_strerror(status)));
  }
  if (!dns_server.empty()) {
    absl::Status status = SetRequestDNSServer(dns_server);
    if (!status.ok()) {
      return status;
    }
  }
  initialized_ = true;
  return absl::OkStatus();
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
        GRPC_ARES_RESOLVER_TRACE_LOG("request: %p shutdown fd: %s", this,
                                     fd_node->polled_fd->GetName());
        fd_node->polled_fd->ShutdownLocked(
            absl::CancelledError("AresResolver::Orphan"));
        fd_node->already_shutdown = true;
      }
    }
  }
  Unref(DEBUG_LOCATION, "Orphan");
}

absl::Status AresResolver::SetRequestDNSServer(absl::string_view dns_server) {
  GRPC_ARES_RESOLVER_TRACE_LOG("request:%p Using DNS server %s", this,
                               dns_server.data());
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
  int status = ares_set_servers_ports(channel_, &dns_server_addr);
  if (status != ARES_SUCCESS) {
    return AresStatusToAbslStatus(
        status, absl::StrCat("c-ares status is not ARES_SUCCESS: ",
                             ares_strerror(status)));
  }
  return absl::OkStatus();
}

void AresResolver::WorkLocked() {
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
          new_list.emplace_back(new FdNode(
              socks[i], polled_fd_factory_->NewGrpcPolledFdLocked(socks[i])));
          GRPC_ARES_RESOLVER_TRACE_LOG("request:%p new fd: %d", this, socks[i]);
        } else {
          new_list.splice(new_list.end(), fd_node_list_, iter);
        }
        FdNode* fd_node = new_list.back().get();
        // Register read_closure if the socket is readable and read_closure
        // has not been registered with this socket.
        if (ARES_GETSOCK_READABLE(socks_bitmask, i) &&
            !fd_node->readable_registered) {
          GRPC_ARES_RESOLVER_TRACE_LOG("request:%p notify read on: %d", this,
                                       fd_node->as);
          fd_node->readable_registered = true;
          fd_node->polled_fd->RegisterForOnReadableLocked(
              [self = Ref(DEBUG_LOCATION, "Work"),
               fd_node](absl::Status status) mutable {
                self->OnReadable(fd_node, status);
              });
        }
        // Register write_closure if the socket is writable and write_closure
        // has not been registered with this socket.
        if (ARES_GETSOCK_WRITABLE(socks_bitmask, i) &&
            !fd_node->writable_registered) {
          GRPC_ARES_RESOLVER_TRACE_LOG("request:%p notify write on: %d", this,
                                       fd_node->as);
          fd_node->writable_registered = true;
          fd_node->polled_fd->RegisterForOnWriteableLocked(
              [self = Ref(DEBUG_LOCATION, "Work"),
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
      GRPC_ARES_RESOLVER_TRACE_LOG("request: %p shutdown fd: %s", this,
                                   fd_node->polled_fd->GetName());
      fd_node->polled_fd->ShutdownLocked(absl::OkStatus());
      fd_node->already_shutdown = true;
    }
    if (!fd_node->readable_registered && !fd_node->writable_registered) {
      GRPC_ARES_RESOLVER_TRACE_LOG("request: %p delete fd: %s", this,
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
  EventEngine::Duration next_ares_backup_poll_alarm_duration =
      calculate_next_ares_backup_poll_alarm_duration();
  GRPC_ARES_RESOLVER_TRACE_LOG(
      "request:%p StartTimers next ares process poll time in %zu ms", this,
      Milliseconds(next_ares_backup_poll_alarm_duration));

  ares_backup_poll_alarm_handle_ = event_engine_->RunAfter(
      next_ares_backup_poll_alarm_duration,
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
    do {
      ares_process_fd(channel_, fd_node->as, ARES_SOCKET_BAD);
    } while (fd_node->polled_fd->IsFdStillReadableLocked());
  } else {
    // If error is not absl::OkStatus() or the resolution was cancelled, it
    // means the fd has been shutdown or timed out. The pending lookups made
    // on this request will be cancelled by the following ares_cancel(). The
    // remaining file descriptors in this request will be cleaned up in the
    // following Work() method.
    ares_cancel(channel_);
  }
  WorkLocked();
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
  WorkLocked();
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
    EventEngine::Duration next_ares_backup_poll_alarm_duration =
        calculate_next_ares_backup_poll_alarm_duration();
    ares_backup_poll_alarm_handle_ = event_engine_->RunAfter(
        next_ares_backup_poll_alarm_duration,
        [self = Ref(DEBUG_LOCATION, "OnAresBackupPollAlarm")]() {
          self->OnAresBackupPollAlarm();
        });
    WorkLocked();
  }
}

void AresResolver::LookupHostname(
    absl::string_view name, int port, int family,
    absl::AnyInvocable<void(absl::StatusOr<AresResolver::Result>)> callback) {
  grpc_core::MutexLock lock(&mutex_);
  GPR_ASSERT(initialized_);
  callback_map_.emplace(++id_, std::move(callback));
  auto* resolver_arg = new HostnameQueryArg(this, id_, name, port, family);
  ares_gethostbyname(channel_, std::string(name).c_str(), family,
                     &AresResolver::OnHostbynameDoneLocked,
                     static_cast<void*>(resolver_arg));
  WorkLocked();
  MaybeStartTimerLocked();
}

void AresResolver::LookupSRV(
    absl::string_view name,
    absl::AnyInvocable<void(absl::StatusOr<AresResolver::Result>)> callback) {
  grpc_core::MutexLock lock(&mutex_);
  GPR_ASSERT(initialized_);
  callback_map_.emplace(++id_, std::move(callback));
  auto* resolver_arg = new QueryArg(this, id_, name);
  ares_query(channel_, std::string(name).c_str(), ns_c_in, ns_t_srv,
             &AresResolver::OnSRVQueryDoneLocked,
             static_cast<void*>(resolver_arg));
  WorkLocked();
  MaybeStartTimerLocked();
}

void AresResolver::LookupTXT(
    absl::string_view name,
    absl::AnyInvocable<void(absl::StatusOr<AresResolver::Result>)> callback) {
  grpc_core::MutexLock lock(&mutex_);
  GPR_ASSERT(initialized_);
  callback_map_.emplace(++id_, std::move(callback));
  auto* resolver_arg = new QueryArg(this, id_, name);
  ares_search(channel_, std::string(name).c_str(), ns_c_in, ns_t_txt,
              &AresResolver::OnTXTDoneLocked, static_cast<void*>(resolver_arg));
  WorkLocked();
  MaybeStartTimerLocked();
}

void AresResolver::OnHostbynameDoneLocked(void* arg, int status,
                                          int /*timeouts*/,
                                          struct hostent* hostent) {
  std::unique_ptr<HostnameQueryArg> hostname_qa(
      static_cast<HostnameQueryArg*>(arg));
  auto* ares_resolver = hostname_qa->ares_resolver;
  auto nh = ares_resolver->callback_map_.extract(hostname_qa->callback_map_id);
  GPR_ASSERT(!nh.empty());
  auto callback = std::move(nh.mapped());
  if (status != ARES_SUCCESS) {
    std::string error_msg = absl::StrFormat(
        "c-ares status is not ARES_SUCCESS qtype=%s name=%s: %s",
        hostname_qa->type, hostname_qa->qname.c_str(), ares_strerror(status));
    GRPC_ARES_RESOLVER_TRACE_LOG("request:%p on_hostbyname_done_locked: %s",
                                 ares_resolver, error_msg.c_str());
    ares_resolver->event_engine_->Run(
        [callback = std::move(callback),
         status = AresStatusToAbslStatus(status, error_msg)]() mutable {
          callback(status);
        });
    return;
  }
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
            "request:%p c-ares resolver gets a AF_INET6 result: \n"
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
            "request:%p c-ares resolver gets a AF_INET result: \n"
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
  auto callback = std::move(nh.mapped());
  if (status != ARES_SUCCESS) {
    std::string error_msg = absl::StrFormat(
        "c-ares status is not ARES_SUCCESS qtype=SRV name=%s: %s",
        qa->qname.c_str(), ares_strerror(status));
    GRPC_ARES_RESOLVER_TRACE_LOG("request:%p on_hostbyname_done_locked: %s",
                                 ares_resolver, error_msg.c_str());
    ares_resolver->event_engine_->Run(
        [callback = std::move(callback),
         status = AresStatusToAbslStatus(status, error_msg)]() mutable {
          callback(status);
        });
    return;
  }
  struct ares_srv_reply* reply = nullptr;
  const int parse_status = ares_parse_srv_reply(abuf, alen, &reply);
  GRPC_ARES_RESOLVER_TRACE_LOG("request:%p ares_parse_srv_reply: %d",
                               ares_resolver, parse_status);
  std::vector<EventEngine::DNSResolver::SRVRecord> result;
  if (parse_status == ARES_SUCCESS) {
    for (struct ares_srv_reply* srv_it = reply; srv_it != nullptr;
         srv_it = srv_it->next) {
      EventEngine::DNSResolver::SRVRecord record;
      record.host = srv_it->host;
      record.port = srv_it->port;
      record.priority = srv_it->priority;
      record.weight = srv_it->weight;
      result.push_back(std::move(record));
    }
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
  auto callback = std::move(nh.mapped());
  struct ares_txt_ext* reply = nullptr;
  int parse_status = ARES_SUCCESS;
  if (status == ARES_SUCCESS) {
    GRPC_ARES_RESOLVER_TRACE_LOG(
        "request:%p on_txt_done_locked name=%s ARES_SUCCESS", ares_resolver,
        qa->qname.c_str());
    parse_status = ares_parse_txt_reply_ext(buf, len, &reply);
  }
  if (status != ARES_SUCCESS || parse_status != ARES_SUCCESS) {
    std::string error_msg = absl::StrFormat(
        "c-ares status is not ARES_SUCCESS qtype=TXT name=%s: %s",
        qa->qname.c_str(), ares_strerror(status));
    GRPC_ARES_RESOLVER_TRACE_LOG("request:%p on_hostbyname_done_locked: %s",
                                 ares_resolver, error_msg.c_str());
    ares_resolver->event_engine_->Run(
        [callback = std::move(callback),
         status = AresStatusToAbslStatus(status, error_msg)]() mutable {
          callback(status);
        });
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
  GRPC_ARES_RESOLVER_TRACE_LOG("request: %p, got %zu TXT records",
                               ares_resolver, result.size());
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

HostnameQuery::HostnameQuery(EventEngine* event_engine,
                             AresResolver* ares_resolver)
    : grpc_core::RefCounted<HostnameQuery>(
          GRPC_TRACE_FLAG_ENABLED(grpc_trace_ares_resolver) ? "HostnameQuery"
                                                            : nullptr),
      event_engine_(event_engine),
      ares_resolver_(ares_resolver) {}

void HostnameQuery::Lookup(
    absl::string_view name, absl::string_view default_port,
    EventEngine::DNSResolver::LookupHostnameCallback on_resolve) {
  absl::string_view host;
  absl::string_view port_s;
  grpc_core::SplitHostPort(name, &host, &port_s);
  if (host.empty()) {
    event_engine_->Run(
        [on_resolve = std::move(on_resolve),
         status = absl::InvalidArgumentError(absl::StrCat(
             "Unparseable name: ", name))]() mutable { on_resolve(status); });
    return;
  }
  if (port_s.empty()) {
    if (default_port.empty()) {
      event_engine_->Run([on_resolve = std::move(on_resolve),
                          status = absl::InvalidArgumentError(absl::StrFormat(
                              "No port in name %s or default_port argument",
                              name.data()))]() mutable { on_resolve(status); });
      return;
    }
    port_s = default_port;
  }
  int port = 0;
  if (port_s == "http") {
    port = 80;
  } else if (port_s == "https") {
    port = 443;
  } else if (!absl::SimpleAtoi(port_s, &port)) {
    event_engine_->Run([on_resolve = std::move(on_resolve),
                        status = absl::InvalidArgumentError(absl::StrCat(
                            "Failed to parse port in name: ",
                            name))]() mutable { on_resolve(status); });
    return;
  }
  on_resolve_ = std::move(on_resolve);
  pending_requests_ = 2;
  ares_resolver_->LookupHostname(
      host, port, AF_INET,
      [self = Ref(DEBUG_LOCATION, "A query")](
          absl::StatusOr<AresResolver::Result> result_or) {
        if (result_or.ok()) {
          GPR_ASSERT(absl::holds_alternative<Result>(*result_or));
          auto result = absl::get<Result>(*result_or);
          self->MaybeOnResolve(std::move(result));
        } else {
          self->MaybeOnResolve(result_or.status());
        }
      });
  ares_resolver_->LookupHostname(
      host, port, AF_INET6,
      [self = Ref(DEBUG_LOCATION, "AAAA query")](
          absl::StatusOr<AresResolver::Result> result_or) {
        if (result_or.ok()) {
          GPR_ASSERT(absl::holds_alternative<Result>(*result_or));
          auto result = absl::get<Result>(*result_or);
          self->MaybeOnResolve(std::move(result));
        } else {
          self->MaybeOnResolve(result_or.status());
        }
      });
}

void HostnameQuery::MaybeOnResolve(absl::StatusOr<Result> result_or) {
  bool done = false;
  {
    grpc_core::MutexLock lock(&mutex_);
    if (--pending_requests_ == 0) {
      // This is the last one.
      done = true;
    }
    if (!result_or.ok()) {
      if (IsDefaultStatusOr(result_)) {
        // Sets with first error.
        result_ = result_or.status();
      }
    } else {
      if (result_.ok()) {
        result_->insert(result_->end(), result_or->begin(), result_or->end());
      } else {
        // Overrides the existing error.
        result_ = std::move(*result_or);
      }
    }
  }
  if (done) {
    MaybeSortResolvedAddresses();
    on_resolve_(std::move(result_));
  }
}

void HostnameQuery::LogResolvedAddressesListLocked(
    absl::string_view input_output_str) {
  GPR_ASSERT(result_.ok());
  for (size_t i = 0; i < result_->size(); i++) {
    auto addr_str = ResolvedAddressToString((*result_)[i]);
    gpr_log(GPR_INFO,
            "(EventEngine ares_resolver) c-ares address sorting: "
            "%s[%" PRIuPTR "]=%s",
            input_output_str.data(), i,
            addr_str.ok() ? addr_str->c_str()
                          : addr_str.status().ToString().c_str());
  }
}

void HostnameQuery::MaybeSortResolvedAddresses() {
  if (!result_.ok()) {
    return;
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_ares_resolver_address_sorting)) {
    LogResolvedAddressesListLocked("input");
  }
  address_sorting_sortable* sortables = static_cast<address_sorting_sortable*>(
      gpr_zalloc(sizeof(address_sorting_sortable) * result_->size()));
  for (size_t i = 0; i < result_->size(); i++) {
    sortables[i].user_data = &(*result_)[i];
    memcpy(&sortables[i].dest_addr.addr, (*result_)[i].address(),
           (*result_)[i].size());
    sortables[i].dest_addr.len = (*result_)[i].size();
  }
  address_sorting_rfc_6724_sort(sortables, result_->size());
  HostnameQuery::Result sorted;
  sorted.reserve(result_->size());
  for (size_t i = 0; i < result_->size(); i++) {
    sorted.emplace_back(
        *static_cast<EventEngine::ResolvedAddress*>(sortables[i].user_data));
  }
  gpr_free(sortables);
  result_ = std::move(sorted);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_ares_resolver_address_sorting)) {
    LogResolvedAddressesListLocked("output");
  }
}

SRVQuery::SRVQuery(EventEngine* event_engine, AresResolver* ares_resolver)
    : grpc_core::RefCounted<SRVQuery>(
          GRPC_TRACE_FLAG_ENABLED(grpc_trace_ares_resolver) ? "SRVQuery"
                                                            : nullptr),
      event_engine_(event_engine),
      ares_resolver_(ares_resolver) {}

void SRVQuery::Lookup(absl::string_view name,
                      EventEngine::DNSResolver::LookupSRVCallback on_resolve) {
  absl::string_view host;
  absl::string_view port;
  grpc_core::SplitHostPort(name, &host, &port);
  if (host.empty()) {
    event_engine_->Run(
        [on_resolve = std::move(on_resolve),
         status = absl::InvalidArgumentError(absl::StrCat(
             "Unparseable name: ", name))]() mutable { on_resolve(status); });
    return;
  }
  // Don't query for SRV records if the target is "localhost"
  if (absl::EqualsIgnoreCase(host, "localhost")) {
    event_engine_->Run(
        [on_resolve = std::move(on_resolve),
         status = absl::UnknownError(
             "Skip querying for SRV records for localhost target")]() mutable {
          on_resolve(status);
        });
    return;
  }
  on_resolve_ = std::move(on_resolve);
  ares_resolver_->LookupSRV(
      host, [self = Ref(DEBUG_LOCATION, "SRV query")](
                absl::StatusOr<AresResolver::Result> result_or) {
        if (!result_or.ok()) {
          self->on_resolve_(result_or.status());
          return;
        }
        GPR_ASSERT(absl::holds_alternative<Result>(*result_or));
        self->on_resolve_(absl::get<Result>(*result_or));
      });
}

TXTQuery::TXTQuery(EventEngine* event_engine, AresResolver* ares_resolver)
    : grpc_core::RefCounted<TXTQuery>(
          GRPC_TRACE_FLAG_ENABLED(grpc_trace_ares_resolver) ? "TXTQuery"
                                                            : nullptr),
      event_engine_(event_engine),
      ares_resolver_(ares_resolver) {}

void TXTQuery::Lookup(absl::string_view name,
                      EventEngine::DNSResolver::LookupTXTCallback on_resolve) {
  absl::string_view host;
  absl::string_view port;
  grpc_core::SplitHostPort(name, &host, &port);
  if (host.empty()) {
    event_engine_->Run(
        [on_resolve = std::move(on_resolve),
         status = absl::InvalidArgumentError(absl::StrCat(
             "Unparseable name: ", name))]() mutable { on_resolve(status); });
    return;
  }
  // Don't query for TXT records if the target is "localhost"
  if (absl::EqualsIgnoreCase(host, "localhost")) {
    event_engine_->Run(
        [on_resolve = std::move(on_resolve),
         status = absl::UnknownError(
             "Skip querying for TXT records for localhost target")]() mutable {
          on_resolve(status);
        });
    return;
  }
  on_resolve_ = std::move(on_resolve);
  ares_resolver_->LookupTXT(
      host, [self = Ref(DEBUG_LOCATION, "TXT query")](
                absl::StatusOr<AresResolver::Result> result_or) {
        if (!result_or.ok()) {
          self->on_resolve_(result_or.status());
          return;
        }
        GPR_ASSERT(absl::holds_alternative<Result>(*result_or));
        self->on_resolve_(absl::get<Result>(*result_or));
      });
}

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_ARES == 1

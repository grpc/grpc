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

#if GRPC_ARES == 1

#include <list>
#include <memory>
#include <variant>

#include <ares.h>

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

}  // namespace

using LookupHostnameCallback = EventEngine::DNSResolver::LookupHostnameCallback;

enum class LookupType {
  kA,
  kAAAA,
  kSRV,
  kTXT,
};

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
    absl::Status status = SetRequestDNSServer(dns_server_);
    if (!status.ok()) {
      return status;
    }
  }
  initialized_ = true;
  return absl::OkStatus();
}

void AresResolver::Lookup(
    LookupType type, absl::string_view name, absl::string_view dns_server,
    absl::AnyInvocable<void(absl::StatusOr<AresResolver::Result>)>
        on_complete) {
  grpc_core::MutexLock lock(&mutex_);
  GPR_ASSERT(initialized_);
  on_complete_ = std::move(on_complete);
  switch (type) {
    case LookupType::kA:
      ares_gethostbyname(channel_, name, AF_INET,
                         &AresResolver::OnHostbynameDoneLocked,
                         static_cast<void*>(this));
      break;
    case LookupType::kAAAA:
      ares_gethostbyname(channel_, name, AF_INET6,
                         &AresResolver::OnHostbynameDoneLocked,
                         static_cast<void*>(this));
      break;
    case LookupType::kSRV:
      ares_query(channel_, name, ns_c_in, ns_t_srv,
                 &AresResolver::OnSRVQueryDoneLocked, static_cast<void*>(this));
      break;
    case LookupType::kTXT:
      ares_search(channel_, name, ns_c_in, ns_t_txt,
                  &AresResolver::OnTXTDoneLocked, static_cast<void*>(this));
      break;
  }
  WorkLocked();
}

void AresResolver::Orphan() {
  {
    grpc_core::MutexLock lock(&mutex_);
    shutting_down_ = true;
    for (const auto& fd_node : fd_node_list_) {
      if (!fd_node->already_shutdown) {
        GRPC_ARES_WRAPPER_TRACE_LOG("request: %p shutdown fd: %s", this,
                                    fd_node->polled_fd->GetName());
        fd_node->polled_fd->ShutdownLocked(status);
        fd_node->already_shutdown = true;
      }
    }
  }
  Unref(DEBUG_LOCATION, "Orphan");
}

absl::Status AresResolver::SetRequestDNSServer(absl::string_view dns_server) {
  GRPC_ARES_WRAPPER_TRACE_LOG("request:%p Using DNS server %s", this,
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
          GRPC_ARES_WRAPPER_TRACE_LOG("request:%p new fd: %d", this, socks[i]);
        } else {
          new_list.splice(new_list.end(), fd_node_list_, iter);
        }
        FdNode* fd_node = new_list.back().get();
        // Register read_closure if the socket is readable and read_closure
        // has not been registered with this socket.
        if (ARES_GETSOCK_READABLE(socks_bitmask, i) &&
            !fd_node->readable_registered) {
          GRPC_ARES_WRAPPER_TRACE_LOG("request:%p notify read on: %d", this,
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
          GRPC_ARES_WRAPPER_TRACE_LOG("request:%p notify write on: %d", this,
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
      GRPC_ARES_WRAPPER_TRACE_LOG("request: %p shutdown fd: %s", this,
                                  fd_node->polled_fd->GetName());
      fd_node->polled_fd->ShutdownLocked(absl::OkStatus());
      fd_node->already_shutdown = true;
    }
    if (!fd_node->readable_registered && !fd_node->writable_registered) {
      GRPC_ARES_WRAPPER_TRACE_LOG("request: %p delete fd: %s", this,
                                  fd_node->polled_fd->GetName());
      fd_node_list_.pop_front();
    } else {
      new_list.splice(new_list.end(), fd_node_list_, fd_node_list_.begin());
    }
  }
  fd_node_list_ = std::move(new_list);
}

void AresResolver::OnReadable(FdNode* fd_node, absl::Status status) {
  grpc_core::MutexLock lock(&mutex_);
  GPR_ASSERT(fd_node->readable_registered);
  fd_node->readable_registered = false;
  GRPC_ARES_WRAPPER_TRACE_LOG("OnReadable: fd: %d; request: %p; status: %s",
                              fd_node->as, this, status.ToString().c_str());
  if (status.ok() && !done_) {
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
  Work();
}

void AresResolver::OnWritable(FdNode* fd_node, absl::Status status) {
  grpc_core::MutexLock lock(&mutex_);
  GPR_ASSERT(fd_node->writable_registered);
  fd_node->writable_registered = false;
  GRPC_ARES_WRAPPER_TRACE_LOG("OnWritable: fd: %d; request:%p; status: %s",
                              fd_node->as, this, status.ToString().c_str());
  if (status.ok() && !done_) {
    ares_process_fd(channel_, ARES_SOCKET_BAD, fd_node->as);
  } else {
    // If error is not absl::OkStatus() or the resolution was cancelled, it
    // means the fd has been shutdown or timed out. The pending lookups made
    // on this request will be cancelled by the following ares_cancel(). The
    // remaining file descriptors in this request will be cleaned up in the
    // following Work() method.
    ares_cancel(channel_);
  }
  Work();
}

void AresResolver::OnHostbynameDoneLocked(void* arg, int status,
                                          int /*timeouts*/,
                                          struct hostent* hostent) {
  AresResolver* object = static_cast<AresResolver*>(arg);
  object->done_ = true;
  if (status != ARES_SUCCESS) {
    std::string error_msg = absl::StrFormat(
        "c-ares status is not ARES_SUCCESS qtype=A/AAAA name=%s: %s",
        object->name_.c_str(), ares_strerror(status));
    GRPC_ARES_WRAPPER_TRACE_LOG("request:%p on_hostbyname_done_locked: %s",
                                object, error_msg.c_str());
    object->event_engine_->Run(
        [on_complete = std::move(object->on_complete_),
         status = AresStatusToAbslStatus(status, error_msg)] {
          on_complete(status);
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
        addr.sin6_port = htons(request->port_);
        result.emplace_back(reinterpret_cast<const sockaddr*>(&addr), addr_len);
        char output[INET6_ADDRSTRLEN];
        ares_inet_ntop(AF_INET6, &addr.sin6_addr, output, INET6_ADDRSTRLEN);
        GRPC_ARES_WRAPPER_TRACE_LOG(
            "request:%p c-ares resolver gets a AF_INET6 result: \n"
            "  addr: %s\n  port: %d\n  sin6_scope_id: %d\n",
            request, output, request->port_, addr.sin6_scope_id);
        break;
      }
      case AF_INET: {
        size_t addr_len = sizeof(struct sockaddr_in);
        struct sockaddr_in addr;
        memset(&addr, 0, addr_len);
        memcpy(&addr.sin_addr, hostent->h_addr_list[i], sizeof(struct in_addr));
        addr.sin_family = static_cast<unsigned char>(hostent->h_addrtype);
        addr.sin_port = htons(request->port_);
        result.emplace_back(reinterpret_cast<const sockaddr*>(&addr), addr_len);
        char output[INET_ADDRSTRLEN];
        ares_inet_ntop(AF_INET, &addr.sin_addr, output, INET_ADDRSTRLEN);
        GRPC_ARES_WRAPPER_TRACE_LOG(
            "request:%p c-ares resolver gets a AF_INET result: \n"
            "  addr: %s\n  port: %d\n",
            request, output, request->port_);
        break;
      }
    }
  }
  object->event_engine_->Run(
      [on_complete = std::move(object->on_complete_),
       result = std::move(result)] { on_complete(std::move(result)); });
}

void AresResolver::OnSRVQueryDoneLocked(void* arg, int status, int /*timeouts*/,
                                        unsigned char* abuf, int alen) {
  AresResolver* object = static_cast<AresResolver*>(arg);
  object->done_ = true;
  if (status != ARES_SUCCESS) {
    std::string error_msg = absl::StrFormat(
        "c-ares status is not ARES_SUCCESS qtype=SRV name=%s: %s",
        object->name_.c_str(), ares_strerror(status));
    GRPC_ARES_WRAPPER_TRACE_LOG("request:%p on_hostbyname_done_locked: %s",
                                object, error_msg.c_str());
    object->event_engine_->Run(
        [on_complete = std::move(object->on_complete_),
         status = AresStatusToAbslStatus(status, error_msg)] {
          on_complete(status);
        });
    return;
  }
  struct ares_srv_reply* reply = nullptr;
  const int parse_status = ares_parse_srv_reply(abuf, alen, &reply);
  GRPC_ARES_WRAPPER_TRACE_LOG("request:%p ares_parse_srv_reply: %d", r,
                              parse_status);
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
  object->event_engine_->Run(
      [on_complete = std::move(object->on_complete_),
       result = std::move(result)] { on_complete(std::move(result)); });
}

void AresResolver::OnTXTDoneLocked(void* arg, int status, int /*timeouts*/,
                                   unsigned char* buf, int len) {
  AresResolver* object = static_cast<AresResolver*>(arg);
  object->done_ = true;
  struct ares_txt_ext* reply = nullptr;
  int parse_status = ARES_SUCCESS;
  if (status == ARES_SUCCESS) {
    GRPC_ARES_WRAPPER_TRACE_LOG(
        "request:%p on_txt_done_locked name=%s ARES_SUCCESS", r,
        r->host().c_str());
    parse_status = ares_parse_txt_reply_ext(buf, len, &reply);
  }
  if (status != ARES_SUCCESS || parse_status != ARES_SUCCESS) {
    std::string error_msg = absl::StrFormat(
        "c-ares status is not ARES_SUCCESS qtype=TXT name=%s: %s",
        object->name_.c_str(), ares_strerror(status));
    GRPC_ARES_WRAPPER_TRACE_LOG("request:%p on_hostbyname_done_locked: %s",
                                object, error_msg.c_str());
    object->event_engine_->Run(
        [on_complete = std::move(object->on_complete_),
         status = AresStatusToAbslStatus(status, error_msg)] {
          on_complete(status);
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
  GRPC_ARES_WRAPPER_TRACE_LOG("request: %p, got %zu TXT records", r,
                              result.size());
  if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_ares_wrapper)) {
    for (const auto& record : result) {
      gpr_log(GPR_INFO, "%s", record.c_str());
    }
  }
  // Clean up.
  ares_free_data(reply);
  object->event_engine_->Run(
      [on_complete = std::move(object->on_complete_),
       result = std::move(result)] { on_complete(std::move(result)); });
}

void HostnameQuery::Lookup(absl::string_view name,
                           absl::string_view default_port,
                           LookupHostnameCallback on_resolve) {
  absl::string_view host;
  absl::string_view port;
  grpc_core::SplitHostPort(name, &host, &port);
  if (host.empty()) {
    event_engine->Run([on_resolve = std::move(on_resolve),
                       status = absl::InvalidArgumentError(
                           absl::StrCat("Unparseable name: ", name))]() {
      on_resolve(status);
    });
    return;
  }
  if (port.empty()) {
    if (default_port.empty()) {
      event_engine->Run([on_resolve = std::move(on_resolve),
                         status = absl::InvalidArgumentError(absl::StrFormat(
                             "No port in name %s or default_port argument",
                             name.data()))]() { on_resolve(status); });
      return;
    }
    port = default_port;
  }
  if (port == "http") {
    port_ = 80;
  } else if (port == "https") {
    port_ = 443;
  } else if (!absl::SimpleAtoi(port, &port_)) {
    event_engine->Run([on_resolve = std::move(on_resolve),
                       status = absl::InvalidArgumentError(absl::StrCat(
                           "Failed to parse port in name: ", name))]() {
      on_resolve(status);
    });
    return;
  }
  on_resolve_ = std::move(on_resolve);
  ares_resolver_->Lookup(
      LookupType::kA, host,
      [self = Ref(DEBUG_LOCATION, "A query")](
          absl::StatusOr<AresResolver::Result> result_or) {
        grpc_core::MutexLock lock(&self->mutex_);
        if (!result_or.ok()) {
          return;
        }
        GPR_ASSERT(absl::holds_alternative<Result>(*result_or));
        auto result = absl::get<Result>(*result_or);
      });
  ares_resolver_->Lookup(
      LookupType::kAAAA, host,
      [self = Ref(DEBUG_LOCATION, "AAAA query")](
          absl::StatusOr<AresResolver::Result> result_or) {
        {
          grpc_core::MutexLock lock(&self->mutex_);
          --self->pending_requests_;
          if (!result_or.ok()) {
            return;
          } else {
            GPR_ASSERT(absl::holds_alternative<Result>(*result_or));
            auto result = absl::get<Result>(*result_or);
          }
        }
      });
}

void SRVQuery::Lookup(absl::string_view name, LookupSRVCallback on_resolve) {
  absl::string_view host;
  absl::string_view port;
  grpc_core::SplitHostPort(name, &host, &port);
  if (host.empty()) {
    event_engine_->Run([on_resolve = std::move(on_resolve),
                        status = absl::InvalidArgumentError(
                            absl::StrCat("Unparseable name: ", name))]() {
      on_resolve(status);
    });
    return;
  }
  // Don't query for SRV records if the target is "localhost"
  if (absl::EqualsIgnoreCase(host, "localhost")) {
    event_engine_->Run(
        [on_resolve = std::move(on_resolve),
         status = absl::UnknownError(
             "Skip querying for SRV records for localhost target")]() {
          on_resolve(status);
        });
    return;
  }
  on_resolve_ = std::move(on_resolve);
  ares_resolver_->Lookup(
      LookupType::kSRV, host,
      [self = Ref(DEBUG_LOCATION, "SRV query")](
          absl::StatusOr<AresResolver::Result> result_or) {
        if (!result_or.ok()) {
          self->on_resolve_(result_or.status());
          return;
        }
        GPR_ASSERT(absl::holds_alternative<Result>(*result_or));
        self->on_resolve_(absl::get<Result>(*result_or));
      });
}

void TXTQuery::Lookup(absl::string_view name, LookupTXTCallback on_resolve) {
  absl::string_view host;
  absl::string_view port;
  grpc_core::SplitHostPort(name, &host, &port);
  if (host.empty()) {
    event_engine_->Run([on_resolve = std::move(on_resolve),
                        status = absl::InvalidArgumentError(
                            absl::StrCat("Unparseable name: ", name))]() {
      on_resolve(status);
    });
    return;
  }
  // Don't query for TXT records if the target is "localhost"
  if (absl::EqualsIgnoreCase(host, "localhost")) {
    event_engine_->Run(
        [on_resolve = std::move(on_resolve),
         status = absl::UnknownError(
             "Skip querying for TXT records for localhost target")]() {
          on_resolve(status);
        });
    return;
  }
  on_resolve_ = std::move(on_resolve);
  ares_resolver_->Lookup(
      LookupType::kTXT, host,
      [self = Ref(DEBUG_LOCATION, "TXT query")](
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

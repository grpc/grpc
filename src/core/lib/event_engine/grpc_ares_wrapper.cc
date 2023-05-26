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

#include "src/core/lib/event_engine/grpc_ares_wrapper.h"

#include <iterator>

#include "src/core/lib/iomgr/port.h"

// IWYU pragma: no_include <arpa/inet.h>
// IWYU pragma: no_include <arpa/nameser.h>
// IWYU pragma: no_include <inttypes.h>
// IWYU pragma: no_include <netdb.h>
// IWYU pragma: no_include <netinet/in.h>
// IWYU pragma: no_include <stdlib.h>
// IWYU pragma: no_include <sys/socket.h>
// IWYU pragma: no_include <ratio>

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

grpc_core::TraceFlag grpc_trace_ares_wrapper_address_sorting(
    false, "ares_wrapper_address_sorting");

grpc_core::TraceFlag grpc_trace_ares_wrapper_stacktrace(
    false, "ares_wrapper_stacktrace");

void PrintCurrentStackTrace() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_ares_wrapper_stacktrace)) {
    absl::optional<std::string> stacktrace = grpc_core::GetCurrentStackTrace();
    if (stacktrace.has_value()) {
      gpr_log(GPR_DEBUG, "%s", stacktrace->c_str());
    } else {
      gpr_log(GPR_DEBUG, "stacktrace unavailable");
    }
  }
}

grpc_core::TraceFlag grpc_trace_ares_wrapper(false,
                                             "event_engine_ares_wrapper");

namespace {

EventEngine::Duration calculate_next_ares_backup_poll_alarm_duration() {
  // An alternative here could be to use ares_timeout to try to be more
  // accurate, but that would require using "struct timeval"'s, which just
  // makes things a bit more complicated. So just poll every second, as
  // suggested by the c-ares code comments.
  return std::chrono::seconds(1);
}

bool IsIpv6LoopbackAvailable() {
#ifdef GRPC_POSIX_SOCKET_ARES_EV_DRIVER
  return PosixSocketWrapper::IsIpv6LoopbackAvailable();
#elif defined(GRPC_WINDOWS_SOCKET_ARES_EV_DRIVER)
  // TODO(yijiem): make this portable for Windows
  return false;
#else
#error "Unsupported platform"
#endif
}

struct HostbynameArg {
  GrpcAresHostnameRequest* request;
  const char* qtype;
};

absl::Status AresStatusToAbslStatus(int status, const std::string& error_msg) {
  switch (status) {
    case ARES_ECANCELLED:
      return absl::Status(absl::StatusCode::kCancelled, error_msg);
    case ARES_ENOTIMP:
      return absl::Status(absl::StatusCode::kUnimplemented, error_msg);
    case ARES_ENOTFOUND:
      return absl::Status(absl::StatusCode::kNotFound, error_msg);
    default:
      return absl::Status(absl::StatusCode::kUnknown, error_msg);
  }
}

}  // namespace

void GrpcAresHostnameRequest::OnHostbynameDoneLocked(void* arg, int status,
                                                     int /*timeouts*/,
                                                     struct hostent* hostent) {
  std::unique_ptr<HostbynameArg> harg(static_cast<HostbynameArg*>(arg));
  GrpcAresHostnameRequest* request = harg->request;
  GPR_ASSERT(--request->pending_queries_ >= 0);
  if (request->pending_queries_ == 0) {
    request->set_shutting_down();
  }
  if (status != ARES_SUCCESS) {
    std::string error_msg = absl::StrFormat(
        "c-ares status is not ARES_SUCCESS qtype=%s name=%s: %s", harg->qtype,
        request->host().c_str(), ares_strerror(status));
    GRPC_ARES_WRAPPER_TRACE_LOG("request:%p on_hostbyname_done_locked: %s",
                                request, error_msg.c_str());
    request->AddError(AresStatusToAbslStatus(status, error_msg));
    return;
  }
  GRPC_ARES_WRAPPER_TRACE_LOG(
      "request:%p on_hostbyname_done_locked qtype=%s host=%s ARES_SUCCESS",
      request, harg->qtype, request->host().c_str());

  std::vector<EventEngine::ResolvedAddress> resolved_addresses;
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
        resolved_addresses.emplace_back(
            reinterpret_cast<const sockaddr*>(&addr), addr_len);
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
        resolved_addresses.emplace_back(
            reinterpret_cast<const sockaddr*>(&addr), addr_len);
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
  if (request->result_.ok()) {
    request->result_->insert(request->result_->end(),
                             resolved_addresses.begin(),
                             resolved_addresses.end());
  } else {
    // Override the existing error.
    request->result_ = std::move(resolved_addresses);
  }
}

void GrpcAresSRVRequest::OnSRVQueryDoneLocked(void* arg, int status,
                                              int /*timeouts*/,
                                              unsigned char* abuf, int alen) {
  GrpcAresSRVRequest* r = static_cast<GrpcAresSRVRequest*>(arg);
  r->set_shutting_down();
  if (status != ARES_SUCCESS) {
    std::string error_msg = absl::StrFormat(
        "c-ares status is not ARES_SUCCESS qtype=SRV name=%s: %s",
        r->host().c_str(), ares_strerror(status));
    GRPC_ARES_WRAPPER_TRACE_LOG("request:%p on_srv_query_done_locked: %s", r,
                                error_msg.c_str());
    r->AddError(AresStatusToAbslStatus(status, error_msg));
    return;
  }
  GRPC_ARES_WRAPPER_TRACE_LOG(
      "request:%p on_srv_query_done_locked name=%s ARES_SUCCESS", r,
      r->host().c_str());
  struct ares_srv_reply* reply = nullptr;
  const int parse_status = ares_parse_srv_reply(abuf, alen, &reply);
  GRPC_ARES_WRAPPER_TRACE_LOG("request:%p ares_parse_srv_reply: %d", r,
                              parse_status);
  Result result;
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
  r->result_ = std::move(result);
}

void GrpcAresTXTRequest::OnTXTDoneLocked(void* arg, int status,
                                         int /*timeouts*/, unsigned char* buf,
                                         int len) {
  GrpcAresTXTRequest* r = static_cast<GrpcAresTXTRequest*>(arg);
  r->set_shutting_down();
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
        r->host().c_str(), ares_strerror(status));
    GRPC_ARES_WRAPPER_TRACE_LOG("request:%p on_txt_done_locked %s", r,
                                error_msg.c_str());
    r->AddError(AresStatusToAbslStatus(status, error_msg));
    return;
  }
  Result result;
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
  r->result_ = std::move(result);
}

GrpcAresRequest::GrpcAresRequest(
    absl::string_view name, EventEngine::Duration timeout,
    std::unique_ptr<GrpcPolledFdFactory> polled_fd_factory,
    EventEngine* event_engine)
    : grpc_core::InternallyRefCounted<GrpcAresRequest>(
          GRPC_TRACE_FLAG_ENABLED(grpc_trace_ares_wrapper) ? "GrpcAresRequest"
                                                           : nullptr),
      name_(name),
      timeout_(timeout),
      polled_fd_factory_(std::move(polled_fd_factory)),
      event_engine_(event_engine) {}

GrpcAresRequest::~GrpcAresRequest() {
  GPR_ASSERT(fd_node_list_.empty());
  if (initialized_) {
    ares_destroy(channel_);
    PrintCurrentStackTrace();
  }
  GRPC_ARES_WRAPPER_TRACE_LOG("request:%p destructor", this);
}

absl::StatusOr<std::string> GrpcAresRequest::ParseNameToResolve()
    ABSL_NO_THREAD_SAFETY_ANALYSIS {
  GPR_DEBUG_ASSERT(!initialized_);
  std::string port;
  // parse name, splitting it into host and port parts
  grpc_core::SplitHostPort(name_, &host_, &port);
  absl::Status error;
  if (host_.empty()) {
    return absl::Status(absl::StatusCode::kInvalidArgument,
                        absl::StrCat("Unparseable name: ", name_));
  }
  return port;
}

absl::Status GrpcAresRequest::InitializeAresOptions(
    absl::string_view dns_server) ABSL_NO_THREAD_SAFETY_ANALYSIS {
  GPR_DEBUG_ASSERT(!initialized_);
  ares_options opts = {};
  opts.flags |= ARES_FLAG_STAYOPEN;
  int status = ares_init_options(&channel_, &opts, ARES_OPT_FLAGS);
  if (status != ARES_SUCCESS) {
    gpr_log(GPR_ERROR, "ares_init_options failed, status: %d", status);
    return AresStatusToAbslStatus(
        status, absl::StrCat("Failed to init ares channel. c-ares error: ",
                             ares_strerror(status)));
  }
  event_engine_grpc_ares_test_only_inject_config(channel_);
  // If dns_server is specified, use it.
  absl::Status error = SetRequestDNSServer(dns_server);
  if (!error.ok()) {
    ares_destroy(channel_);
    return error;
  }
  initialized_ = true;
  return absl::OkStatus();
}

bool GrpcAresRequest::Cancel() {
  grpc_core::MutexLock lock(&mu());
  if (std::exchange(shutting_down_, true)) {
    // Already shutting down, maybe resolved, cancelled or timed-out.
    return false;
  }
  cancelled_ = true;
  CancelTimers();
  ShutdownPolledFdsLocked(grpc_core::StatusCreate(
      absl::StatusCode::kCancelled, "Cancel", DEBUG_LOCATION, {}));
  return true;
}

void GrpcAresRequest::Work() {
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
        if (iter == std::end(fd_node_list_)) {
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

void GrpcAresRequest::StartTimers() {
  // Initialize overall DNS resolution timeout alarm
  EventEngine::Duration timeout =
      timeout_.count() == 0 ? EventEngine::Duration::max() : timeout_;
  GRPC_ARES_WRAPPER_TRACE_LOG("request:%p StartTimers timeout in %zu ms", this,
                              Milliseconds(timeout));

  query_timeout_handle_ = event_engine_->RunAfter(
      timeout,
      [self = Ref(DEBUG_LOCATION, "StartTimers")] { self->OnQueryTimeout(); });

  // Initialize the backup poll alarm
  EventEngine::Duration next_ares_backup_poll_alarm_duration =
      calculate_next_ares_backup_poll_alarm_duration();
  GRPC_ARES_WRAPPER_TRACE_LOG(
      "request:%p StartTimers next ares process poll time in %zu ms", this,
      Milliseconds(next_ares_backup_poll_alarm_duration));

  ares_backup_poll_alarm_handle_ =
      event_engine_->RunAfter(next_ares_backup_poll_alarm_duration,
                              [self = Ref(DEBUG_LOCATION, "StartTimers")]() {
                                self->OnAresBackupPollAlarm();
                              });
}

void GrpcAresRequest::CancelTimers() {
  if (query_timeout_handle_.has_value()) {
    event_engine_->Cancel(*query_timeout_handle_);
    query_timeout_handle_.reset();
  }
  if (ares_backup_poll_alarm_handle_.has_value()) {
    event_engine_->Cancel(*ares_backup_poll_alarm_handle_);
    ares_backup_poll_alarm_handle_.reset();
  }
}

absl::Status GrpcAresRequest::SetRequestDNSServer(
    absl::string_view dns_server) {
  if (!dns_server.empty()) {
    GRPC_ARES_WRAPPER_TRACE_LOG("request:%p Using DNS server %s", this,
                                dns_server.data());
    grpc_resolved_address addr;
    struct ares_addr_port_node dns_server_addr;
    if (grpc_parse_ipv4_hostport(dns_server, &addr, /*log_errors=*/false)) {
      dns_server_addr.family = AF_INET;
      struct sockaddr_in* in = reinterpret_cast<struct sockaddr_in*>(addr.addr);
      memcpy(&dns_server_addr.addr.addr4, &in->sin_addr,
             sizeof(struct in_addr));
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
      return absl::Status(absl::StatusCode::kInvalidArgument,
                          absl::StrCat("Cannot parse authority: ", dns_server));
    }
    // Prevent an uninitialized variable.
    dns_server_addr.next = nullptr;
    int status = ares_set_servers_ports(channel_, &dns_server_addr);
    if (status != ARES_SUCCESS) {
      return AresStatusToAbslStatus(
          status, absl::StrCat("c-ares status is not ARES_SUCCESS: ",
                               ares_strerror(status)));
    }
  }
  return absl::OkStatus();
}

void GrpcAresRequest::OnReadable(FdNode* fd_node, absl::Status status) {
  mu().Lock();
  GPR_ASSERT(fd_node->readable_registered);
  fd_node->readable_registered = false;
  GRPC_ARES_WRAPPER_TRACE_LOG("OnReadable: fd: %d; request: %p; status: %s",
                              fd_node->as, this, status.ToString().c_str());
  PrintCurrentStackTrace();
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
  Work();
  if (shutting_down_) {
    OnResolveLockedAndUnlock();
    return;
  }
  mu().Unlock();
}

void GrpcAresRequest::OnWritable(FdNode* fd_node, absl::Status status) {
  mu().Lock();
  GPR_ASSERT(fd_node->writable_registered);
  fd_node->writable_registered = false;
  GRPC_ARES_WRAPPER_TRACE_LOG("OnWritable: fd: %d; request:%p; status: %s",
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
  Work();
  if (shutting_down_) {
    OnResolveLockedAndUnlock();
    return;
  }
  mu().Unlock();
}

void GrpcAresRequest::OnQueryTimeout() {
  grpc_core::MutexLock lock(&mu());
  query_timeout_handle_.reset();
  GRPC_ARES_WRAPPER_TRACE_LOG("request:%p OnQueryTimeout. shutting_down_=%d",
                              this, shutting_down_);
  if (std::exchange(shutting_down_, true)) {
    // Already shutting down, maybe resolved or cancelled.
    return;
  }
  absl::Status status(absl::StatusCode::kDeadlineExceeded, "OnQueryTimeout");
  AddError(status);
  ShutdownPolledFdsLocked(std::move(status));
}

// In case of non-responsive DNS servers, dropped packets, etc., c-ares has
// intelligent timeout and retry logic, which we can take advantage of by
// polling ares_process_fd on time intervals. Overall, the c-ares library is
// meant to be called into and given a chance to proceed name resolution:
//   a) when fd events happen
//   b) when some time has passed without fd events having happened
// For the latter, we use this backup poller. Also see
// https://github.com/grpc/grpc/pull/17688 description for more details.
void GrpcAresRequest::OnAresBackupPollAlarm() {
  mu().Lock();
  ares_backup_poll_alarm_handle_.reset();
  GRPC_ARES_WRAPPER_TRACE_LOG(
      "request:%p OnAresBackupPollAlarm shutting_down=%d.", this,
      shutting_down_);
  if (!shutting_down_) {
    for (const auto& fd_node : fd_node_list_) {
      if (!fd_node->already_shutdown) {
        GRPC_ARES_WRAPPER_TRACE_LOG(
            "request:%p OnAresBackupPollAlarm; ares_process_fd. fd=%s", this,
            fd_node->polled_fd->GetName());
        ares_socket_t as = fd_node->polled_fd->GetWrappedAresSocketLocked();
        ares_process_fd(channel_, as, as);
      }
    }
    if (!shutting_down_) {
      EventEngine::Duration next_ares_backup_poll_alarm_duration =
          calculate_next_ares_backup_poll_alarm_duration();
      ares_backup_poll_alarm_handle_ = event_engine_->RunAfter(
          next_ares_backup_poll_alarm_duration,
          [self = Ref(DEBUG_LOCATION, "OnAresBackupPollAlarm")]() {
            self->OnAresBackupPollAlarm();
          });
    }
    Work();
    if (shutting_down_) {
      OnResolveLockedAndUnlock();
      return;
    }
  }
  mu().Unlock();
}

// TODO(yijiem): Consider report this status or as part of the result when
// calling on_resolve_. This status is received in OnReadable/OnWritable.
void GrpcAresRequest::ShutdownPolledFdsLocked(absl::Status status) {
  for (const auto& fd_node : fd_node_list_) {
    if (!fd_node->already_shutdown) {
      GRPC_ARES_WRAPPER_TRACE_LOG("request: %p shutdown fd: %s", this,
                                  fd_node->polled_fd->GetName());
      fd_node->polled_fd->ShutdownLocked(status);
      fd_node->already_shutdown = true;
    }
  }
}

GrpcAresHostnameRequest::GrpcAresHostnameRequest(
    absl::string_view name, absl::string_view default_port,
    EventEngine::Duration timeout,
    std::unique_ptr<GrpcPolledFdFactory> polled_fd_factory,
    EventEngine* event_engine)
    : GrpcAresRequest(name, timeout, std::move(polled_fd_factory),
                      event_engine),
      default_port_(default_port),
      event_engine_(event_engine) {}

GrpcAresHostnameRequest::~GrpcAresHostnameRequest() {
  GRPC_ARES_WRAPPER_TRACE_LOG("request:%p destructor", this);
}

void GrpcAresHostnameRequest::Start(
    absl::AnyInvocable<void(absl::StatusOr<Result>)> on_resolve) {
  grpc_core::ReleasableMutexLock lock(&mu());
  GPR_ASSERT(initialized());
  on_resolve_ = std::move(on_resolve);
  GRPC_ARES_WRAPPER_TRACE_LOG(
      "request:%p c-ares GrpcAresHostnameRequest::Start name=%s, "
      "default_port=%s",
      this, name().c_str(), default_port_.c_str());
  // Early out if the target is an ipv4 or ipv6 literal.
  if (ResolveAsIPLiteralLocked()) {
    auto finish = [on_resolve = std::move(on_resolve_),
                   result = std::move(result_),
                   self = Ref(DEBUG_LOCATION, "Start")]() mutable {
      on_resolve(std::move(result));
    };
    lock.Release();
    event_engine_->Run(std::move(finish));
    return;
  }
  // TODO(yijiem): Early out if the target is localhost and we're on Windows.

  // We add up pending_queries_ here since ares_gethostbyname may directly
  // invoke the callback inline e.g. if there is any error with the input. The
  // callback will invoke OnResolve with an error status and may start the
  // shutdown process too early (before the second ares_gethostbyname) if we
  // haven't added up here.
  ++pending_queries_;
  if (IsIpv6LoopbackAvailable()) {
    ++pending_queries_;
    auto* arg = new HostbynameArg();
    arg->request = this;
    arg->qtype = "AAAA";
    ares_gethostbyname(channel(), host().c_str(), AF_INET6,
                       &GrpcAresHostnameRequest::OnHostbynameDoneLocked,
                       static_cast<void*>(arg));
  }
  auto* arg = new HostbynameArg();
  arg->request = this;
  arg->qtype = "A";
  ares_gethostbyname(channel(), host().c_str(), AF_INET,
                     &GrpcAresHostnameRequest::OnHostbynameDoneLocked,
                     static_cast<void*>(arg));
  Work();
  if (shutting_down()) {
    // It's possible that ares_gethostbyname gets everything done inline.
    if (result_.ok() && !result_->empty()) {
      SortResolvedAddressesLocked();
    }
    auto finish = [on_resolve = std::move(on_resolve_),
                   result = std::move(result_),
                   self = Ref(DEBUG_LOCATION, "Start")]() mutable {
      on_resolve(std::move(result));
    };
    // unlock
    lock.Release();
    event_engine_->Run(std::move(finish));
    return;
  }
  StartTimers();
}

void GrpcAresHostnameRequest::AddError(absl::Status error) {
  if (result_.ok()) {
    // Never override valid result.
    return;
  }
  if (result_.status().message().empty()) {
    // message is empty, consider this as the initial state.
    result_ = std::move(error);
  } else {
    result_ = absl::Status(
        result_.status().code(),
        absl::StrCat(result_.status().message(), "; ", error.message()));
  }
}

void GrpcAresHostnameRequest::OnResolveLockedAndUnlock() {
  if (std::exchange(resolved_, true)) {
    mu().Unlock();
    return;
  }
  GPR_ASSERT(pending_queries_ == 0);
  if (cancelled()) {
    // Cancel does not invoke on_resolve_.
    mu().Unlock();
    return;
  }
  CancelTimers();
  if (result_.ok() && !result_->empty()) {
    SortResolvedAddressesLocked();
  }
  auto on_resolve = std::move(on_resolve_);
  auto result = std::move(result_);
  // unlock
  mu().Unlock();
  on_resolve(std::move(result));
}

bool GrpcAresHostnameRequest::ResolveAsIPLiteralLocked() {
  // host_ and port_ should have been parsed successfully in Initialize.
  const std::string hostport = grpc_core::JoinHostPort(host(), port_);
  // TODO(yijiem): Change this when refactoring code in
  // src/core/lib/address_utils to use EventEngine::ResolvedAddress.
  grpc_resolved_address addr;
  if (grpc_parse_ipv4_hostport(hostport, &addr, false /* log errors */) ||
      grpc_parse_ipv6_hostport(hostport, &addr, false /* log errors */)) {
    result_.emplace();
    result_->emplace_back(reinterpret_cast<sockaddr*>(addr.addr), addr.len);
    return true;
  }
  return false;
}

void GrpcAresHostnameRequest::LogResolvedAddressesListLocked(
    absl::string_view input_output_str) {
  GPR_ASSERT(result_.ok());
  for (size_t i = 0; i < result_->size(); i++) {
    auto addr_str = ResolvedAddressToString((*result_)[i]);
    gpr_log(GPR_INFO,
            "(EventEngine c-ares wrapper) request:%p c-ares address sorting: "
            "%s[%" PRIuPTR "]=%s",
            this, input_output_str.data(), i,
            addr_str.ok() ? addr_str->c_str()
                          : addr_str.status().ToString().c_str());
  }
}

void GrpcAresHostnameRequest::SortResolvedAddressesLocked() {
  GPR_ASSERT(result_.ok());
  if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_ares_wrapper_address_sorting)) {
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
  Result sorted;
  sorted.reserve(result_->size());
  for (size_t i = 0; i < result_->size(); i++) {
    sorted.emplace_back(
        *static_cast<EventEngine::ResolvedAddress*>(sortables[i].user_data));
  }
  gpr_free(sortables);
  result_ = std::move(sorted);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_ares_wrapper_address_sorting)) {
    LogResolvedAddressesListLocked("output");
  }
}

// Deliberately thread-unsafe since it should only be called in the factory
// method as part of initialization.
absl::Status GrpcAresHostnameRequest::ParsePort(absl::string_view port)
    ABSL_NO_THREAD_SAFETY_ANALYSIS {
  if (port.empty()) {
    if (default_port_.empty()) {
      return absl::Status(
          absl::StatusCode::kInvalidArgument,
          absl::StrFormat("No port in name %s or default_port argument",
                          name().c_str()));
    }
    port = default_port_;
  }
  if (port == "http") {
    port_ = 80;
  } else if (port == "https") {
    port_ = 443;
  } else if (!absl::SimpleAtoi(port, &port_)) {
    return absl::Status(absl::StatusCode::kInvalidArgument,
                        absl::StrCat("Failed to parse port in name: ", name()));
  }
  return absl::OkStatus();
}

GrpcAresSRVRequest::GrpcAresSRVRequest(
    absl::string_view name, EventEngine::Duration timeout,
    std::unique_ptr<GrpcPolledFdFactory> polled_fd_factory,
    EventEngine* event_engine)
    : GrpcAresRequest(name, timeout, std::move(polled_fd_factory),
                      event_engine),
      event_engine_(event_engine) {}

GrpcAresSRVRequest::~GrpcAresSRVRequest() {
  GRPC_ARES_WRAPPER_TRACE_LOG("request:%p destructor", this);
}

void GrpcAresSRVRequest::Start(
    absl::AnyInvocable<void(absl::StatusOr<Result>)> on_resolve) {
  grpc_core::ReleasableMutexLock lock(&mu());
  GPR_ASSERT(initialized());
  on_resolve_ = std::move(on_resolve);
  // Query the SRV record
  ares_query(channel(), host().c_str(), ns_c_in, ns_t_srv,
             &GrpcAresSRVRequest::OnSRVQueryDoneLocked,
             static_cast<void*>(this));
  Work();
  if (shutting_down()) {
    auto finish = [on_resolve = std::move(on_resolve_),
                   result = std::move(result_),
                   self = Ref(DEBUG_LOCATION, "Start")]() mutable {
      on_resolve(std::move(result));
    };
    // unlock
    lock.Release();
    event_engine_->Run(std::move(finish));
    return;
  }
  StartTimers();
}

void GrpcAresSRVRequest::AddError(absl::Status error) {
  if (result_.ok()) {
    // Never override valid result.
    return;
  }
  if (result_.status().message().empty()) {
    // message is empty, consider this as the initial state.
    result_ = std::move(error);
  } else {
    result_ = absl::Status(
        result_.status().code(),
        absl::StrCat(result_.status().message(), "; ", error.message()));
  }
}

void GrpcAresSRVRequest::OnResolveLockedAndUnlock() {
  if (std::exchange(resolved_, true)) {
    mu().Unlock();
    return;
  }
  if (cancelled()) {
    // Cancel does not invoke on_resolve_.
    mu().Unlock();
    return;
  }
  CancelTimers();
  auto on_resolve = std::move(on_resolve_);
  auto result = std::move(result_);
  // unlock
  mu().Unlock();
  on_resolve(std::move(result));
}

GrpcAresTXTRequest::GrpcAresTXTRequest(
    absl::string_view name, EventEngine::Duration timeout,
    std::unique_ptr<GrpcPolledFdFactory> polled_fd_factory,
    EventEngine* event_engine)
    : GrpcAresRequest(name, timeout, std::move(polled_fd_factory),
                      event_engine),
      event_engine_(event_engine) {}

GrpcAresTXTRequest::~GrpcAresTXTRequest() {
  GRPC_ARES_WRAPPER_TRACE_LOG("request:%p destructor", this);
}

void GrpcAresTXTRequest::Start(
    absl::AnyInvocable<void(absl::StatusOr<Result>)> on_resolve) {
  grpc_core::ReleasableMutexLock lock(&mu());
  GPR_ASSERT(initialized());
  on_resolve_ = std::move(on_resolve);
  // Query the TXT record
  ares_search(channel(), host().c_str(), ns_c_in, ns_t_txt,
              &GrpcAresTXTRequest::OnTXTDoneLocked, static_cast<void*>(this));
  Work();
  if (shutting_down()) {
    auto finish = [on_resolve = std::move(on_resolve_),
                   result = std::move(result_),
                   self = Ref(DEBUG_LOCATION, "Start")]() mutable {
      on_resolve(std::move(result));
    };
    // unlock
    lock.Release();
    event_engine_->Run(std::move(finish));
    return;
  }
  StartTimers();
}

void GrpcAresTXTRequest::AddError(absl::Status error) {
  if (result_.ok()) {
    // Never override valid result.
    return;
  }
  if (result_.status().message().empty()) {
    // message is empty, consider this as the initial state.
    result_ = std::move(error);
  } else {
    result_ = absl::Status(
        result_.status().code(),
        absl::StrCat(result_.status().message(), "; ", error.message()));
  }
}

void GrpcAresTXTRequest::OnResolveLockedAndUnlock() {
  if (std::exchange(resolved_, true)) {
    mu().Unlock();
    return;
  }
  if (cancelled()) {
    // Cancel does not invoke on_resolve_.
    mu().Unlock();
    return;
  }
  CancelTimers();
  auto on_resolve = std::move(on_resolve_);
  auto result = std::move(result_);
  // unlock
  mu().Unlock();
  on_resolve(std::move(result));
}

absl::StatusOr<GrpcAresHostnameRequest*> GrpcAresHostnameRequest::Create(
    absl::string_view name, absl::string_view default_port,
    absl::string_view dns_server, EventEngine::Duration timeout,
    std::unique_ptr<GrpcPolledFdFactory> polled_fd_factory,
    EventEngine* event_engine) ABSL_NO_THREAD_SAFETY_ANALYSIS {
  auto* request = new GrpcAresHostnameRequest(
      name, default_port, timeout, std::move(polled_fd_factory), event_engine);
  absl::StatusOr<std::string> result = request->ParseNameToResolve();
  if (!result.ok()) {
    delete request;
    return result.status();
  }
  absl::Status status = request->ParsePort(*result);
  if (!status.ok()) {
    delete request;
    return status;
  }
  status = request->InitializeAresOptions(dns_server);
  if (!status.ok()) {
    delete request;
    return status;
  }
  return request;
}

absl::StatusOr<GrpcAresSRVRequest*> GrpcAresSRVRequest::Create(
    absl::string_view name, EventEngine::Duration timeout,
    absl::string_view dns_server,
    std::unique_ptr<GrpcPolledFdFactory> polled_fd_factory,
    EventEngine* event_engine) ABSL_NO_THREAD_SAFETY_ANALYSIS {
  auto* request = new GrpcAresSRVRequest(
      name, timeout, std::move(polled_fd_factory), event_engine);
  absl::StatusOr<std::string> result = request->ParseNameToResolve();
  if (!result.ok()) {
    delete request;
    return result.status();
  }
  // Don't query for SRV records if the target is "localhost"
  if (absl::EqualsIgnoreCase(request->host(), "localhost")) {
    delete request;
    return absl::Status(absl::StatusCode::kUnknown,
                        "Skip querying for SRV records for localhost target");
  }
  absl::Status status = request->InitializeAresOptions(dns_server);
  if (!status.ok()) {
    delete request;
    return status;
  }
  return request;
}

absl::StatusOr<GrpcAresTXTRequest*> GrpcAresTXTRequest::Create(
    absl::string_view name, EventEngine::Duration timeout,
    absl::string_view dns_server,
    std::unique_ptr<GrpcPolledFdFactory> polled_fd_factory,
    EventEngine* event_engine) ABSL_NO_THREAD_SAFETY_ANALYSIS {
  auto* request = new GrpcAresTXTRequest(
      name, timeout, std::move(polled_fd_factory), event_engine);
  absl::StatusOr<std::string> result = request->ParseNameToResolve();
  if (!result.ok()) {
    delete request;
    return result.status();
  }
  // Don't query for TXT records if the target is "localhost"
  if (absl::EqualsIgnoreCase(request->host(), "localhost")) {
    delete request;
    return absl::Status(absl::StatusCode::kUnknown,
                        "Skip querying for TXT records for localhost target");
  }
  absl::Status status = request->InitializeAresOptions(dns_server);
  if (!status.ok()) {
    delete request;
    return status;
  }
  return request;
}

}  // namespace experimental
}  // namespace grpc_event_engine

void noop_inject_channel_config(ares_channel /*channel*/) {}

void (*event_engine_grpc_ares_test_only_inject_config)(ares_channel channel) =
    noop_inject_channel_config;

#endif  // GRPC_ARES == 1

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

#include "src/core/lib/event_engine/ares_driver.h"

#include <netdb.h>
#include <string.h>

#include <algorithm>
#include <initializer_list>
#include <type_traits>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/types/optional.h"
#include "ares.h"

#include <grpc/event_engine/event_engine.h>

#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/event_engine/nameser.h"  // IWYU pragma: keep
#include "src/core/lib/event_engine/posix_engine/posix_engine_closure.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/examine_stack.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/iomgr/error.h"

#ifdef _WIN32
#else
#include <sys/ioctl.h>

#include "src/core/lib/event_engine/posix_engine/tcp_socket_utils.h"
#endif

namespace grpc_event_engine {
namespace experimental {

grpc_core::TraceFlag grpc_trace_cares_resolver_stacktrace(
    false, "cares_resolver_stacktrace");

#define GRPC_CARES_STACKTRACE()                                          \
  do {                                                                   \
    if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_cares_resolver_stacktrace)) { \
      absl::optional<std::string> stacktrace =                           \
          grpc_core::GetCurrentStackTrace();                             \
      if (stacktrace.has_value()) {                                      \
        gpr_log(GPR_DEBUG, "%s", stacktrace->c_str());                   \
      } else {                                                           \
        gpr_log(GPR_DEBUG, "stacktrace unavailable");                    \
      }                                                                  \
    }                                                                    \
  } while (0)

grpc_core::TraceFlag grpc_trace_cares_resolver(false, "cares_resolver");

#define GRPC_CARES_TRACE_LOG(format, ...)                           \
  do {                                                              \
    if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_cares_resolver)) {       \
      gpr_log(GPR_DEBUG, "(c-ares resolver) " format, __VA_ARGS__); \
    }                                                               \
  } while (0)

namespace {

bool is_fd_still_readable(int fd) {
  size_t bytes_available = 0;
  return ioctl(fd, FIONREAD, &bytes_available) == 0 && bytes_available > 0;
}

void on_hostbyname_done_locked(void* arg, int status, int /*timeouts*/,
                               struct hostent* hostent)
    ABSL_NO_THREAD_SAFETY_ANALYSIS {
  // This callback is invoked from the c-ares library, so disable thread safety
  // analysis. Note that we are guaranteed to be holding r->mu, though.
  GrpcAresHostnameRequest* request = static_cast<GrpcAresHostnameRequest*>(arg);
  if (status != ARES_SUCCESS) {
    std::string error_msg = absl::StrFormat(
        "C-ares status is not ARES_SUCCESS qtype=%s name=%s is_balancer=%d: %s",
        request->qtype(), request->host(), request->is_balancer(),
        ares_strerror(status));
    GRPC_CARES_TRACE_LOG("request:%p on_hostbyname_done_locked: %s", request,
                         error_msg.c_str());
    absl::Status error = GRPC_ERROR_CREATE(error_msg);
    // r->error = grpc_error_add_child(error, r->error);
    request->OnResolve(error);
    return;
  }
  GRPC_CARES_TRACE_LOG(
      "request:%p on_hostbyname_done_locked qtype=%s host=%s ARES_SUCCESS",
      request, request->qtype(), std::string(request->host()).c_str());
  GRPC_CARES_STACKTRACE();

  std::vector<EventEngine::ResolvedAddress> resolved_addresses;
  // TODO(yijiem): the old on_hostbyname_done_locked seems to allow collecting
  // both addresses and balancer_addresses before calling on_done in the same
  // request. But looks like in reality no one is doing so.

  for (size_t i = 0; hostent->h_addr_list[i] != nullptr; ++i) {
    // TODO(yijiem): how to return back this channel args?
    // grpc_core::ChannelArgs args;
    // if (hr->is_balancer) {
    //   args = args.Set(GRPC_ARG_DEFAULT_AUTHORITY, hr->host);
    // }
    switch (hostent->h_addrtype) {
      case AF_INET6: {
        size_t addr_len = sizeof(struct sockaddr_in6);
        struct sockaddr_in6 addr;
        memset(&addr, 0, addr_len);
        memcpy(&addr.sin6_addr, hostent->h_addr_list[i],
               sizeof(struct in6_addr));
        addr.sin6_family = static_cast<unsigned char>(hostent->h_addrtype);
        addr.sin6_port = request->port();
        resolved_addresses.emplace_back(
            reinterpret_cast<const sockaddr*>(&addr), addr_len);
        char output[INET6_ADDRSTRLEN];
        ares_inet_ntop(AF_INET6, &addr.sin6_addr, output, INET6_ADDRSTRLEN);
        GRPC_CARES_TRACE_LOG(
            "request:%p c-ares resolver gets a AF_INET6 result: \n"
            "  addr: %s\n  port: %d\n  sin6_scope_id: %d\n",
            request, output, ntohs(request->port()), addr.sin6_scope_id);
        break;
      }
      case AF_INET: {
        size_t addr_len = sizeof(struct sockaddr_in);
        struct sockaddr_in addr;
        memset(&addr, 0, addr_len);
        memcpy(&addr.sin_addr, hostent->h_addr_list[i], sizeof(struct in_addr));
        addr.sin_family = static_cast<unsigned char>(hostent->h_addrtype);
        addr.sin_port = request->port();
        resolved_addresses.emplace_back(
            reinterpret_cast<const sockaddr*>(&addr), addr_len);
        char output[INET_ADDRSTRLEN];
        ares_inet_ntop(AF_INET, &addr.sin_addr, output, INET_ADDRSTRLEN);
        GRPC_CARES_TRACE_LOG(
            "request:%p c-ares resolver gets a AF_INET result: \n"
            "  addr: %s\n  port: %d\n",
            request, output, ntohs(request->port()));
        break;
      }
    }
  }
  request->OnResolve(std::move(resolved_addresses));
}

void on_srv_query_done_locked(void* arg, int status, int /*timeouts*/,
                              unsigned char* abuf,
                              int alen) ABSL_NO_THREAD_SAFETY_ANALYSIS {
  // This callback is invoked from the c-ares library, so disable thread safety
  // analysis. Note that we are guaranteed to be holding r->mu, though.
  GrpcAresSRVRequest* r = static_cast<GrpcAresSRVRequest*>(arg);
  if (status != ARES_SUCCESS) {
    std::string error_msg = absl::StrFormat(
        "C-ares status is not ARES_SUCCESS qtype=SRV name=%s: %s",
        r->service_name(), ares_strerror(status));
    GRPC_CARES_TRACE_LOG("request:%p on_srv_query_done_locked: %s", r,
                         error_msg.c_str());
    grpc_error_handle error = GRPC_ERROR_CREATE(error_msg);
    // r->error = grpc_error_add_child(error, r->error);
    r->OnResolve(error);
    return;
  }
  GRPC_CARES_TRACE_LOG(
      "request:%p on_srv_query_done_locked name=%s ARES_SUCCESS", r,
      r->service_name());
  struct ares_srv_reply* reply = nullptr;
  const int parse_status = ares_parse_srv_reply(abuf, alen, &reply);
  GRPC_CARES_TRACE_LOG("request:%p ares_parse_srv_reply: %d", r, parse_status);
  std::vector<EventEngine::DNSResolver::SRVRecord> result;
  if (parse_status == ARES_SUCCESS) {
    for (struct ares_srv_reply* srv_it = reply; srv_it != nullptr;
         srv_it = srv_it->next) {
      result.emplace_back(srv_it->host, srv_it->port, srv_it->priority,
                          srv_it->weight);
    }
  }
  if (reply != nullptr) {
    ares_free_data(reply);
  }
  r->OnResolve(std::move(result));
}

const char g_service_config_attribute_prefix[] = "grpc_config=";

void on_txt_done_locked(void* arg, int status, int /*timeouts*/,
                        unsigned char* buf,
                        int len) ABSL_NO_THREAD_SAFETY_ANALYSIS {
  // This callback is invoked from the c-ares library, so disable thread safety
  // analysis. Note that we are guaranteed to be holding r->mu, though.
  GrpcAresTXTRequest* r = static_cast<GrpcAresTXTRequest*>(arg);
  const size_t prefix_len = sizeof(g_service_config_attribute_prefix) - 1;
  struct ares_txt_ext* result = nullptr;
  struct ares_txt_ext* reply = nullptr;
  grpc_error_handle error;
  if (status != ARES_SUCCESS) goto fail;
  GRPC_CARES_TRACE_LOG("request:%p on_txt_done_locked name=%s ARES_SUCCESS", r,
                       q->name().c_str());
  status = ares_parse_txt_reply_ext(buf, len, &reply);
  if (status != ARES_SUCCESS) goto fail;
  // Find service config in TXT record.
  for (result = reply; result != nullptr; result = result->next) {
    if (result->record_start &&
        memcmp(result->txt, g_service_config_attribute_prefix, prefix_len) ==
            0) {
      break;
    }
  }
  // Found a service config record.
  if (result != nullptr) {
    size_t service_config_len = result->length - prefix_len;
    *r->service_config_json_out =
        static_cast<char*>(gpr_malloc(service_config_len + 1));
    memcpy(*r->service_config_json_out, result->txt + prefix_len,
           service_config_len);
    for (result = result->next; result != nullptr && !result->record_start;
         result = result->next) {
      *r->service_config_json_out = static_cast<char*>(
          gpr_realloc(*r->service_config_json_out,
                      service_config_len + result->length + 1));
      memcpy(*r->service_config_json_out + service_config_len, result->txt,
             result->length);
      service_config_len += result->length;
    }
    (*r->service_config_json_out)[service_config_len] = '\0';
    GRPC_CARES_TRACE_LOG("request:%p found service config: %s", r,
                         *r->service_config_json_out);
  }
  // Clean up.
  ares_free_data(reply);
  grpc_ares_request_unref_locked(r);
  return;
fail:
  std::string error_msg =
      absl::StrFormat("C-ares status is not ARES_SUCCESS qtype=TXT name=%s: %s",
                      q->name(), ares_strerror(status));
  GRPC_CARES_TRACE_LOG("request:%p on_txt_done_locked %s", r,
                       error_msg.c_str());
  error = GRPC_ERROR_CREATE(error_msg);
  r->error = grpc_error_add_child(error, r->error);
}

}  // namespace

GrpcAresRequest::GrpcAresRequest(
    absl::string_view name, absl::optional<absl::string_view> default_port,
    EventEngine::Duration timeout,
    RegisterSocketWithPollerCallback register_socket_with_poller_cb,
    EventEngine* event_engine)
    : grpc_core::InternallyRefCounted<GrpcAresRequest>(
          GRPC_TRACE_FLAG_ENABLED(grpc_trace_cares_resolver) ? "GrpcAresRequest"
                                                             : nullptr),
      name_(name),
      default_port_(default_port.has_value() ? *default_port : ""),
      timeout_(timeout),
      register_socket_with_poller_cb_(
          std::move(register_socket_with_poller_cb)),
      event_engine_(event_engine) {}

GrpcAresRequest::~GrpcAresRequest() {
  if (initialized_) {
    ares_destroy(channel_);
    GRPC_CARES_TRACE_LOG("request:%p destructor", this);
    GRPC_CARES_STACKTRACE();
  }
}

absl::Status GrpcAresRequest::Initialize(absl::string_view dns_server,
                                         bool check_port) {
  absl::MutexLock lock(&mu_);
  GPR_ASSERT(!initialized_);
  absl::string_view port;
  GPR_ASSERT(grpc_core::SplitHostPort(name_, &host_, &port));
  absl::Status error;
  if (host_.empty()) {
    error =
        grpc_error_set_str(GRPC_ERROR_CREATE("unparseable host:port"),
                           grpc_core::StatusStrProperty::kTargetAddress, name_);
    return error;
  } else if (check_port && port.empty()) {
    if (default_port_.empty()) {
      error = grpc_error_set_str(GRPC_ERROR_CREATE("no port in name"),
                                 grpc_core::StatusStrProperty::kTargetAddress,
                                 name_);
      return error;
    }
    port = default_port_;
  }
  if (!port.empty()) {
    port_ = grpc_strhtons(std::string(port).c_str());
  }
  ares_options opts = {};
  opts.flags |= ARES_FLAG_STAYOPEN;
  int status = ares_init_options(&channel_, &opts, ARES_OPT_FLAGS);
  if (status != ARES_SUCCESS) {
    gpr_log(GPR_ERROR, "ares_init_options failed, status: %d", status);
    return GRPC_ERROR_CREATE(absl::StrCat(
        "Failed to init ares channel. C-ares error: ", ares_strerror(status)));
  }
  error = SetRequestDNSServer(dns_server);
  if (!error.ok()) {
    return error;
  }
  initialized_ = true;
  return absl::OkStatus();
}

void GrpcAresRequest::Cancel() {
  absl::MutexLock lock(&mu_);
  // TODO(yijiem): implement
  // TODO(yijiem): really need to add locking now
  if (!shutting_down_) {
    shutting_down_ = true;
    while (!fd_node_list_->IsEmpty()) {
      FdNodeList::FdNode* fd_node = fd_node_list_->PopFdNode();
      fd_node->handle()->ShutdownHandle(absl::OkStatus());
      PosixEngineClosure* on_handle_destroyed = new PosixEngineClosure(
          [self = Ref(DEBUG_LOCATION, "ares OnHandleDestroyed"),
           fd_node](absl::Status status) {
            self->OnHandleDestroyed(fd_node, status);
          },
          /*is_permanent=*/false);
      int release_fd = -1;
      fd_node->handle()->OrphanHandle(on_handle_destroyed, &release_fd,
                                      "no longer used by ares");
      GPR_ASSERT(release_fd == fd_node->WrappedFd());
    }
  }
}

void GrpcAresRequest::Orphan() {}

void GrpcAresRequest::Work() {
  std::unique_ptr<FdNodeList> new_list = std::make_unique<FdNodeList>();
  ares_socket_t socks[ARES_GETSOCK_MAXNUM];
  int socks_bitmask = ares_getsock(channel_, socks, ARES_GETSOCK_MAXNUM);
  for (size_t i = 0; i < ARES_GETSOCK_MAXNUM; i++) {
    if (ARES_GETSOCK_READABLE(socks_bitmask, i) ||
        ARES_GETSOCK_WRITABLE(socks_bitmask, i)) {
      FdNodeList::FdNode* fd_node = fd_node_list_->PopFdNode(socks[i]);
      if (fd_node == nullptr) {
        fd_node = new FdNodeList::FdNode(
            socks[i], std::move(register_socket_with_poller_cb_(socks[i])));
        GRPC_CARES_TRACE_LOG("request:%p new fd: %d", this,
                             fd_node->WrappedFd());
      }
      new_list->PushFdNode(fd_node);
      // Register read_closure if the socket is readable and read_closure has
      // not been registered with this socket.
      if (ARES_GETSOCK_READABLE(socks_bitmask, i) &&
          !fd_node->readable_registered()) {
        GRPC_CARES_TRACE_LOG("request:%p notify read on: %d", this,
                             fd_node->WrappedFd());
        PosixEngineClosure* on_read = new PosixEngineClosure(
            [self = Ref(DEBUG_LOCATION, "ares OnReadable"), fd_node](
                absl::Status status) { self->OnReadable(fd_node, status); },
            /*is_permanent=*/false);
        fd_node->handle()->NotifyOnRead(on_read);
        fd_node->set_readable_registered(true);
      }
      // Register write_closure if the socket is writable and write_closure
      // has not been registered with this socket.
      if (ARES_GETSOCK_WRITABLE(socks_bitmask, i) &&
          !fd_node->writable_registered()) {
        GRPC_CARES_TRACE_LOG("request:%p notify write on: %d", this,
                             fd_node->WrappedFd());
        PosixEngineClosure* on_write = new PosixEngineClosure(
            [self = Ref(DEBUG_LOCATION, "ares OnWritable"), fd_node](
                absl::Status status) { self->OnWritable(fd_node, status); },
            /*is_permanent=*/false);
        fd_node->handle()->NotifyOnWrite(on_write);
        fd_node->set_writable_registered(true);
      }
    }
  }
  // Any remaining fds in ev_driver->fds were not returned by ares_getsock()
  // and are therefore no longer in use, so they can be shut down and removed
  // from the list.
  while (!fd_node_list_->IsEmpty()) {
    FdNodeList::FdNode* fd_node = fd_node_list_->PopFdNode();
    // TODO(yijiem): shutdown the fd_node/handle from the poller
    if (!fd_node->readable_registered() && !fd_node->writable_registered()) {
      // TODO(yijiem): other destroy steps
      fd_node->handle()->ShutdownHandle(absl::OkStatus());
      PosixEngineClosure* on_handle_destroyed = new PosixEngineClosure(
          [self = Ref(DEBUG_LOCATION, "ares OnHandleDestroyed"),
           fd_node](absl::Status status) {
            self->OnHandleDestroyed(fd_node, status);
          },
          /*is_permanent=*/false);
      int release_fd = -1;
      fd_node->handle()->OrphanHandle(on_handle_destroyed, &release_fd,
                                      "no longer used by ares");
      GPR_ASSERT(release_fd == fd_node->WrappedFd());
    } else {
      new_list->PushFdNode(fd_node);
    }
  }
  fd_node_list_.swap(new_list);
}

absl::Status GrpcAresRequest::SetRequestDNSServer(
    absl::string_view dns_server) {
  if (!dns_server.empty()) {
    GRPC_CARES_TRACE_LOG("request:%p Using DNS server %s", this,
                         dns_server.data());
    grpc_resolved_address addr;
    if (grpc_parse_ipv4_hostport(dns_server, &addr, /*log_errors=*/false)) {
      dns_server_addr_.family = AF_INET;
      struct sockaddr_in* in = reinterpret_cast<struct sockaddr_in*>(addr.addr);
      memcpy(&dns_server_addr_.addr.addr4, &in->sin_addr,
             sizeof(struct in_addr));
      dns_server_addr_.tcp_port = grpc_sockaddr_get_port(&addr);
      dns_server_addr_.udp_port = grpc_sockaddr_get_port(&addr);
    } else if (grpc_parse_ipv6_hostport(dns_server, &addr,
                                        /*log_errors=*/false)) {
      dns_server_addr_.family = AF_INET6;
      struct sockaddr_in6* in6 =
          reinterpret_cast<struct sockaddr_in6*>(addr.addr);
      memcpy(&dns_server_addr_.addr.addr6, &in6->sin6_addr,
             sizeof(struct in6_addr));
      dns_server_addr_.tcp_port = grpc_sockaddr_get_port(&addr);
      dns_server_addr_.udp_port = grpc_sockaddr_get_port(&addr);
    } else {
      return GRPC_ERROR_CREATE(
          absl::StrCat("cannot parse authority ", dns_server));
    }
    int status = ares_set_servers_ports(channel_, &dns_server_addr_);
    if (status != ARES_SUCCESS) {
      return GRPC_ERROR_CREATE(absl::StrCat(
          "C-ares status is not ARES_SUCCESS: ", ares_strerror(status)));
    }
  }
  return absl::OkStatus();
}

void GrpcAresRequest::OnReadable(FdNodeList::FdNode* fd_node,
                                 absl::Status status) {
  absl::MutexLock lock(&mu_);
  GPR_ASSERT(fd_node->readable_registered());
  fd_node->set_readable_registered(false);
  GRPC_CARES_TRACE_LOG("request:%p %s readable on %d", this, ToString().c_str(),
                       fd_node->WrappedFd());
  GRPC_CARES_STACKTRACE();
  if (status.ok() && !shutting_down()) {
    do {
      ares_process_fd(channel_,
                      static_cast<ares_socket_t>(fd_node->WrappedFd()),
                      ARES_SOCKET_BAD);
    } while (is_fd_still_readable(fd_node->WrappedFd()));
  } else {
    // If error is not absl::OkStatus() or the resolution was cancelled, it
    // means the fd has been shutdown or timed out. The pending lookups made
    // on this ev_driver will be cancelled by the following ares_cancel() and
    // the on_done callbacks will be invoked with a status of ARES_ECANCELLED.
    // The remaining file descriptors in this ev_driver will be cleaned up in
    // the follwing grpc_ares_notify_on_event_locked().
    ares_cancel(channel_);
  }
  Work();
}

void GrpcAresRequest::OnWritable(FdNodeList::FdNode* fd_node,
                                 absl::Status status) {
  absl::MutexLock lock(&mu_);
  GPR_ASSERT(fd_node->writable_registered());
  fd_node->set_writable_registered(false);
  GRPC_CARES_TRACE_LOG("request:%p writable on %d", this, fd_node->WrappedFd());
  if (status.ok() && !shutting_down()) {
    ares_process_fd(channel_, ARES_SOCKET_BAD,
                    static_cast<ares_socket_t>(fd_node->WrappedFd()));
  } else {
    // If error is not absl::OkStatus() or the resolution was cancelled, it
    // means the fd has been shutdown or timed out. The pending lookups made
    // on this ev_driver will be cancelled by the following ares_cancel() and
    // the on_done callbacks will be invoked with a status of ARES_ECANCELLED.
    // The remaining file descriptors in this ev_driver will be cleaned up in
    // the follwing grpc_ares_notify_on_event_locked().
    ares_cancel(channel_);
  }
  Work();
}

void GrpcAresRequest::OnHandleDestroyed(FdNodeList::FdNode* fd_node,
                                        absl::Status status) {
  absl::MutexLock lock(&mu_);
  GPR_ASSERT(status.ok());
  GRPC_CARES_TRACE_LOG("request: %p OnDone for fd_node: %d", this,
                       fd_node->WrappedFd());
  GRPC_CARES_STACKTRACE();
  delete fd_node;
}

GrpcAresHostnameRequest::~GrpcAresHostnameRequest() {
  GRPC_CARES_TRACE_LOG("request:%p destructor", this);
}

void GrpcAresHostnameRequest::Start() {
  absl::MutexLock lock(&mu_);
  GPR_ASSERT(initialized_);
  // We add up pending_queries_ here since ares_gethostbyname may directly
  // invoke the callback inline if there is any error with the input. The
  // callback will invoke OnResolve with an error status and may destroy the
  // object too early (before the second ares_gethostbyname) if we haven't added
  // up here.
  pending_queries_++;
  // TODO(yijiem): factor out
  if (PosixSocketWrapper::IsIpv6LoopbackAvailable()) {
    pending_queries_++;
    ares_gethostbyname(channel_, std::string(host_).c_str(), AF_INET6,
                       &on_hostbyname_done_locked, static_cast<void*>(this));
  }
  // TODO(yijiem): set_request_dns_server if specified
  ares_gethostbyname(channel_, std::string(host_).c_str(), AF_INET,
                     &on_hostbyname_done_locked, static_cast<void*>(this));
  Work();
}

void GrpcAresHostnameRequest::OnResolve(absl::StatusOr<Result> result) {
  // TODO(yijiem): handle failure case
  GPR_ASSERT(pending_queries_ > 0);
  pending_queries_--;
  if (result.ok()) {
    result_.insert(result_.end(), result->begin(), result->end());
  } else {
    error_ = result.status();
  }
  if (pending_queries_ == 0) {
    // We mark the event driver as being shut down.
    // grpc_ares_notify_on_event_locked will shut down any remaining
    // fds.
    shutting_down_ = true;
    // TODO(yijiem): cancel timers here
    if (error_.ok()) {
      // TODO(yijiem): sort the addresses
      event_engine_->Run([on_resolve = std::move(on_resolve_),
                          result = std::move(result_),
                          token = reinterpret_cast<intptr_t>(this)]() mutable {
        on_resolve(std::move(result), token);
      });
    } else {
      // The reason that we are using EventEngine::Run() here is that we are
      // holding GrpcAresRequest::mu_ now and calling on_resolve will call into
      // the Engine to clean up some state there (which will take its own
      // mutex). The call could go further all the way back to the caller of the
      // Lookup* call which may then take its own mutex. This mutex order is
      // inverted from the order from which the caller calls into the
      // ares_driver. This could trigger absl::Mutex deadlock detection and TSAN
      // warning though it might be false positive.
      //
      // Another way might work is to std::move away on_resolve_, result_ or
      // error_ under lock, then unlock and then call on_resolve.
      event_engine_->Run([on_resolve = std::move(on_resolve_),
                          error = std::move(error_),
                          token = reinterpret_cast<intptr_t>(this)]() mutable {
        on_resolve(std::move(error), token);
      });
    }
    Unref(DEBUG_LOCATION, "shutting down");
  }
}

// TODO(yijiem): Don't query for SRV records if the target is "localhost"
void GrpcAresSRVRequest::Start() {
  absl::MutexLock lock(&mu_);
  GPR_ASSERT(initialized_);
  // Query the SRV record
  service_name_ = absl::StrCat("_grpclb._tcp.", host_);
  ares_query(channel_, service_name_.c_str(), ns_c_in, ns_t_srv,
             on_srv_query_done_locked, static_cast<void*>(this));
  Work();
}

void GrpcAresSRVRequest::OnResolve(absl::StatusOr<Result> result) {
  shutting_down_ = true;
  // TODO(yijiem): cancel timers here
  event_engine_->Run([on_resolve = std::move(on_resolve_),
                      result = std::move(result),
                      token = reinterpret_cast<intptr_t>(this)]() mutable {
    on_resolve(std::move(result), token);
  });
  Unref(DEBUG_LOCATION, "shutting down");
}

// TODO(yijiem): Don't query for TXT records if the target is "localhost"
void GrpcAresTXTRequest::Start() {
  absl::MutexLock lock(&mu_);
  GPR_ASSERT(initialized_);
  // Query the TXT record
  std::string config_name = absl::StrCat("_grpc_config.", host_);
  ares_search(channel_, config_name.c_str(), ns_c_in, ns_t_txt,
              on_txt_done_locked, static_cast<void*>(this));
  Work();
}

void GrpcAresTXTRequest::OnResolve(absl::StatusOr<Result> result) {}

}  // namespace experimental
}  // namespace grpc_event_engine

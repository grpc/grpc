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

#include <arpa/nameser.h>
#include <inttypes.h>
#include <netdb.h>
#include <string.h>

#include <algorithm>
#include <chrono>
#include <initializer_list>
#include <ratio>
#include <type_traits>
#include <utility>

#include <address_sorting/address_sorting.h>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/types/optional.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/event_engine/grpc_polled_fd.h"
#include "src/core/lib/event_engine/nameser.h"  // IWYU pragma: keep
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/examine_stack.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/resolved_address.h"
#include "src/core/lib/iomgr/sockaddr.h"
#ifdef _WIN32
#else
#include "src/core/lib/event_engine/posix_engine/tcp_socket_utils.h"
#endif

namespace grpc_event_engine {
namespace experimental {

grpc_core::TraceFlag grpc_trace_ares_driver_address_sorting(
    false, "ares_driver_address_sorting");

grpc_core::TraceFlag grpc_trace_ares_driver_stacktrace(
    false, "ares_driver_stacktrace");

#define GRPC_ARES_DRIVER_STACK_TRACE()                                \
  do {                                                                \
    if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_ares_driver_stacktrace)) { \
      absl::optional<std::string> stacktrace =                        \
          grpc_core::GetCurrentStackTrace();                          \
      if (stacktrace.has_value()) {                                   \
        gpr_log(GPR_DEBUG, "%s", stacktrace->c_str());                \
      } else {                                                        \
        gpr_log(GPR_DEBUG, "stacktrace unavailable");                 \
      }                                                               \
    }                                                                 \
  } while (0)

grpc_core::TraceFlag grpc_trace_ares_driver(false, "ares_driver");

#define GRPC_ARES_DRIVER_TRACE_LOG(format, ...)                 \
  do {                                                          \
    if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_ares_driver)) {      \
      gpr_log(GPR_DEBUG, "(ares driver) " format, __VA_ARGS__); \
    }                                                           \
  } while (0)

namespace {

struct HostbynameArg {
  GrpcAresHostnameRequest* request;
  const char* qtype;
};

void on_hostbyname_done_locked(void* arg, int status, int /*timeouts*/,
                               struct hostent* hostent)
    ABSL_NO_THREAD_SAFETY_ANALYSIS {
  // This callback is invoked from the c-ares library, so disable thread safety
  // analysis. Note that we are guaranteed to be holding r->mu, though.
  std::unique_ptr<HostbynameArg> harg(static_cast<HostbynameArg*>(arg));
  GrpcAresHostnameRequest* request = harg->request;
  if (status != ARES_SUCCESS) {
    std::string error_msg = absl::StrFormat(
        "C-ares status is not ARES_SUCCESS qtype=%s name=%s: %s", harg->qtype,
        request->host(), ares_strerror(status));
    GRPC_ARES_DRIVER_TRACE_LOG("request:%p on_hostbyname_done_locked: %s",
                               request, error_msg.c_str());
    GRPC_ARES_DRIVER_STACK_TRACE();
    absl::Status error = GRPC_ERROR_CREATE(error_msg);
    request->OnResolve(error);
    return;
  }
  GRPC_ARES_DRIVER_TRACE_LOG(
      "request:%p on_hostbyname_done_locked qtype=%s host=%s ARES_SUCCESS",
      request, harg->qtype, std::string(request->host()).c_str());
  GRPC_ARES_DRIVER_STACK_TRACE();

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
        addr.sin6_port = request->port();
        resolved_addresses.emplace_back(
            reinterpret_cast<const sockaddr*>(&addr), addr_len);
        char output[INET6_ADDRSTRLEN];
        ares_inet_ntop(AF_INET6, &addr.sin6_addr, output, INET6_ADDRSTRLEN);
        GRPC_ARES_DRIVER_TRACE_LOG(
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
        GRPC_ARES_DRIVER_TRACE_LOG(
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
    GRPC_ARES_DRIVER_TRACE_LOG("request:%p on_srv_query_done_locked: %s", r,
                               error_msg.c_str());
    grpc_error_handle error = GRPC_ERROR_CREATE(error_msg);
    r->OnResolve(error);
    return;
  }
  GRPC_ARES_DRIVER_TRACE_LOG(
      "request:%p on_srv_query_done_locked name=%s ARES_SUCCESS", r,
      r->service_name());
  struct ares_srv_reply* reply = nullptr;
  const int parse_status = ares_parse_srv_reply(abuf, alen, &reply);
  GRPC_ARES_DRIVER_TRACE_LOG("request:%p ares_parse_srv_reply: %d", r,
                             parse_status);
  std::vector<EventEngine::DNSResolver::SRVRecord> result;
  if (parse_status == ARES_SUCCESS) {
    for (struct ares_srv_reply* srv_it = reply; srv_it != nullptr;
         srv_it = srv_it->next) {
      result.push_back({.host = srv_it->host,
                        .port = srv_it->port,
                        .priority = srv_it->priority,
                        .weight = srv_it->weight});
    }
  }
  if (reply != nullptr) {
    ares_free_data(reply);
  }
  r->OnResolve(std::move(result));
}

constexpr char g_service_config_attribute_prefix[] = "grpc_config=";
constexpr size_t g_prefix_len = sizeof(g_service_config_attribute_prefix) - 1;

void on_txt_done_locked(void* arg, int status, int /*timeouts*/,
                        unsigned char* buf,
                        int len) ABSL_NO_THREAD_SAFETY_ANALYSIS {
  // This callback is invoked from the c-ares library, so disable thread safety
  // analysis. Note that we are guaranteed to be holding r->mu, though.
  GrpcAresTXTRequest* r = static_cast<GrpcAresTXTRequest*>(arg);
  struct ares_txt_ext* reply = nullptr;
  int parse_status = ARES_SUCCESS;
  if (status == ARES_SUCCESS) {
    GRPC_ARES_DRIVER_TRACE_LOG(
        "request:%p on_txt_done_locked name=%s ARES_SUCCESS", r,
        r->config_name());
    parse_status = ares_parse_txt_reply_ext(buf, len, &reply);
  }
  if (status != ARES_SUCCESS || parse_status != ARES_SUCCESS) {
    grpc_error_handle error;
    std::string error_msg = absl::StrFormat(
        "C-ares status is not ARES_SUCCESS qtype=TXT name=%s: %s",
        r->config_name(), ares_strerror(status));
    GRPC_ARES_DRIVER_TRACE_LOG("request:%p on_txt_done_locked %s", r,
                               error_msg.c_str());
    error = GRPC_ERROR_CREATE(error_msg);
    r->OnResolve(error);
    return;
  }
  // Find service config in TXT record.
  struct ares_txt_ext* result = nullptr;
  for (result = reply; result != nullptr; result = result->next) {
    if (result->record_start &&
        memcmp(result->txt, g_service_config_attribute_prefix, g_prefix_len) ==
            0) {
      break;
    }
  }
  std::string service_config_json_out;
  // Found a service config record.
  if (result != nullptr) {
    size_t service_config_len = result->length - g_prefix_len;
    service_config_json_out.append(
        reinterpret_cast<char*>(result->txt) + g_prefix_len,
        service_config_len);
    for (result = result->next; result != nullptr && !result->record_start;
         result = result->next) {
      service_config_json_out.append(reinterpret_cast<char*>(result->txt),
                                     result->length);
    }
    GRPC_ARES_DRIVER_TRACE_LOG("request:%p found service config: %s", r,
                               service_config_json_out.c_str());
  }
  // Clean up.
  ares_free_data(reply);
  r->OnResolve(std::move(service_config_json_out));
}

EventEngine::Duration calculate_next_ares_backup_poll_alarm_duration() {
  // An alternative here could be to use ares_timeout to try to be more
  // accurate, but that would require using "struct timeval"'s, which just makes
  // things a bit more complicated. So just poll every second, as suggested
  // by the c-ares code comments.
  return std::chrono::seconds(1);
}

bool IsIpv6LoopbackAvailable() {
#ifdef _WIN32
  // TODO(yijiem): (debt) move pieces for Windows
#else
  return PosixSocketWrapper::IsIpv6LoopbackAvailable();
#endif
}

}  // namespace

struct GrpcAresRequest::FdNode {
  FdNode() = default;
  explicit FdNode(ares_socket_t as, GrpcPolledFd* polled_fd)
      : as(as), polled_fd(polled_fd) {}
  ares_socket_t as;
  std::unique_ptr<GrpcPolledFd> polled_fd;
  // next fd node
  FdNode* next = nullptr;
  /// if the readable closure has been registered
  bool readable_registered = false;
  /// if the writable closure has been registered
  bool writable_registered = false;
  bool already_shutdown = false;
};

// TODO(yijiem): see if we can use std::list
// per ares-channel linked-list of FdNodes
class GrpcAresRequest::FdNodeList {
 public:
  class FdNodeListIterator {
   public:
    bool operator!=(const FdNodeListIterator& other) {
      return node_ != other.node_;
    }
    bool operator==(const FdNodeListIterator& other) {
      return node_ == other.node_;
    }
    FdNodeListIterator& operator++(int) {
      node_ = node_->next;
      return *this;
    }
    FdNode* operator*() { return node_; }
    static FdNodeListIterator universal_end() {
      return FdNodeListIterator(nullptr);
    }

   private:
    friend class FdNodeList;
    explicit FdNodeListIterator(FdNode* node) : node_(node) {}
    FdNode* node_;
  };
  using Iterator = FdNodeListIterator;

  FdNodeList() = default;
  ~FdNodeList() { GPR_ASSERT(IsEmpty()); }

  Iterator begin() { return Iterator(head_); }
  Iterator end() { return Iterator::universal_end(); }

  bool IsEmpty() const { return head_ == nullptr; }

  void PushFdNode(FdNode* fd_node)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&GrpcAresRequest::mu_) {
    fd_node->next = head_;
    head_ = fd_node;
  }

  FdNode* PopFdNode() ABSL_EXCLUSIVE_LOCKS_REQUIRED(&GrpcAresRequest::mu_) {
    GPR_ASSERT(!IsEmpty());
    FdNode* ret = head_;
    head_ = head_->next;
    return ret;
  }

  // Search for as in the FdNode list. This is an O(n) search, the max
  // possible value of n is ARES_GETSOCK_MAXNUM (16). n is typically 1 - 2
  // in our tests.
  FdNode* PopFdNode(ares_socket_t as)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&GrpcAresRequest::mu_) {
    FdNode phony_head;
    phony_head.next = head_;
    FdNode* node = &phony_head;
    while (node->next != nullptr) {
      if (node->next->polled_fd->GetWrappedAresSocketLocked() == as) {
        FdNode* ret = node->next;
        node->next = node->next->next;
        head_ = phony_head.next;
        return ret;
      }
      node = node->next;
    }
    return nullptr;
  }

 private:
  FdNode* head_ = nullptr;
};

GrpcAresRequest::GrpcAresRequest(
    absl::string_view name, absl::optional<absl::string_view> default_port,
    EventEngine::Duration timeout,
    RegisterAresSocketWithPollerCallback register_cb, EventEngine* event_engine)
    : grpc_core::RefCounted<GrpcAresRequest>(
          GRPC_TRACE_FLAG_ENABLED(grpc_trace_ares_driver) ? "GrpcAresRequest"
                                                          : nullptr),
      name_(name),
      default_port_(default_port.has_value() ? *default_port : ""),
      timeout_(timeout),
      fd_node_list_(std::make_unique<FdNodeList>()),
      event_engine_(event_engine),
      polled_fd_factory_(NewGrpcPolledFdFactory(std::move(register_cb), &mu_)) {
}

GrpcAresRequest::~GrpcAresRequest() {
  if (initialized_) {
    ares_destroy(channel_);
    GRPC_ARES_DRIVER_TRACE_LOG("request:%p destructor", this);
    GRPC_ARES_DRIVER_STACK_TRACE();
  }
}

absl::Status GrpcAresRequest::Initialize(absl::string_view dns_server,
                                         bool check_port) {
  absl::MutexLock lock(&mu_);
  GPR_DEBUG_ASSERT(!initialized_);
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
  ares_driver_test_only_inject_config(channel_);
  error = SetRequestDNSServer(dns_server);
  if (!error.ok()) {
    return error;
  }
  initialized_ = true;
  return absl::OkStatus();
}

void GrpcAresRequest::Cancel() {
  absl::MutexLock lock(&mu_);
  if (!shutting_down_) {
    shutting_down_ = true;
    cancelled_ = true;
    ShutdownPollerHandles();
  }
}

void GrpcAresRequest::Work() {
  std::unique_ptr<FdNodeList> new_list = std::make_unique<FdNodeList>();
  ares_socket_t socks[ARES_GETSOCK_MAXNUM];
  int socks_bitmask = ares_getsock(channel_, socks, ARES_GETSOCK_MAXNUM);
  for (size_t i = 0; i < ARES_GETSOCK_MAXNUM; i++) {
    if (ARES_GETSOCK_READABLE(socks_bitmask, i) ||
        ARES_GETSOCK_WRITABLE(socks_bitmask, i)) {
      FdNode* fd_node = fd_node_list_->PopFdNode(socks[i]);
      if (fd_node == nullptr) {
        fd_node = new FdNode(
            socks[i], polled_fd_factory_->NewGrpcPolledFdLocked(socks[i]));
        GRPC_ARES_DRIVER_TRACE_LOG("request:%p new fd: %d", this, fd_node->as);
      }
      new_list->PushFdNode(fd_node);
      // Register read_closure if the socket is readable and read_closure has
      // not been registered with this socket.
      if (ARES_GETSOCK_READABLE(socks_bitmask, i) &&
          !fd_node->readable_registered) {
        GRPC_ARES_DRIVER_TRACE_LOG("request:%p notify read on: %d", this,
                                   fd_node->as);
        fd_node->polled_fd->RegisterForOnReadableLocked(
            [self = Ref(DEBUG_LOCATION, "Work"), fd_node](absl::Status status) {
              self->OnReadable(fd_node, status);
            });
        fd_node->readable_registered = true;
      }
      // Register write_closure if the socket is writable and write_closure
      // has not been registered with this socket.
      if (ARES_GETSOCK_WRITABLE(socks_bitmask, i) &&
          !fd_node->writable_registered) {
        GRPC_ARES_DRIVER_TRACE_LOG("request:%p notify write on: %d", this,
                                   fd_node->as);
        fd_node->polled_fd->RegisterForOnWriteableLocked(
            [self = Ref(DEBUG_LOCATION, "Work"), fd_node](absl::Status status) {
              self->OnWritable(fd_node, status);
            });
        fd_node->writable_registered = true;
      }
    }
  }
  // Any remaining fds in ev_driver->fds were not returned by ares_getsock()
  // and are therefore no longer in use, so they can be shut down and removed
  // from the list.
  while (!fd_node_list_->IsEmpty()) {
    FdNode* fd_node = fd_node_list_->PopFdNode();
    if (!fd_node->already_shutdown) {
      fd_node->polled_fd->ShutdownLocked(absl::OkStatus());
      fd_node->already_shutdown = true;
    }
    if (!fd_node->readable_registered && !fd_node->writable_registered) {
      GRPC_ARES_DRIVER_TRACE_LOG("request: %p delete fd: %s", this,
                                 fd_node->polled_fd->GetName());
      delete fd_node;
    } else {
      new_list->PushFdNode(fd_node);
    }
  }
  fd_node_list_.swap(new_list);
}

void GrpcAresRequest::StartTimers() {
#define ToMillis(duration) \
  std::chrono::duration_cast<std::chrono::milliseconds>(duration).count()

  // Initialize overall DNS resolution timeout alarm
  EventEngine::Duration timeout =
      timeout_.count() == 0 ? EventEngine::Duration::max() : timeout_;
  GRPC_ARES_DRIVER_TRACE_LOG("request:%p StartTimers timeout in %" PRId64 " ms",
                             this, ToMillis(timeout));

  Ref(DEBUG_LOCATION, "StartTimers").release();
  query_timeout_handle_ = event_engine_->RunAfter(timeout, [this] {
    grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
    grpc_core::ExecCtx exec_ctx;
    OnQueryTimeout();
  });

  // Initialize the backup poll alarm
  EventEngine::Duration next_ares_backup_poll_alarm_duration =
      calculate_next_ares_backup_poll_alarm_duration();
  GRPC_ARES_DRIVER_TRACE_LOG(
      "request:%p StartTimers next ares process poll time in %" PRId64 " ms",
      this, ToMillis(next_ares_backup_poll_alarm_duration));

  Ref(DEBUG_LOCATION, "StartTimers").release();
  ares_backup_poll_alarm_handle_ =
      event_engine_->RunAfter(next_ares_backup_poll_alarm_duration, [this] {
        grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
        grpc_core::ExecCtx exec_ctx;
        OnAresBackupPollAlarm();
      });
}

void GrpcAresRequest::CancelTimers() {
  if (query_timeout_handle_.has_value()) {
    if (event_engine_->Cancel(*query_timeout_handle_)) {
      Unref(DEBUG_LOCATION, "CancelTimers");
    }
    query_timeout_handle_.reset();
  }
  if (ares_backup_poll_alarm_handle_.has_value()) {
    if (event_engine_->Cancel(*ares_backup_poll_alarm_handle_)) {
      Unref(DEBUG_LOCATION, "CancelTimers");
    }
    ares_backup_poll_alarm_handle_.reset();
  }
}

absl::Status GrpcAresRequest::SetRequestDNSServer(
    absl::string_view dns_server) {
  if (!dns_server.empty()) {
    GRPC_ARES_DRIVER_TRACE_LOG("request:%p Using DNS server %s", this,
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
      return GRPC_ERROR_CREATE(
          absl::StrCat("cannot parse authority ", dns_server));
    }
    // Prevent an uninitialized variable.
    dns_server_addr.next = nullptr;
    int status = ares_set_servers_ports(channel_, &dns_server_addr);
    if (status != ARES_SUCCESS) {
      return GRPC_ERROR_CREATE(absl::StrCat(
          "C-ares status is not ARES_SUCCESS: ", ares_strerror(status)));
    }
  }
  return absl::OkStatus();
}

void GrpcAresRequest::OnReadable(FdNode* fd_node, absl::Status status) {
  absl::MutexLock lock(&mu_);
  GPR_ASSERT(fd_node->readable_registered);
  fd_node->readable_registered = false;
  GRPC_ARES_DRIVER_TRACE_LOG("OnReadable: request: %p; fd: %d; status: %s",
                             this, fd_node->as, status.ToString().c_str());
  GRPC_ARES_DRIVER_STACK_TRACE();
  if (status.ok() && !shutting_down_) {
    do {
      ares_process_fd(channel_, static_cast<ares_socket_t>(fd_node->as),
                      ARES_SOCKET_BAD);
    } while (fd_node->polled_fd->IsFdStillReadableLocked());
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

void GrpcAresRequest::OnWritable(FdNode* fd_node, absl::Status status) {
  absl::MutexLock lock(&mu_);
  GPR_ASSERT(fd_node->writable_registered);
  fd_node->writable_registered = false;
  GRPC_ARES_DRIVER_TRACE_LOG("OnWritable: fd: %d; request:%p; status: %s",
                             fd_node->as, this, status.ToString().c_str());
  if (status.ok() && !shutting_down_) {
    ares_process_fd(channel_, ARES_SOCKET_BAD,
                    static_cast<ares_socket_t>(fd_node->as));
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

void GrpcAresRequest::OnQueryTimeout() {
  absl::MutexLock lock(&mu_);
  query_timeout_handle_.reset();
  GRPC_ARES_DRIVER_TRACE_LOG("request:%p OnQueryTimeout. shutting_down_=%d",
                             this, shutting_down_);
  if (!shutting_down_) {
    shutting_down_ = true;
    ShutdownPollerHandles();
  }
  Unref(DEBUG_LOCATION, "OnQueryTimeout");
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
  absl::MutexLock lock(&mu_);
  ares_backup_poll_alarm_handle_.reset();
  GRPC_ARES_DRIVER_TRACE_LOG(
      "request:%p OnAresBackupPollAlarm shutting_down=%d.", this,
      shutting_down_);
  if (!shutting_down_) {
    for (auto it = fd_node_list_->begin(); it != fd_node_list_->end(); it++) {
      if (!(*it)->already_shutdown) {
        GRPC_ARES_DRIVER_TRACE_LOG(
            "request:%p OnAresBackupPollAlarm; ares_process_fd. fd=%s", this,
            (*it)->polled_fd->GetName());
        ares_socket_t as = (*it)->polled_fd->GetWrappedAresSocketLocked();
        ares_process_fd(channel_, as, as);
      }
    }
    if (!shutting_down_) {
      EventEngine::Duration next_ares_backup_poll_alarm_duration =
          calculate_next_ares_backup_poll_alarm_duration();
      Ref(DEBUG_LOCATION, "OnAresBackupPollAlarm").release();
      ares_backup_poll_alarm_handle_ =
          event_engine_->RunAfter(next_ares_backup_poll_alarm_duration, [this] {
            grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
            grpc_core::ExecCtx exec_ctx;
            OnAresBackupPollAlarm();
          });
    }
    Work();
  }
  Unref(DEBUG_LOCATION, "OnAresBackupPollAlarm");
}

void GrpcAresRequest::ShutdownPollerHandles() {
  for (auto it = fd_node_list_->begin(); it != fd_node_list_->end(); it++) {
    if (!(*it)->already_shutdown) {
      (*it)->polled_fd->ShutdownLocked(
          grpc_core::StatusCreate(absl::StatusCode::kCancelled,
                                  "ShutdownPollerHandles", DEBUG_LOCATION, {}));
      (*it)->already_shutdown = true;
    }
  }
}

GrpcAresHostnameRequest::GrpcAresHostnameRequest(
    absl::string_view name, absl::string_view default_port,
    EventEngine::Duration timeout,
    RegisterAresSocketWithPollerCallback register_cb, EventEngine* event_engine)
    : GrpcAresRequest(name, default_port, timeout, std::move(register_cb),
                      event_engine) {}

GrpcAresHostnameRequest::~GrpcAresHostnameRequest() {
  GRPC_ARES_DRIVER_TRACE_LOG("request:%p destructor", this);
}

void GrpcAresHostnameRequest::Start(OnResolveCallback<Result> on_resolve) {
  auto self = Ref(DEBUG_LOCATION, "Start");
  absl::MutexLock lock(&mu_);
  GPR_DEBUG_ASSERT(initialized_);
  on_resolve_ = std::move(on_resolve);
  GRPC_ARES_DRIVER_TRACE_LOG(
      "request:%p c-ares GrpcAresHostnameRequest::Start name=%s, "
      "default_port=%s",
      this, name_.c_str(), default_port_.c_str());
  // Early out if the target is an ipv4 or ipv6 literal.
  if (ResolveAsIPLiteralLocked()) {
    return;
  }
  // TODO(yijiem): Early out if the target is localhost and we're on Windows.

  // We add up pending_queries_ here since ares_gethostbyname may directly
  // invoke the callback inline if there is any error with the input. The
  // callback will invoke OnResolve with an error status and may destroy the
  // object too early (before the second ares_gethostbyname) if we haven't added
  // up here.
  pending_queries_++;
  if (IsIpv6LoopbackAvailable()) {
    pending_queries_++;
    auto* arg = new HostbynameArg();
    arg->request = this;
    arg->qtype = "AAAA";
    ares_gethostbyname(channel_, std::string(host_).c_str(), AF_INET6,
                       &on_hostbyname_done_locked, static_cast<void*>(arg));
  }
  auto* arg = new HostbynameArg();
  arg->request = this;
  arg->qtype = "A";
  ares_gethostbyname(channel_, std::string(host_).c_str(), AF_INET,
                     &on_hostbyname_done_locked, static_cast<void*>(arg));
  // It's possible that ares_gethostbyname gets everything done inline.
  if (!shutting_down_) {
    Work();
    StartTimers();
  }
}

void GrpcAresHostnameRequest::OnResolve(absl::StatusOr<Result> result) {
  GPR_ASSERT(pending_queries_ > 0);
  pending_queries_--;
  if (result.ok()) {
    result_.insert(result_.end(), result->begin(), result->end());
  } else {
    error_ = grpc_error_add_child(error_, result.status());
  }
  if (pending_queries_ == 0) {
    class ScopedClosureRunner {
     private:
      using Closure = absl::AnyInvocable<void()>;

     public:
      explicit ScopedClosureRunner(Closure closure)
          : closure_(std::move(closure)) {}
      ~ScopedClosureRunner() { closure_(); }

     private:
      Closure closure_;
    };
    // Always Unref when goes out of this scope.
    ScopedClosureRunner cr([this] { Unref(DEBUG_LOCATION, "OnResolve"); });
    // We mark the event driver as being shut down.
    // grpc_ares_notify_on_event_locked will shut down any remaining
    // fds.
    shutting_down_ = true;
    CancelTimers();
    if (cancelled_) {
      // Cancel does not invoke on_resolve.
      return;
    }
    if (!result_.empty()) {
      // As long as there are records, we return them. Note that there might be
      // error_ from the other request too.
      SortResolvedAddresses();
      event_engine_->Run([on_resolve = std::move(on_resolve_),
                          result = std::move(result_),
                          token = reinterpret_cast<intptr_t>(this)]() mutable {
        on_resolve(std::move(result), token);
      });
      return;
    }
    GPR_ASSERT(!error_.ok());
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
}

bool GrpcAresHostnameRequest::ResolveAsIPLiteralLocked() {
  GPR_DEBUG_ASSERT(initialized_);
  // host_ and port_ should have been parsed successfully in Initialize.
  const std::string hostport = grpc_core::JoinHostPort(host_, ntohs(port_));
  // TODO(yijiem): maybe add ResolvedAddress version of these to
  // tcp_socket_utils.cc
  grpc_resolved_address addr;
  if (grpc_parse_ipv4_hostport(hostport, &addr, false /* log errors */) ||
      grpc_parse_ipv6_hostport(hostport, &addr, false /* log errors */)) {
    Result result;
    result.emplace_back(reinterpret_cast<sockaddr*>(addr.addr), addr.len);
    event_engine_->Run([on_resolve = std::move(on_resolve_),
                        result = std::move(result),
                        token = reinterpret_cast<intptr_t>(this)]() mutable {
      on_resolve(std::move(result), token);
    });
    return true;
  }
  return false;
}

void GrpcAresHostnameRequest::LogResolvedAddressesList(
    const char* input_output_str) {
  for (size_t i = 0; i < result_.size(); i++) {
    auto addr_str = ResolvedAddressToString(result_[i]);
    gpr_log(GPR_INFO,
            "(ares driver) request:%p c-ares address sorting: %s[%" PRIuPTR
            "]=%s",
            this, input_output_str, i,
            addr_str.ok() ? addr_str->c_str()
                          : addr_str.status().ToString().c_str());
  }
}

void GrpcAresHostnameRequest::SortResolvedAddresses() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_ares_driver_address_sorting)) {
    LogResolvedAddressesList("input");
  }
  address_sorting_sortable* sortables = static_cast<address_sorting_sortable*>(
      gpr_zalloc(sizeof(address_sorting_sortable) * result_.size()));
  for (size_t i = 0; i < result_.size(); i++) {
    sortables[i].user_data = &result_[i];
    memcpy(&sortables[i].dest_addr.addr, result_[i].address(),
           result_[i].size());
    sortables[i].dest_addr.len = result_[i].size();
  }
  address_sorting_rfc_6724_sort(sortables, result_.size());
  std::vector<EventEngine::ResolvedAddress> sorted;
  sorted.reserve(result_.size());
  for (size_t i = 0; i < result_.size(); i++) {
    sorted.emplace_back(
        *static_cast<EventEngine::ResolvedAddress*>(sortables[i].user_data));
  }
  gpr_free(sortables);
  result_ = std::move(sorted);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_ares_driver_address_sorting)) {
    LogResolvedAddressesList("output");
  }
}

GrpcAresSRVRequest::GrpcAresSRVRequest(
    absl::string_view name, EventEngine::Duration timeout,
    RegisterAresSocketWithPollerCallback register_cb, EventEngine* event_engine)
    : GrpcAresRequest(name, absl::nullopt, timeout, std::move(register_cb),
                      event_engine) {}

void GrpcAresSRVRequest::Start(OnResolveCallback<Result> on_resolve) {
  auto self = Ref(DEBUG_LOCATION, "Start");
  absl::MutexLock lock(&mu_);
  GPR_ASSERT(initialized_);
  // Don't query for SRV records if the target is "localhost"
  if (gpr_stricmp(std::string(host_).c_str(), "localhost") == 0) {
    event_engine_->Run([on_resolve = std::move(on_resolve),
                        token = reinterpret_cast<intptr_t>(this)]() mutable {
      on_resolve(GRPC_ERROR_CREATE(
                     "Skip querying for SRV records for localhost target"),
                 token);
    });
    return;
  }
  on_resolve_ = std::move(on_resolve);
  // Query the SRV record
  service_name_ = absl::StrCat("_grpclb._tcp.", host_);
  ares_query(channel_, service_name_.c_str(), ns_c_in, ns_t_srv,
             on_srv_query_done_locked, static_cast<void*>(this));
  if (!shutting_down_) {
    Work();
    StartTimers();
  }
}

void GrpcAresSRVRequest::OnResolve(absl::StatusOr<Result> result) {
  shutting_down_ = true;
  CancelTimers();
  event_engine_->Run([on_resolve = std::move(on_resolve_),
                      result = std::move(result),
                      token = reinterpret_cast<intptr_t>(this)]() mutable {
    on_resolve(std::move(result), token);
  });
  Unref(DEBUG_LOCATION, "OnResolve");
}

GrpcAresTXTRequest::GrpcAresTXTRequest(
    absl::string_view name, EventEngine::Duration timeout,
    RegisterAresSocketWithPollerCallback register_cb, EventEngine* event_engine)
    : GrpcAresRequest(name, absl::nullopt, timeout, std::move(register_cb),
                      event_engine) {}

void GrpcAresTXTRequest::Start(OnResolveCallback<Result> on_resolve) {
  auto self = Ref(DEBUG_LOCATION, "Start");
  absl::MutexLock lock(&mu_);
  GPR_ASSERT(initialized_);
  // Don't query for TXT records if the target is "localhost"
  if (gpr_stricmp(std::string(host_).c_str(), "localhost") == 0) {
    event_engine_->Run([on_resolve = std::move(on_resolve),
                        token = reinterpret_cast<intptr_t>(this)]() mutable {
      on_resolve(
          GRPC_ERROR_CREATE("Skip querying for TXT records localhost target"),
          token);
    });
    return;
  }
  on_resolve_ = std::move(on_resolve);
  // Query the TXT record
  config_name_ = absl::StrCat("_grpc_config.", host_);
  ares_search(channel_, config_name_.c_str(), ns_c_in, ns_t_txt,
              on_txt_done_locked, static_cast<void*>(this));
  if (!shutting_down_) {
    Work();
    StartTimers();
  }
}

void GrpcAresTXTRequest::OnResolve(absl::StatusOr<Result> result) {
  shutting_down_ = true;
  CancelTimers();
  event_engine_->Run([on_resolve = std::move(on_resolve_),
                      result = std::move(result),
                      token = reinterpret_cast<intptr_t>(this)]() mutable {
    on_resolve(std::move(result), token);
  });
  Unref(DEBUG_LOCATION, "OnResolve");
}

}  // namespace experimental
}  // namespace grpc_event_engine

void noop_inject_channel_config(ares_channel /*channel*/) {}

void (*ares_driver_test_only_inject_config)(ares_channel channel) =
    noop_inject_channel_config;

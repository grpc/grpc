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

#include "src/core/lib/event_engine/ares.h"

#include "ares.h"

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
  PosixEventEngine::PosixDNSResolver::GrpcAresHostnameRequest* request =
      static_cast<PosixEventEngine::PosixDNSResolver::GrpcAresHostnameRequest*>(
          arg);
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
      request, request->qtype(), request->host());
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

}  // namespace

GrpcAresHostnameRequest::GrpcAresHostnameRequest(
    absl::string_view host, uint16_t port, Duration timeout,
    LookupHostnameCallback on_resolve)
    : GrpcAresRequest(host, port, timeout), on_resolve_(std::move(on_resolve)) {
  if (PosixSocketWrapper::IsIpv6LoopbackAvailable()) {
    // TODO(yijiem): set_request_dns_server if specified
    pending_queries_++;
    ares_gethostbyname(channel, this->host(), AF_INET6,
                       &on_hostbyname_done_locked, static_cast<void*>(this));
  }
  // TODO(yijiem): set_request_dns_server if specified
  pending_queries_++;
  ares_gethostbyname(channel, this->host(), AF_INET, &on_hostbyname_done_locked,
                     static_cast<void*>(this));
}

void GrpcAresHostnameRequest::OnResolve(
    absl::StatusOr<std::vector<ResolvedAddress>> result)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
  // TODO(yijiem): handle failure case
  GPR_ASSERT(result.ok());
  GPR_ASSERT(pending_queries_ > 0);
  pending_queries_--;
  result_.insert(result_.end(), result->begin(), result->end());
  if (pending_queries_ == 0) {
    // TODO(yijiem): sort the addresses
    on_resolve_(std::move(result_));
  }
}

~GrpcAresRequest::GrpcAresRequest() {
  if (initialized_) {
    ares_destroy(channel_);
  }
}

bool GrpcAresRequest::Initialize() {
  GPR_ASSERT(!initialized_);
  ares_options opts = {};
  opts.flags |= ARES_FLAG_STAYOPEN;
  int status = ares_init_options(&channel_, &opts, ARES_OPT_FLAGS);
  if (status != ARES_SUCCESS) {
    gpr_log(GPR_ERROR, "ares_init_options failed, status: %d", status);
    return false;
  }
  initialized_ = true;
}

void GrpcAresRequest::Orphan() {
  // TODO(yijiem): implement
}

void GrpcAresRequest::Work() {
  std::unique_ptr<FdNodeList> new_list = std::make_unique<FdNodeList>();
  ares_socket_t socks[ARES_GETSOCK_MAXNUM];
  int socks_bitmask =
      ares_getsock(request->channel(), socks, ARES_GETSOCK_MAXNUM);
  for (size_t i = 0; i < ARES_GETSOCK_MAXNUM; i++) {
    if (ARES_GETSOCK_READABLE(socks_bitmask, i) ||
        ARES_GETSOCK_WRITABLE(socks_bitmask, i)) {
      FdNodeList::FdNode* fd_node =
          request->fd_node_list()->PopFdNode(socks[i]);
      if (fd_node == nullptr) {
        PosixEventPoller* poller = poller_manager_->Poller();
        GPR_DEBUG_ASSERT(poller != nullptr);
        EventHandle* handle =
            poller->CreateHandle(socks[i], "c-ares", poller->CanTrackErrors());
        fd_node = new FdNodeList::FdNode(socks[i], handle);
        GRPC_CARES_TRACE_LOG("request:%p new fd: %d", request,
                             fd_node->WrappedFd());
      }
      new_list->PushFdNode(fd_node);
      // Register read_closure if the socket is readable and read_closure has
      // not been registered with this socket.
      if (ARES_GETSOCK_READABLE(socks_bitmask, i) &&
          !fd_node->readable_registered()) {
        GRPC_CARES_TRACE_LOG("request:%p notify read on: %d", request,
                             fd_node->WrappedFd());
        PosixEngineClosure* on_read = new PosixEngineClosure(
            [this, fd_node, request](absl::Status status) {
              OnReadable(fd_node, request, status);
            },
            /*is_permanent=*/false);
        fd_node->event_handle()->NotifyOnRead(on_read);
        fd_node->set_readable_registered(true);
      }
      // Register write_closure if the socket is writable and write_closure
      // has not been registered with this socket.
      if (ARES_GETSOCK_WRITABLE(socks_bitmask, i) &&
          !fd_node->writable_registered()) {
        GRPC_CARES_TRACE_LOG("request:%p notify write on: %d", request,
                             fd_node->WrappedFd());
        PosixEngineClosure* on_write = new PosixEngineClosure(
            [this, fd_node, request](absl::Status status) {
              OnWritable(fd_node, request, status);
            },
            /*is_permanent=*/false);
        fd_node->event_handle()->NotifyOnWrite(on_write);
        fd_node->set_writable_registered(true);
      }
    }
  }
  // Any remaining fds in ev_driver->fds were not returned by ares_getsock()
  // and are therefore no longer in use, so they can be shut down and removed
  // from the list.
  while (!request->fd_node_list()->IsEmpty()) {
    FdNodeList::FdNode* fd_node = request->fd_node_list()->PopFdNode();
    // TODO(yijiem): shutdown the fd_node/handle from the poller
    if (!fd_node->readable_registered() && !fd_node->writable_registered()) {
      // TODO(yijiem): other destroy steps
      fd_node->event_handle()->ShutdownHandle(absl::OkStatus());
      PosixEngineClosure* on_handle_destroyed = new PosixEngineClosure(
          [this, fd_node, request](absl::Status status) {
            OnHandleDestroyed(fd_node, request, status);
          },
          /*is_permanent=*/false);
      int release_fd = -1;
      fd_node->event_handle()->OrphanHandle(on_handle_destroyed, &release_fd,
                                            "no longer used by ares");
      GPR_ASSERT(release_fd == fd_node->WrappedFd());
    } else {
      new_list->PushFdNode(fd_node);
    }
  }
  request->fd_node_list().swap(new_list);
}

void GrpcAresRequest::OnReadable(FdNodeList::FdNode* fd_node,
                                 absl::Status status) {
  GPR_ASSERT(fd_node->readable_registered());
  fd_node->set_readable_registered(false);
  GRPC_CARES_TRACE_LOG("request:%p %s readable on %d", request,
                       request->ToString().c_str(), fd_node->WrappedFd());
  GRPC_CARES_STACKTRACE();
  if (status.ok() && !request->shutting_down()) {
    do {
      ares_process_fd(request->channel(),
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
    ares_cancel(request->channel());
  }
  Work();
  // request->Unref();
}

void GrpcAresRequest::OnWritable(FdNodeList::FdNode* fd_node,
                                 absl::Status status) {
  GPR_ASSERT(fd_node->writable_registered());
  fd_node->set_writable_registered(false);
  GRPC_CARES_TRACE_LOG("request:%p writable on %d", request,
                       fd_node->WrappedFd());
  if (status.ok() && !request->shutting_down()) {
    ares_process_fd(request->channel(), ARES_SOCKET_BAD,
                    static_cast<ares_socket_t>(fd_node->WrappedFd()));
  } else {
    // If error is not absl::OkStatus() or the resolution was cancelled, it
    // means the fd has been shutdown or timed out. The pending lookups made
    // on this ev_driver will be cancelled by the following ares_cancel() and
    // the on_done callbacks will be invoked with a status of ARES_ECANCELLED.
    // The remaining file descriptors in this ev_driver will be cleaned up in
    // the follwing grpc_ares_notify_on_event_locked().
    ares_cancel(request->channel());
  }
  Work();
  // request->Unref();
}

void GrpcAresRequest::OnHandleDestroyed(FdNodeList::FdNode* fd_node,
                                        absl::Status status) {
  GPR_ASSERT(status.ok());
  // TODO(yijiem): destroy request
  GRPC_CARES_TRACE_LOG("request: %p OnDone for fd_node: %d", request,
                       fd_node->WrappedFd());
  GRPC_CARES_STACKTRACE();
  delete fd_node;
  // TODO(yijiem): revisit this
  // If request does not have active fd_nodes, considers it as complete and
  // frees its memory.
  if (request->fd_node_list()->IsEmpty()) {
    GRPC_CARES_TRACE_LOG(
        "request: %p has no active fd_node and appears done, freeing its "
        "memory",
        request);
    delete request;
  }
}

}  // namespace experimental
}  // namespace grpc_event_engine

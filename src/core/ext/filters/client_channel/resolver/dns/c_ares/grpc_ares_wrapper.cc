/*
 *
 * Copyright 2016 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <grpc/support/port_platform.h>

#if GRPC_ARES == 1 && !defined(GRPC_UV)

#include "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper.h"
#include "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper_internal.h"
#include "src/core/lib/iomgr/sockaddr.h"

#include <string.h>
#include <sys/types.h>

#include <ares.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>

#include <address_sorting/address_sorting.h>
#include "src/core/ext/filters/client_channel/parse_address.h"
#include "src/core/lib/gpr/host_port.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/iomgr/iomgr_internal.h"
#include "src/core/lib/iomgr/nameser.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/core/lib/iomgr/timer.h"

using grpc_core::GrpcPolledFd;
using grpc_core::GrpcPolledFdFactory;
using grpc_core::ServerAddress;
using grpc_core::ServerAddressList;

static gpr_once g_basic_init = GPR_ONCE_INIT;
static gpr_mu g_init_mu;

grpc_core::TraceFlag grpc_trace_cares_address_sorting(false,
                                                      "cares_address_sorting");

grpc_core::TraceFlag grpc_trace_cares_resolver(false, "cares_resolver");

class FdNode;
class FdNodeList;

struct grpc_ares_request {
  /** indicates the DNS server to use, if specified */
  struct ares_addr_port_node dns_server_addr;
  /** following members are set in grpc_resolve_address_ares_impl */
  /** closure to call when the request completes */
  grpc_closure* on_done = nullptr;
  /** the pointer to receive the resolved addresses */
  grpc_core::UniquePtr<grpc_core::ServerAddressList>* addresses_out = nullptr;
  /** the pointer to receive the service config in JSON */
  char** service_config_json_out = nullptr;
  /** number of ongoing queries */
  size_t pending_queries = 0;
  /** the ares_channel owned by this request */
  ares_channel channel;
  /** pollset set for driving the IO events of the channel */
  grpc_pollset_set* pollset_set = nullptr;
  /** This refcount is used both by logical queries and by I/O
   * or timeout callbacks. It protects only the completion of a
   * request, rather than the dtor directly. */
  gpr_refcount refs;
  /** combiner to synchronize c-ares and I/O callbacks on */
  grpc_combiner* combiner = nullptr;
  /** a list of grpc_fd that this request is currently using. */
  grpc_core::UniquePtr<FdNodeList> fds = grpc_core::MakeUnique<FdNodeList>();
  /** is this request currently working? */
  bool working = false;
  /** is this request being shut down */
  bool shutting_down = false;
  /** Creates new GrpcPolledFd's */
  grpc_core::UniquePtr<GrpcPolledFdFactory> polled_fd_factory = nullptr;
  /** query timeout in milliseconds */
  int query_timeout_ms;
  /** alarm to cancel active queries */
  grpc_timer query_timeout;
  /** cancels queries on a timeout */
  grpc_closure on_timeout_locked;

  /** is there at least one successful query, set in on_done_cb */
  bool success = false;
  /** the errors explaining the request failure, set in on_done_cb */
  grpc_error* error = GRPC_ERROR_NONE;
};

static grpc_ares_request* grpc_ares_request_internal_ref(grpc_ares_request* r,
                                                         const char* reason);

static void grpc_ares_request_internal_unref(grpc_ares_request* r,
                                             const char* reason);

static void grpc_ares_notify_on_event_locked(grpc_ares_request* request);

void grpc_ares_request_destroy(grpc_ares_request* r) {
  GRPC_CARES_TRACE_LOG("request:%p destroy", r);
  grpc_core::Delete(r);
}

class FdNode {
 public:
  explicit FdNode(grpc_core::UniquePtr<GrpcPolledFd> grpc_polled_fd)
      : grpc_polled_fd_(std::move(grpc_polled_fd)) {}

  ~FdNode() {
    GPR_ASSERT(!readable_registered_);
    GPR_ASSERT(!writeable_registered_);
    GPR_ASSERT(already_shutdown_);
  }

  struct ClosureArg {
   public:
    ClosureArg(FdNode* fdn, grpc_ares_request* r, const char* reason)
        : fdn(fdn), r(r), reason(reason) {
      grpc_ares_request_internal_ref(r, reason);
    }
    ~ClosureArg() {
      grpc_ares_notify_on_event_locked(r);
      grpc_ares_request_internal_unref(r, reason);
    }
    FdNode* fdn;
    grpc_ares_request* r;
    const char* reason;
  };

  static void OnReadableLocked(void* arg, grpc_error* error) {
    auto c = grpc_core::UniquePtr<ClosureArg>(static_cast<ClosureArg*>(arg));
    c->fdn->OnReadableInnerLocked(c->r, error);
  }

  static void OnWriteableLocked(void* arg, grpc_error* error) {
    auto c = grpc_core::UniquePtr<ClosureArg>(static_cast<ClosureArg*>(arg));
    c->fdn->OnWriteableInnerLocked(c->r, error);
  }

  void OnReadableInnerLocked(grpc_ares_request* r, grpc_error* error) {
    const ares_socket_t as = grpc_polled_fd_->GetWrappedAresSocketLocked();
    readable_registered_ = false;
    GRPC_CARES_TRACE_LOG("request:%p readable on %s", r, GetName());
    if (error == GRPC_ERROR_NONE) {
      do {
        ares_process_fd(r->channel, as, ARES_SOCKET_BAD);
      } while (grpc_polled_fd_->IsFdStillReadableLocked());
    } else {
      // If error is not GRPC_ERROR_NONE, it means the fd has been shutdown or
      // timed out. The pending lookups made on this request will be cancelled
      // by the following ares_cancel() and the on_done callbacks will be
      // invoked with a status of ARES_ECANCELLED. The remaining file
      // descriptors in this request will be cleaned up in the follwing
      // grpc_ares_notify_on_event_locked().
      ares_cancel(r->channel);
    }
  }

  void OnWriteableInnerLocked(grpc_ares_request* r, grpc_error* error) {
    const ares_socket_t as = grpc_polled_fd_->GetWrappedAresSocketLocked();
    writeable_registered_ = false;
    GRPC_CARES_TRACE_LOG("request:%p writable on %s", r, GetName());
    if (error == GRPC_ERROR_NONE) {
      ares_process_fd(r->channel, ARES_SOCKET_BAD, as);
    } else {
      // If error is not GRPC_ERROR_NONE, it means the fd has been shutdown or
      // timed out. The pending lookups made on this request will be cancelled
      // by the following ares_cancel() and the on_done callbacks will be
      // invoked with a status of ARES_ECANCELLED. The remaining file
      // descriptors in this request will be cleaned up in the follwing
      // grpc_ares_notify_on_event_locked().
      ares_cancel(r->channel);
    }
  }

  void MaybeRegisterForOnReadableLocked(grpc_ares_request* r,
                                        grpc_combiner* combiner) {
    if (!readable_registered_) {
      GRPC_CARES_TRACE_LOG("request:%p notify read on: %s", r, GetName());
      auto closure_arg = grpc_core::New<ClosureArg>(this, r, "on readbable");
      GRPC_CLOSURE_INIT(&read_closure_, OnReadableLocked, closure_arg,
                        grpc_combiner_scheduler(combiner));
      grpc_polled_fd_->RegisterForOnReadableLocked(&read_closure_);
      readable_registered_ = true;
    }
  }

  void MaybeRegisterForOnWriteableLocked(grpc_ares_request* r,
                                         grpc_combiner* combiner) {
    if (!writeable_registered_) {
      GRPC_CARES_TRACE_LOG("request:%p notify write on: %s", r, GetName());
      auto closure_arg = grpc_core::New<ClosureArg>(this, r, "on writeable");
      GRPC_CLOSURE_INIT(&write_closure_, OnWriteableLocked, closure_arg,
                        grpc_combiner_scheduler(combiner));
      grpc_polled_fd_->RegisterForOnWriteableLocked(&write_closure_);
      writeable_registered_ = true;
    }
  }

  void ShutdownLocked(const char* reason) {
    if (!already_shutdown_) {
      already_shutdown_ = true;
      grpc_polled_fd_->ShutdownLocked(
          GRPC_ERROR_CREATE_FROM_STATIC_STRING(reason));
    }
  }

  GrpcPolledFd* grpc_polled_fd() { return grpc_polled_fd_.get(); }

  const char* GetName() { return grpc_polled_fd_->GetName(); }

 protected:
  friend class FdNodeList;
  /** next fd node in the list */
  FdNode* next_ = nullptr;
  /** if the readable closure has been registered */
  bool readable_registered_ = false;
  /** if the writable closure has been registered */
  bool writeable_registered_ = false;

 private:
  /** a closure wrapping on_readable_locked, which should be
     invoked when the grpc_fd in this node becomes readable. */
  grpc_closure read_closure_;
  /** a closure wrapping on_writable_locked, which should be
     invoked when the grpc_fd in this node becomes writable. */
  grpc_closure write_closure_;
  /** wrapped fd that's polled by grpc's poller for the current platform */
  grpc_core::UniquePtr<GrpcPolledFd> grpc_polled_fd_;
  /** if the fd has been shutdown yet from grpc iomgr perspective */
  bool already_shutdown_ = false;
};

class FdNodeList {
 public:
  explicit FdNodeList(){};

  ~FdNodeList() { GPR_ASSERT(head_ == nullptr); }

  /** The list takes ownership over "fdn" */
  void Add(FdNode* fdn) {
    GPR_ASSERT(fdn->next_ == nullptr);
    fdn->next_ = head_;
    head_ = fdn;
  }

  /* Ownership of the result is given back to the caller */
  FdNode* Remove(ares_socket_t as) {
    FdNode** prev = &head_;
    FdNode* cur = head_;
    while (cur != nullptr) {
      if (cur->grpc_polled_fd()->GetWrappedAresSocketLocked() == as) {
        *prev = cur->next_;
        cur->next_ = nullptr;
        return cur;
      }
      prev = &cur->next_;
      cur = cur->next_;
    }
    return nullptr;
  }

  /* Initiates shutdown (if not done already) on all fd's in the list */
  void ShutdownAllFdsLocked(const char* reason) {
    GRPC_CARES_TRACE_LOG("ShutdownAllFdsLocked: %s. size:%zu", reason, size());
    FdNode* cur = head_;
    while (cur != nullptr) {
      cur->ShutdownLocked(reason);
      cur = cur->next_;
    }
  }

  /* Try to destroy as many fd nodes as possible */
  void DeleteInactiveLocked(const grpc_ares_request* r) {
    FdNode* new_head = nullptr;
    while (head_ != nullptr) {
      FdNode* cur = head_;
      head_ = head_->next_;
      if (cur->readable_registered_ || cur->writeable_registered_) {
        cur->next_ = new_head;
        new_head = cur;
      } else {
        GRPC_CARES_TRACE_LOG("request:%p delete fd: %s", r, cur->GetName());
        grpc_core::Delete(cur);
      }
    }
    head_ = new_head;
  }

  /* Ownership of all items in the list is given "dst" */
  void DumpLocked(FdNodeList* dst) {
    while (head_ != nullptr) {
      FdNode* cur = head_;
      head_ = head_->next_;
      cur->next_ = nullptr;
      dst->Add(cur);
    }
  }

  size_t size() {
    size_t out = 0;
    FdNode* cur = head_;
    while (cur != nullptr) {
      out++;
      cur = cur->next_;
    }
    return out;
  }

 private:
  FdNode* head_ = nullptr;
};

static grpc_ares_request* grpc_ares_request_internal_ref(grpc_ares_request* r,
                                                         const char* reason) {
  GRPC_CARES_TRACE_LOG("Ref request:%p. reason:%s", r, reason);
  gpr_ref(&r->refs);
  return r;
}

static void grpc_ares_request_internal_unref(grpc_ares_request* r,
                                             const char* reason) {
  GRPC_CARES_TRACE_LOG("Unref request:%p. reason:%s", r, reason);
  if (gpr_unref(&r->refs)) {
    GRPC_COMBINER_UNREF(r->combiner, "ares request complete");
    GPR_ASSERT(r->fds->size() == 0);
    ares_destroy(r->channel);
    ServerAddressList* addresses = r->addresses_out->get();
    if (addresses != nullptr) {
      grpc_cares_wrapper_address_sorting_sort(addresses);
    }
    GRPC_CLOSURE_SCHED(r->on_done, r->error);
  }
}

static void grpc_ares_request_queries_ref_locked(grpc_ares_request* r,
                                                 const char* reason) {
  r->pending_queries++;
  grpc_ares_request_internal_ref(r, reason);
}

static void grpc_ares_request_queries_unref_locked(grpc_ares_request* r,
                                                   const char* reason) {
  r->pending_queries--;
  if (r->pending_queries == 0u) {
    // We mark the request as being shut down. If the request
    // is working, grpc_ares_notify_on_event_locked will shut down the
    // fds; if it's not working, there are no fds to shut down.
    r->shutting_down = true;
    grpc_timer_cancel(&r->query_timeout);
  }
  grpc_ares_request_internal_unref(r, reason);
}

typedef struct grpc_ares_hostbyname_request {
  /** following members are set in create_hostbyname_request_locked
   */
  /** the top-level request instance */
  grpc_ares_request* parent_request;
  /** host to resolve, parsed from the name to resolve */
  char* host;
  /** port to fill in sockaddr_in, parsed from the name to resolve */
  uint16_t port;
  /** is it a grpclb address */
  bool is_balancer;
} grpc_ares_hostbyname_request;

void grpc_ares_request_shutdown_locked(grpc_ares_request* r) {
  r->shutting_down = true;
  r->fds->ShutdownAllFdsLocked("grpc_ares_request_shutdown_locked");
}

static void on_timeout_locked(void* arg, grpc_error* error) {
  grpc_ares_request* r = static_cast<grpc_ares_request*>(arg);
  GRPC_CARES_TRACE_LOG(
      "request:%p on_timeout_locked. request->shutting_down=%d. "
      "err=%s",
      r, r->shutting_down, grpc_error_string(error));
  if (!r->shutting_down && error == GRPC_ERROR_NONE) {
    grpc_ares_request_shutdown_locked(r);
  }
  grpc_ares_request_internal_unref(r, "on timeout");
}

// Get the file descriptors used by the request's ares channel, register
// callbacks with these filedescriptors.
static void grpc_ares_notify_on_event_locked(grpc_ares_request* r) {
  grpc_core::UniquePtr<FdNodeList> new_list =
      grpc_core::MakeUnique<FdNodeList>();
  if (!r->shutting_down) {
    ares_socket_t socks[ARES_GETSOCK_MAXNUM];
    int socks_bitmask = ares_getsock(r->channel, socks, ARES_GETSOCK_MAXNUM);
    for (size_t i = 0; i < ARES_GETSOCK_MAXNUM; i++) {
      if (ARES_GETSOCK_READABLE(socks_bitmask, i) ||
          ARES_GETSOCK_WRITABLE(socks_bitmask, i)) {
        FdNode* fdn = r->fds->Remove(socks[i]);
        // Create a new fd_node if sock[i] is not in the fd_node list.
        if (fdn == nullptr) {
          grpc_core::UniquePtr<GrpcPolledFd> grpc_polled_fd =
              grpc_core::UniquePtr<GrpcPolledFd>(
                  r->polled_fd_factory->NewGrpcPolledFdLocked(
                      socks[i], r->pollset_set, r->combiner));
          fdn = grpc_core::New<FdNode>(std::move(grpc_polled_fd));
          GRPC_CARES_TRACE_LOG("request:%p new fd: %s", r, fdn->GetName());
        }
        new_list->Add(fdn);
        // Register read_closure if the socket is readable and read_closure has
        // not been registered with this socket.
        if (ARES_GETSOCK_READABLE(socks_bitmask, i)) {
          fdn->MaybeRegisterForOnReadableLocked(r, r->combiner);
        }
        // Register write_closure if the socket is writable and write_closure
        // has not been registered with this socket.
        if (ARES_GETSOCK_WRITABLE(socks_bitmask, i)) {
          fdn->MaybeRegisterForOnWriteableLocked(r, r->combiner);
        }
      }
    }
  }
  // Any remaining fds in request->fds were not returned by ares_getsock() and
  // are therefore no longer in use, so they can be shut down and removed from
  // the list.
  r->fds->ShutdownAllFdsLocked("c-ares fd shutdown");
  r->fds->DeleteInactiveLocked(r);
  r->fds->DumpLocked(new_list.get());
  r->fds = std::move(new_list);
  // If the request has no working fd, all the tasks are done.
  if (r->fds->size() == 0) {
    r->working = false;
    GRPC_CARES_TRACE_LOG("request:%p request stop working", r);
  }
}

static void do_basic_init(void) { gpr_mu_init(&g_init_mu); }

static void log_address_sorting_list(const ServerAddressList& addresses,
                                     const char* input_output_str) {
  for (size_t i = 0; i < addresses.size(); i++) {
    char* addr_str;
    if (grpc_sockaddr_to_string(&addr_str, &addresses[i].address(), true)) {
      gpr_log(GPR_INFO, "c-ares address sorting: %s[%" PRIuPTR "]=%s",
              input_output_str, i, addr_str);
      gpr_free(addr_str);
    } else {
      gpr_log(GPR_INFO,
              "c-ares address sorting: %s[%" PRIuPTR "]=<unprintable>",
              input_output_str, i);
    }
  }
}

void grpc_cares_wrapper_address_sorting_sort(ServerAddressList* addresses) {
  if (grpc_trace_cares_address_sorting.enabled()) {
    log_address_sorting_list(*addresses, "input");
  }
  address_sorting_sortable* sortables = (address_sorting_sortable*)gpr_zalloc(
      sizeof(address_sorting_sortable) * addresses->size());
  for (size_t i = 0; i < addresses->size(); ++i) {
    sortables[i].user_data = &(*addresses)[i];
    memcpy(&sortables[i].dest_addr.addr, &(*addresses)[i].address().addr,
           (*addresses)[i].address().len);
    sortables[i].dest_addr.len = (*addresses)[i].address().len;
  }
  address_sorting_rfc_6724_sort(sortables, addresses->size());
  ServerAddressList sorted;
  sorted.reserve(addresses->size());
  for (size_t i = 0; i < addresses->size(); ++i) {
    sorted.emplace_back(*static_cast<ServerAddress*>(sortables[i].user_data));
  }
  gpr_free(sortables);
  *addresses = std::move(sorted);
  if (grpc_trace_cares_address_sorting.enabled()) {
    log_address_sorting_list(*addresses, "output");
  }
}

static grpc_ares_hostbyname_request* create_hostbyname_request_locked(
    grpc_ares_request* parent_request, char* host, uint16_t port,
    bool is_balancer) {
  grpc_ares_hostbyname_request* hr = static_cast<grpc_ares_hostbyname_request*>(
      gpr_zalloc(sizeof(grpc_ares_hostbyname_request)));
  hr->parent_request = parent_request;
  hr->host = gpr_strdup(host);
  hr->port = port;
  hr->is_balancer = is_balancer;
  grpc_ares_request_queries_ref_locked(parent_request,
                                       "create_hostbyname_request_locked");
  return hr;
}

static void destroy_hostbyname_request_locked(
    grpc_ares_hostbyname_request* hr) {
  grpc_ares_request_queries_unref_locked(hr->parent_request,
                                         "destroy_hostbyname_request_locked");
  gpr_free(hr->host);
  gpr_free(hr);
}

static void on_hostbyname_done_locked(void* arg, int status, int timeouts,
                                      struct hostent* hostent) {
  grpc_ares_hostbyname_request* hr =
      static_cast<grpc_ares_hostbyname_request*>(arg);
  grpc_ares_request* r = hr->parent_request;
  if (status == ARES_SUCCESS) {
    GRPC_ERROR_UNREF(r->error);
    r->error = GRPC_ERROR_NONE;
    r->success = true;
    if (*r->addresses_out == nullptr) {
      *r->addresses_out = grpc_core::MakeUnique<ServerAddressList>();
    }
    ServerAddressList& addresses = **r->addresses_out;
    for (size_t i = 0; hostent->h_addr_list[i] != nullptr; ++i) {
      grpc_core::InlinedVector<grpc_arg, 2> args_to_add;
      if (hr->is_balancer) {
        args_to_add.emplace_back(grpc_channel_arg_integer_create(
            const_cast<char*>(GRPC_ARG_ADDRESS_IS_BALANCER), 1));
        args_to_add.emplace_back(grpc_channel_arg_string_create(
            const_cast<char*>(GRPC_ARG_ADDRESS_BALANCER_NAME), hr->host));
      }
      grpc_channel_args* args = grpc_channel_args_copy_and_add(
          nullptr, args_to_add.data(), args_to_add.size());
      switch (hostent->h_addrtype) {
        case AF_INET6: {
          size_t addr_len = sizeof(struct sockaddr_in6);
          struct sockaddr_in6 addr;
          memset(&addr, 0, addr_len);
          memcpy(&addr.sin6_addr, hostent->h_addr_list[i],
                 sizeof(struct in6_addr));
          addr.sin6_family = static_cast<unsigned char>(hostent->h_addrtype);
          addr.sin6_port = hr->port;
          addresses.emplace_back(&addr, addr_len, args);
          char output[INET6_ADDRSTRLEN];
          ares_inet_ntop(AF_INET6, &addr.sin6_addr, output, INET6_ADDRSTRLEN);
          GRPC_CARES_TRACE_LOG(
              "request:%p c-ares resolver gets a AF_INET6 result: \n"
              "  addr: %s\n  port: %d\n  sin6_scope_id: %d\n",
              r, output, ntohs(hr->port), addr.sin6_scope_id);
          break;
        }
        case AF_INET: {
          size_t addr_len = sizeof(struct sockaddr_in);
          struct sockaddr_in addr;
          memset(&addr, 0, addr_len);
          memcpy(&addr.sin_addr, hostent->h_addr_list[i],
                 sizeof(struct in_addr));
          addr.sin_family = static_cast<unsigned char>(hostent->h_addrtype);
          addr.sin_port = hr->port;
          addresses.emplace_back(&addr, addr_len, args);
          char output[INET_ADDRSTRLEN];
          ares_inet_ntop(AF_INET, &addr.sin_addr, output, INET_ADDRSTRLEN);
          GRPC_CARES_TRACE_LOG(
              "request:%p c-ares resolver gets a AF_INET result: \n"
              "  addr: %s\n  port: %d\n",
              r, output, ntohs(hr->port));
          break;
        }
      }
    }
  } else if (!r->success) {
    char* error_msg;
    gpr_asprintf(&error_msg, "C-ares status is not ARES_SUCCESS: %s",
                 ares_strerror(status));
    grpc_error* error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(error_msg);
    gpr_free(error_msg);
    if (r->error == GRPC_ERROR_NONE) {
      r->error = error;
    } else {
      r->error = grpc_error_add_child(error, r->error);
    }
  }
  destroy_hostbyname_request_locked(hr);
}

void grpc_ares_request_start_working_locked(grpc_ares_request* r) {
  if (!r->working) {
    r->working = true;
    grpc_ares_notify_on_event_locked(r);
  }
}

static void on_srv_query_done_locked(void* arg, int status, int timeouts,
                                     unsigned char* abuf, int alen) {
  grpc_ares_request* r = static_cast<grpc_ares_request*>(arg);
  GRPC_CARES_TRACE_LOG("request:%p on_query_srv_done_locked", r);
  if (status == ARES_SUCCESS) {
    GRPC_CARES_TRACE_LOG("request:%p on_query_srv_done_locked ARES_SUCCESS", r);
    struct ares_srv_reply* reply;
    const int parse_status = ares_parse_srv_reply(abuf, alen, &reply);
    if (parse_status == ARES_SUCCESS) {
      for (struct ares_srv_reply* srv_it = reply; srv_it != nullptr;
           srv_it = srv_it->next) {
        if (grpc_ares_query_ipv6()) {
          grpc_ares_hostbyname_request* hr = create_hostbyname_request_locked(
              r, srv_it->host, htons(srv_it->port), true /* is_balancer */);
          ares_gethostbyname(r->channel, hr->host, AF_INET6,
                             on_hostbyname_done_locked, hr);
        }
        grpc_ares_hostbyname_request* hr = create_hostbyname_request_locked(
            r, srv_it->host, htons(srv_it->port), true /* is_balancer */);
        ares_gethostbyname(r->channel, hr->host, AF_INET,
                           on_hostbyname_done_locked, hr);
        grpc_ares_request_start_working_locked(r);
      }
    }
    if (reply != nullptr) {
      ares_free_data(reply);
    }
  } else if (!r->success) {
    char* error_msg;
    gpr_asprintf(&error_msg, "C-ares status is not ARES_SUCCESS: %s",
                 ares_strerror(status));
    grpc_error* error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(error_msg);
    gpr_free(error_msg);
    if (r->error == GRPC_ERROR_NONE) {
      r->error = error;
    } else {
      r->error = grpc_error_add_child(error, r->error);
    }
  }
  grpc_ares_request_queries_unref_locked(r, "on_srv_query_done_locked");
}

static const char g_service_config_attribute_prefix[] = "grpc_config=";

static void on_txt_done_locked(void* arg, int status, int timeouts,
                               unsigned char* buf, int len) {
  char* error_msg;
  grpc_ares_request* r = static_cast<grpc_ares_request*>(arg);
  GRPC_CARES_TRACE_LOG("request:%p on_txt_done_locked", r);
  const size_t prefix_len = sizeof(g_service_config_attribute_prefix) - 1;
  struct ares_txt_ext* result = nullptr;
  struct ares_txt_ext* reply = nullptr;
  grpc_error* error = GRPC_ERROR_NONE;
  if (status != ARES_SUCCESS) goto fail;
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
  goto done;
fail:
  gpr_asprintf(&error_msg, "C-ares TXT lookup status is not ARES_SUCCESS: %s",
               ares_strerror(status));
  error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(error_msg);
  gpr_free(error_msg);
  if (r->error == GRPC_ERROR_NONE) {
    r->error = error;
  } else {
    r->error = grpc_error_add_child(error, r->error);
  }
done:
  grpc_ares_request_queries_unref_locked(r, "on_txt_done_locked");
}

void grpc_dns_lookup_ares_continue_after_check_localhost_and_ip_literals_locked(
    grpc_ares_request* r, const char* dns_server, const char* name,
    const char* default_port, grpc_pollset_set* interested_parties,
    bool check_grpclb, int query_timeout_ms, grpc_combiner* combiner) {
  grpc_ares_hostbyname_request* hr = nullptr;
  /* parse name, splitting it into host and port parts */
  char* host;
  char* port;
  gpr_split_host_port(name, &host, &port);
  if (host == nullptr) {
    r->error = grpc_error_set_str(
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("unparseable host:port"),
        GRPC_ERROR_STR_TARGET_ADDRESS, grpc_slice_from_copied_string(name));
    goto error_cleanup;
  } else if (port == nullptr) {
    if (default_port == nullptr) {
      r->error = grpc_error_set_str(
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("no port in name"),
          GRPC_ERROR_STR_TARGET_ADDRESS, grpc_slice_from_copied_string(name));
      goto error_cleanup;
    }
    port = gpr_strdup(default_port);
  }
  // If dns_server is specified, use it.
  if (dns_server != nullptr) {
    GRPC_CARES_TRACE_LOG("request:%p Using DNS server %s", r, dns_server);
    grpc_resolved_address addr;
    if (grpc_parse_ipv4_hostport(dns_server, &addr, false /* log_errors */)) {
      r->dns_server_addr.family = AF_INET;
      struct sockaddr_in* in = reinterpret_cast<struct sockaddr_in*>(addr.addr);
      memcpy(&r->dns_server_addr.addr.addr4, &in->sin_addr,
             sizeof(struct in_addr));
      r->dns_server_addr.tcp_port = grpc_sockaddr_get_port(&addr);
      r->dns_server_addr.udp_port = grpc_sockaddr_get_port(&addr);
    } else if (grpc_parse_ipv6_hostport(dns_server, &addr,
                                        false /* log_errors */)) {
      r->dns_server_addr.family = AF_INET6;
      struct sockaddr_in6* in6 =
          reinterpret_cast<struct sockaddr_in6*>(addr.addr);
      memcpy(&r->dns_server_addr.addr.addr6, &in6->sin6_addr,
             sizeof(struct in6_addr));
      r->dns_server_addr.tcp_port = grpc_sockaddr_get_port(&addr);
      r->dns_server_addr.udp_port = grpc_sockaddr_get_port(&addr);
    } else {
      r->error = grpc_error_set_str(
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("cannot parse authority"),
          GRPC_ERROR_STR_TARGET_ADDRESS, grpc_slice_from_copied_string(name));
      goto error_cleanup;
    }
    int status = ares_set_servers_ports(r->channel, &r->dns_server_addr);
    if (status != ARES_SUCCESS) {
      char* error_msg;
      gpr_asprintf(&error_msg, "C-ares status is not ARES_SUCCESS: %s",
                   ares_strerror(status));
      r->error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(error_msg);
      gpr_free(error_msg);
      goto error_cleanup;
    }
  }
  if (grpc_ares_query_ipv6()) {
    hr = create_hostbyname_request_locked(r, host, grpc_strhtons(port),
                                          false /* is_balancer */);
    ares_gethostbyname(r->channel, hr->host, AF_INET6,
                       on_hostbyname_done_locked, hr);
  }
  hr = create_hostbyname_request_locked(r, host, grpc_strhtons(port),
                                        false /* is_balancer */);
  ares_gethostbyname(r->channel, hr->host, AF_INET, on_hostbyname_done_locked,
                     hr);
  if (check_grpclb) {
    /* Query the SRV record */
    grpc_ares_request_queries_ref_locked(r, "query SRV record");
    char* service_name;
    gpr_asprintf(&service_name, "_grpclb._tcp.%s", host);
    ares_query(r->channel, service_name, ns_c_in, ns_t_srv,
               on_srv_query_done_locked, r);
    gpr_free(service_name);
  }
  if (r->service_config_json_out != nullptr) {
    grpc_ares_request_queries_ref_locked(r, "query TXT record");
    char* config_name;
    gpr_asprintf(&config_name, "_grpc_config.%s", host);
    ares_search(r->channel, config_name, ns_c_in, ns_t_txt, on_txt_done_locked,
                r);
    gpr_free(config_name);
  }
  grpc_ares_request_start_working_locked(r);
  gpr_free(host);
  gpr_free(port);
  return;

error_cleanup:
  gpr_free(host);
  gpr_free(port);
}

static bool inner_resolve_as_ip_literal_locked(
    const char* name, const char* default_port,
    grpc_core::UniquePtr<grpc_core::ServerAddressList>* addrs, char** host,
    char** port, char** hostport) {
  gpr_split_host_port(name, host, port);
  if (*host == nullptr) {
    gpr_log(GPR_ERROR,
            "Failed to parse %s to host:port while attempting to resolve as ip "
            "literal.",
            name);
    return false;
  }
  if (*port == nullptr) {
    if (default_port == nullptr) {
      gpr_log(GPR_ERROR,
              "No port or default port for %s while attempting to resolve as "
              "ip literal.",
              name);
      return false;
    }
    *port = gpr_strdup(default_port);
  }
  grpc_resolved_address addr;
  GPR_ASSERT(gpr_join_host_port(hostport, *host, atoi(*port)));
  if (grpc_parse_ipv4_hostport(*hostport, &addr, false /* log errors */) ||
      grpc_parse_ipv6_hostport(*hostport, &addr, false /* log errors */)) {
    GPR_ASSERT(*addrs == nullptr);
    *addrs = grpc_core::MakeUnique<ServerAddressList>();
    (*addrs)->emplace_back(addr.addr, addr.len, nullptr /* args */);
    return true;
  }
  return false;
}

static bool resolve_as_ip_literal_locked(
    const char* name, const char* default_port,
    grpc_core::UniquePtr<grpc_core::ServerAddressList>* addrs) {
  char* host = nullptr;
  char* port = nullptr;
  char* hostport = nullptr;
  bool out = inner_resolve_as_ip_literal_locked(name, default_port, addrs,
                                                &host, &port, &hostport);
  gpr_free(host);
  gpr_free(port);
  gpr_free(hostport);
  return out;
}

static bool target_matches_localhost_inner(const char* name, char** host,
                                           char** port) {
  if (!gpr_split_host_port(name, host, port)) {
    gpr_log(GPR_ERROR, "Unable to split host and port for name: %s", name);
    return false;
  }
  if (gpr_stricmp(*host, "localhost") == 0) {
    return true;
  } else {
    return false;
  }
}

static bool target_matches_localhost(const char* name) {
  char* host = nullptr;
  char* port = nullptr;
  bool out = target_matches_localhost_inner(name, &host, &port);
  gpr_free(host);
  gpr_free(port);
  return out;
}

static grpc_ares_request* grpc_dns_lookup_ares_locked_impl(
    const char* dns_server, const char* name, const char* default_port,
    grpc_pollset_set* interested_parties, grpc_closure* on_done,
    grpc_core::UniquePtr<grpc_core::ServerAddressList>* addrs,
    bool check_grpclb, char** service_config_json, int query_timeout_ms,
    grpc_combiner* combiner) {
  grpc_ares_request* r = grpc_core::New<grpc_ares_request>();
  r->on_done = on_done;
  r->addresses_out = addrs;
  r->service_config_json_out = service_config_json;
  r->fds = grpc_core::MakeUnique<FdNodeList>();
  r->combiner = GRPC_COMBINER_REF(combiner, "ares request");
  r->pollset_set = interested_parties;
  r->polled_fd_factory = grpc_core::NewGrpcPolledFdFactory(r->combiner);
  GRPC_CLOSURE_INIT(&r->on_timeout_locked, on_timeout_locked, r,
                    grpc_combiner_scheduler(combiner));
  r->query_timeout_ms = query_timeout_ms;
  gpr_ref_init(&r->refs, 0);
  grpc_ares_request_queries_ref_locked(r, "initialize lookup");
  ares_options opts;
  memset(&opts, 0, sizeof(opts));
  opts.flags |= ARES_FLAG_STAYOPEN;
  int status = ares_init_options(&r->channel, &opts, ARES_OPT_FLAGS);
  if (status != ARES_SUCCESS) {
    char* err_msg;
    gpr_asprintf(&err_msg, "Failed to init ares channel. C-ares error: %s",
                 ares_strerror(status));
    r->error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(err_msg);
    gpr_free(err_msg);
    grpc_ares_request_queries_unref_locked(r, "failed to init ares channel");
    return r;
  }
  r->polled_fd_factory->ConfigureAresChannelLocked(r->channel);
  GRPC_CARES_TRACE_LOG(
      "request:%p c-ares grpc_dns_lookup_ares_locked_impl name=%s, "
      "default_port=%s",
      r, name, default_port);
  grpc_millis timeout =
      r->query_timeout_ms == 0
          ? GRPC_MILLIS_INF_FUTURE
          : r->query_timeout_ms + grpc_core::ExecCtx::Get()->Now();
  GRPC_CARES_TRACE_LOG(
      "request:%p grpc_ares_request_start_working_locked. timeout in "
      "%" PRId64 " ms",
      r, timeout);
  grpc_ares_request_internal_ref(r, "init query timeout");
  grpc_timer_init(&r->query_timeout, timeout, &r->on_timeout_locked);
  // Early out if the target is an ipv4 or ipv6 literal.
  if (resolve_as_ip_literal_locked(name, default_port, addrs)) {
    grpc_ares_request_queries_unref_locked(r, "ip literal");
    return r;
  }
  // Early out if the target is localhost and we're on Windows.
  if (grpc_ares_maybe_resolve_localhost_manually_locked(name, default_port,
                                                        addrs)) {
    grpc_ares_request_queries_unref_locked(r, "manual localhost");
    return r;
  }
  // Don't query for SRV and TXT records if the target is "localhost", so
  // as to cut down on lookups over the network, especially in tests:
  // https://github.com/grpc/proposal/pull/79
  if (target_matches_localhost(name)) {
    check_grpclb = false;
    r->service_config_json_out = nullptr;
  }
  // Look up name using c-ares lib.
  grpc_dns_lookup_ares_continue_after_check_localhost_and_ip_literals_locked(
      r, dns_server, name, default_port, interested_parties, check_grpclb,
      query_timeout_ms, combiner);
  grpc_ares_request_queries_unref_locked(r, "initialize lookup done");
  return r;
}

grpc_ares_request* (*grpc_dns_lookup_ares_locked)(
    const char* dns_server, const char* name, const char* default_port,
    grpc_pollset_set* interested_parties, grpc_closure* on_done,
    grpc_core::UniquePtr<grpc_core::ServerAddressList>* addrs,
    bool check_grpclb, char** service_config_json, int query_timeout_ms,
    grpc_combiner* combiner) = grpc_dns_lookup_ares_locked_impl;

static void grpc_cancel_ares_request_locked_impl(grpc_ares_request* r) {
  GPR_ASSERT(r != nullptr);
  if (!r->shutting_down) {
    grpc_ares_request_shutdown_locked(r);
  }
}

void (*grpc_cancel_ares_request_locked)(grpc_ares_request* r) =
    grpc_cancel_ares_request_locked_impl;

grpc_error* grpc_ares_init(void) {
  gpr_once_init(&g_basic_init, do_basic_init);
  gpr_mu_lock(&g_init_mu);
  int status = ares_library_init(ARES_LIB_INIT_ALL);
  gpr_mu_unlock(&g_init_mu);

  if (status != ARES_SUCCESS) {
    char* error_msg;
    gpr_asprintf(&error_msg, "ares_library_init failed: %s",
                 ares_strerror(status));
    grpc_error* error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(error_msg);
    gpr_free(error_msg);
    return error;
  }
  return GRPC_ERROR_NONE;
}

void grpc_ares_cleanup(void) {
  gpr_mu_lock(&g_init_mu);
  ares_library_cleanup();
  gpr_mu_unlock(&g_init_mu);
}

/*
 * grpc_resolve_address_ares related structs and functions
 */

typedef struct grpc_resolve_address_ares_request {
  /* combiner that queries and related callbacks run under */
  grpc_combiner* combiner;
  /** the pointer to receive the resolved addresses */
  grpc_resolved_addresses** addrs_out;
  /** currently resolving addresses */
  grpc_core::UniquePtr<ServerAddressList> addresses;
  /** closure to call when the resolve_address_ares request completes */
  grpc_closure* on_resolve_address_done;
  /** a closure wrapping on_resolve_address_done, which should be invoked when
     the grpc_dns_lookup_ares_locked operation is done. */
  grpc_closure on_dns_lookup_done_locked;
  /* target name */
  const char* name;
  /* default port to use if none is specified */
  const char* default_port;
  /* pollset_set to be driven by */
  grpc_pollset_set* interested_parties;
  /* underlying ares_request that the query is performed on */
  grpc_ares_request* ares_request = nullptr;
} grpc_resolve_address_ares_request;

static void on_dns_lookup_done_locked(void* arg, grpc_error* error) {
  grpc_resolve_address_ares_request* r =
      static_cast<grpc_resolve_address_ares_request*>(arg);
  grpc_ares_request_destroy(r->ares_request);
  grpc_resolved_addresses** resolved_addresses = r->addrs_out;
  if (r->addresses == nullptr || r->addresses->empty()) {
    *resolved_addresses = nullptr;
  } else {
    *resolved_addresses = static_cast<grpc_resolved_addresses*>(
        gpr_zalloc(sizeof(grpc_resolved_addresses)));
    (*resolved_addresses)->naddrs = r->addresses->size();
    (*resolved_addresses)->addrs =
        static_cast<grpc_resolved_address*>(gpr_zalloc(
            sizeof(grpc_resolved_address) * (*resolved_addresses)->naddrs));
    for (size_t i = 0; i < (*resolved_addresses)->naddrs; ++i) {
      GPR_ASSERT(!(*r->addresses)[i].IsBalancer());
      memcpy(&(*resolved_addresses)->addrs[i], &(*r->addresses)[i].address(),
             sizeof(grpc_resolved_address));
    }
  }
  GRPC_CLOSURE_SCHED(r->on_resolve_address_done, GRPC_ERROR_REF(error));
  GRPC_COMBINER_UNREF(r->combiner, "on_dns_lookup_done_cb");
  grpc_core::Delete(r);
}

static void grpc_resolve_address_invoke_dns_lookup_ares_locked(
    void* arg, grpc_error* unused_error) {
  grpc_resolve_address_ares_request* r =
      static_cast<grpc_resolve_address_ares_request*>(arg);
  r->ares_request = grpc_dns_lookup_ares_locked(
      nullptr /* dns_server */, r->name, r->default_port, r->interested_parties,
      &r->on_dns_lookup_done_locked, &r->addresses, false /* check_grpclb */,
      nullptr /* service_config_json */, GRPC_DNS_ARES_DEFAULT_QUERY_TIMEOUT_MS,
      r->combiner);
}

static void grpc_resolve_address_ares_impl(const char* name,
                                           const char* default_port,
                                           grpc_pollset_set* interested_parties,
                                           grpc_closure* on_done,
                                           grpc_resolved_addresses** addrs) {
  grpc_resolve_address_ares_request* r =
      grpc_core::New<grpc_resolve_address_ares_request>();
  r->combiner = grpc_combiner_create();
  r->addrs_out = addrs;
  r->on_resolve_address_done = on_done;
  GRPC_CLOSURE_INIT(&r->on_dns_lookup_done_locked, on_dns_lookup_done_locked, r,
                    grpc_combiner_scheduler(r->combiner));
  r->name = name;
  r->default_port = default_port;
  r->interested_parties = interested_parties;
  GRPC_CLOSURE_SCHED(
      GRPC_CLOSURE_CREATE(grpc_resolve_address_invoke_dns_lookup_ares_locked, r,
                          grpc_combiner_scheduler(r->combiner)),
      GRPC_ERROR_NONE);
}

void (*grpc_resolve_address_ares)(
    const char* name, const char* default_port,
    grpc_pollset_set* interested_parties, grpc_closure* on_done,
    grpc_resolved_addresses** addrs) = grpc_resolve_address_ares_impl;

#endif /* GRPC_ARES == 1 && !defined(GRPC_UV) */

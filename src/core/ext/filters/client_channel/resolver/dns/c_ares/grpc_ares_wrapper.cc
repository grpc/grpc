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

#if GRPC_ARES == 1

#include "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper.h"
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
#include "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_ev_driver.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/gprpp/map.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/iomgr/iomgr_internal.h"
#include "src/core/lib/iomgr/nameser.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"

using grpc_core::ServerAddress;
using grpc_core::ServerAddressList;

grpc_core::TraceFlag grpc_trace_cares_address_sorting(false,
                                                      "cares_address_sorting");

grpc_core::TraceFlag grpc_trace_cares_resolver(false, "cares_resolver");

namespace {

class GrpcAresPendingRequestKey {
 public:
  GrpcAresPendingRequestKey() {}

  GrpcAresPendingRequestKey(
    const char* dns_server,
    const char* name,
    const char* default_port,
    bool query_timeout_ms,
    bool check_grpclb,
    bool check_service_config) :
    dns_server_(grpc_core::UniquePtr<char>(gpr_strdup(dns_server))),
    name_(grpc_core::UniquePtr<char>(gpr_strdup(name))),
    default_port_(grpc_core::UniquePtr<char>(gpr_strdup(default_port))),
    query_timeout_ms_(query_timeout_ms),
    check_grpclb_(check_grpclb),
    check_service_config_(check_grpclb) {}

  GrpcAresPendingRequestKey(const GrpcAresPendingRequestKey& other) {
    dns_server_ = grpc_core::UniquePtr<char>(gpr_strdup(other.dns_server_.get()));
    name_ = grpc_core::UniquePtr<char>(gpr_strdup(other.name_.get()));
    default_port_ = grpc_core::UniquePtr<char>(gpr_strdup(other.default_port_.get()));
    query_timeout_ms_ = other.query_timeout_ms_;
    check_grpclb_ = other.check_grpclb_;
    check_service_config_ = other.check_grpclb_;
  }

  GrpcAresPendingRequestKey& operator=(const GrpcAresPendingRequestKey& other) {
    dns_server_ = grpc_core::UniquePtr<char>(gpr_strdup(other.dns_server_.get()));
    name_ = grpc_core::UniquePtr<char>(gpr_strdup(other.name_.get()));
    default_port_ = grpc_core::UniquePtr<char>(gpr_strdup(other.default_port_.get()));
    query_timeout_ms_ = other.query_timeout_ms_;
    check_grpclb_ = other.check_grpclb_;
    check_service_config_ = other.check_grpclb_;
    return *this;
  }

  bool operator<(const GrpcAresPendingRequestKey &other) const {
    int res;
    if ((res = gpr_stricmp(dns_server_.get(), other.dns_server_.get())) != 0) {
      return res < 0 ? true : false;
    }
    if ((res = gpr_stricmp(name_.get(), other.name_.get())) != 0) {
      return res < 0 ? true : false;
    }
    if ((res = gpr_stricmp(default_port_.get(), other.default_port_.get())) != 0) {
      return res < 0 ? true : false;
    }
    if (query_timeout_ms_ != other.query_timeout_ms_) {
      return query_timeout_ms_;
    }
    if (check_grpclb_ != other.check_grpclb_) {
      return check_grpclb_;
    }
    if (check_service_config_ != other.check_service_config_) {
      return check_service_config_;
    }
    return false;
  }

 private:
  /** The fields below are all owned by this object.
   * They are all tracked here only for comparison with other
   * pending_requests_entry objects. */
  /** Specific DNS server to query */
  grpc_core::UniquePtr<char> dns_server_;
  /** target name to query */
  grpc_core::UniquePtr<char> name_;
  /** default port to use */
  grpc_core::UniquePtr<char> default_port_;
  /** query timeout */
  bool query_timeout_ms_;
  /** whether or not to query SRV records */
  bool check_grpclb_;
  /** wether or not to query for service config TXT records */
  bool check_service_config_;
};

}

struct grpc_ares_request {
  /** indicates the DNS server to use, if specified */
  struct ares_addr_port_node dns_server_addr;
  /** following members are set in grpc_resolve_address_ares_impl */
  /** closure to call when the request completes */
  grpc_closure* on_done;
  /** the pointer to receive the resolved addresses */
  grpc_core::UniquePtr<grpc_core::ServerAddressList>* addresses_out;
  /** the pointer to receive the service config in JSON */
  char** service_config_json_out;
  /** the evernt driver used by this request */
  grpc_ares_ev_driver* ev_driver;
  /** number of ongoing queries */
  size_t pending_queries;
  /** the errors explaining query failures, appended to in query callbacks */
  grpc_error* error;
  /** key used for comparison of this resolution request and others */
  GrpcAresPendingRequestKey key;
  /** synchronizes this resolution */
  grpc_combiner* combiner;
  /** used in a global list of pending duplicate requests */
  grpc_ares_request* next;
};

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

static gpr_once g_pending_requests_init = GPR_ONCE_INIT;
static gpr_mu g_pending_requests_mu;
static grpc_core::Map<GrpcAresPendingRequestKey, grpc_ares_request*> *g_pending_requests;

static void do_pending_requests_init() {
  gpr_mu_init(&g_pending_requests_mu);
  g_pending_requests = grpc_core::New<grpc_core::Map<GrpcAresPendingRequestKey, grpc_ares_request*>>();
}

bool push_pending_request(grpc_ares_request* r) {
  gpr_once_init(&g_pending_requests_init, do_pending_requests_init);
  {
    grpc_core::MutexLock lock(&g_pending_requests_mu);
    bool already_exists = false;
    GPR_ASSERT(r->next == nullptr);
    auto pending_requests = g_pending_requests->find(r->key);
    if (pending_requests != g_pending_requests->end()) {
      already_exists = true;
      r->next = pending_requests->second;
    }
    (*g_pending_requests)[r->key] = r;
    return already_exists;
  }
}

/** A unique result for a lookup. This takes owneship over all fields. */
struct complete_request_args {
  grpc_core::UniquePtr<ServerAddressList> address_list;
  char* service_config_json;
  grpc_ares_request* r;
};

void complete_request_locked(void* arg, grpc_error* error) {
  complete_request_args* result = static_cast<complete_request_args*>(arg);
  *result->r->service_config_json_out = result->service_config_json;
  *result->r->addresses_out = std::move(result->address_list);
  GRPC_COMBINER_UNREF(result->r->combiner, "complete request locked");
  GRPC_CLOSURE_SCHED(result->r->on_done, error);
  grpc_core::Delete(result);
}

void pop_pending_requests(const GrpcAresPendingRequestKey &key, const ServerAddressList* address_list, const char* service_config_json, grpc_error* error) {
  grpc_core::MutexLock lock(&g_pending_requests_mu);
  auto pending_requests = g_pending_requests->find(key);
  grpc_ares_request* cur_request = pending_requests->second;
  // There's at least one pending request in the list (the one invoking this).
  GPR_ASSERT(cur_request != nullptr);
  while (cur_request != nullptr) {
    complete_request_args* args = grpc_core::New<complete_request_args>();
    if (address_list != nullptr) {
      args->address_list = grpc_core::UniquePtr<ServerAddressList>(grpc_core::New<ServerAddressList>(*address_list));
    }
    args->service_config_json = gpr_strdup(service_config_json);
    args->r = cur_request;
    GRPC_CLOSURE_SCHED(GRPC_CLOSURE_CREATE(complete_request_locked, args, grpc_combiner_scheduler(cur_request->combiner)), error);
    cur_request = cur_request->next;
  }
  g_pending_requests->erase(key);
}

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
  if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_cares_address_sorting)) {
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
  if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_cares_address_sorting)) {
    log_address_sorting_list(*addresses, "output");
  }
}

static void grpc_ares_request_ref_locked(grpc_ares_request* r) {
  r->pending_queries++;
}

static void grpc_ares_request_unref_locked(grpc_ares_request* r) {
  r->pending_queries--;
  if (r->pending_queries == 0u) {
    grpc_ares_ev_driver_on_queries_complete_locked(r->ev_driver);
  }
}

void grpc_ares_complete_request_locked(grpc_ares_request* r) {
  /* Invoke on_done callback and destroy the
     request */
  r->ev_driver = nullptr;
  ServerAddressList* addresses = r->addresses_out->get();
  if (addresses != nullptr) {
    grpc_cares_wrapper_address_sorting_sort(addresses);
    GRPC_ERROR_UNREF(r->error);
    r->error = GRPC_ERROR_NONE;
    // TODO(apolcyn): allow c-ares to return a service config
    // with no addresses along side it
  }
  char* service_config_json = r->service_config_json_out != nullptr ? *r->service_config_json_out : nullptr;
  pop_pending_requests(r->key, addresses, service_config_json, r->error);
  // All pending request with keys equal to r->key (including this request)
  // will now fill in their output pointers under their own combiners.
  r->addresses_out->reset();
  gpr_free(service_config_json);
  r->service_config_json_out = nullptr;
}

static grpc_ares_hostbyname_request* create_hostbyname_request_locked(
    grpc_ares_request* parent_request, char* host, uint16_t port,
    bool is_balancer) {
  GRPC_CARES_TRACE_LOG(
      "request:%p create_hostbyname_request_locked host:%s port:%d "
      "is_balancer:%d",
      parent_request, host, port, is_balancer);
  grpc_ares_hostbyname_request* hr = static_cast<grpc_ares_hostbyname_request*>(
      gpr_zalloc(sizeof(grpc_ares_hostbyname_request)));
  hr->parent_request = parent_request;
  hr->host = gpr_strdup(host);
  hr->port = port;
  hr->is_balancer = is_balancer;
  grpc_ares_request_ref_locked(parent_request);
  return hr;
}

static void destroy_hostbyname_request_locked(
    grpc_ares_hostbyname_request* hr) {
  grpc_ares_request_unref_locked(hr->parent_request);
  gpr_free(hr->host);
  gpr_free(hr);
}

static void on_hostbyname_done_locked(void* arg, int status, int timeouts,
                                      struct hostent* hostent) {
  grpc_ares_hostbyname_request* hr =
      static_cast<grpc_ares_hostbyname_request*>(arg);
  grpc_ares_request* r = hr->parent_request;
  if (status == ARES_SUCCESS) {
    GRPC_CARES_TRACE_LOG(
        "request:%p on_hostbyname_done_locked host=%s ARES_SUCCESS", r,
        hr->host);
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
  } else {
    char* error_msg;
    gpr_asprintf(&error_msg, "C-ares status is not ARES_SUCCESS: %s",
                 ares_strerror(status));
    GRPC_CARES_TRACE_LOG("request:%p on_hostbyname_done_locked host=%s %s", r,
                         hr->host, error_msg);
    grpc_error* error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(error_msg);
    gpr_free(error_msg);
    r->error = grpc_error_add_child(error, r->error);
  }
  destroy_hostbyname_request_locked(hr);
}

static void on_srv_query_done_locked(void* arg, int status, int timeouts,
                                     unsigned char* abuf, int alen) {
  grpc_ares_request* r = static_cast<grpc_ares_request*>(arg);
  if (status == ARES_SUCCESS) {
    GRPC_CARES_TRACE_LOG("request:%p on_srv_query_done_locked ARES_SUCCESS", r);
    struct ares_srv_reply* reply;
    const int parse_status = ares_parse_srv_reply(abuf, alen, &reply);
    GRPC_CARES_TRACE_LOG("request:%p ares_parse_srv_reply: %d", r,
                         parse_status);
    if (parse_status == ARES_SUCCESS) {
      ares_channel* channel =
          grpc_ares_ev_driver_get_channel_locked(r->ev_driver);
      for (struct ares_srv_reply* srv_it = reply; srv_it != nullptr;
           srv_it = srv_it->next) {
        if (grpc_ares_query_ipv6()) {
          grpc_ares_hostbyname_request* hr = create_hostbyname_request_locked(
              r, srv_it->host, htons(srv_it->port), true /* is_balancer */);
          ares_gethostbyname(*channel, hr->host, AF_INET6,
                             on_hostbyname_done_locked, hr);
        }
        grpc_ares_hostbyname_request* hr = create_hostbyname_request_locked(
            r, srv_it->host, htons(srv_it->port), true /* is_balancer */);
        ares_gethostbyname(*channel, hr->host, AF_INET,
                           on_hostbyname_done_locked, hr);
        grpc_ares_ev_driver_start_locked(r->ev_driver);
      }
    }
    if (reply != nullptr) {
      ares_free_data(reply);
    }
  } else {
    char* error_msg;
    gpr_asprintf(&error_msg, "C-ares status is not ARES_SUCCESS: %s",
                 ares_strerror(status));
    GRPC_CARES_TRACE_LOG("request:%p on_srv_query_done_locked %s", r,
                         error_msg);
    grpc_error* error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(error_msg);
    gpr_free(error_msg);
    r->error = grpc_error_add_child(error, r->error);
  }
  grpc_ares_request_unref_locked(r);
}

static const char g_service_config_attribute_prefix[] = "grpc_config=";

static void on_txt_done_locked(void* arg, int status, int timeouts,
                               unsigned char* buf, int len) {
  char* error_msg;
  grpc_ares_request* r = static_cast<grpc_ares_request*>(arg);
  const size_t prefix_len = sizeof(g_service_config_attribute_prefix) - 1;
  struct ares_txt_ext* result = nullptr;
  struct ares_txt_ext* reply = nullptr;
  grpc_error* error = GRPC_ERROR_NONE;
  if (status != ARES_SUCCESS) goto fail;
  GRPC_CARES_TRACE_LOG("request:%p on_txt_done_locked ARES_SUCCESS", r);
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
  GRPC_CARES_TRACE_LOG("request:%p on_txt_done_locked %s", r, error_msg);
  gpr_free(error_msg);
  r->error = grpc_error_add_child(error, r->error);
done:
  grpc_ares_request_unref_locked(r);
}

void grpc_dns_lookup_ares_continue_after_check_localhost_and_ip_literals_locked(
    grpc_ares_request* r, const char* dns_server, const char* name,
    const char* default_port, grpc_pollset_set* interested_parties,
    bool check_grpclb, int query_timeout_ms, grpc_combiner* combiner) {
  grpc_error* error = GRPC_ERROR_NONE;
  grpc_ares_hostbyname_request* hr = nullptr;
  ares_channel* channel = nullptr;
  /* parse name, splitting it into host and port parts */
  grpc_core::UniquePtr<char> host;
  grpc_core::UniquePtr<char> port;
  grpc_core::SplitHostPort(name, &host, &port);
  if (host == nullptr) {
    error = grpc_error_set_str(
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("unparseable host:port"),
        GRPC_ERROR_STR_TARGET_ADDRESS, grpc_slice_from_copied_string(name));
    goto error_cleanup;
  } else if (port == nullptr) {
    if (default_port == nullptr) {
      error = grpc_error_set_str(
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("no port in name"),
          GRPC_ERROR_STR_TARGET_ADDRESS, grpc_slice_from_copied_string(name));
      goto error_cleanup;
    }
    port.reset(gpr_strdup(default_port));
  }
  error = grpc_ares_ev_driver_create_locked(&r->ev_driver, interested_parties,
                                            query_timeout_ms, combiner, r);
  if (error != GRPC_ERROR_NONE) goto error_cleanup;
  channel = grpc_ares_ev_driver_get_channel_locked(r->ev_driver);
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
      error = grpc_error_set_str(
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("cannot parse authority"),
          GRPC_ERROR_STR_TARGET_ADDRESS, grpc_slice_from_copied_string(name));
      goto error_cleanup;
    }
    int status = ares_set_servers_ports(*channel, &r->dns_server_addr);
    if (status != ARES_SUCCESS) {
      char* error_msg;
      gpr_asprintf(&error_msg, "C-ares status is not ARES_SUCCESS: %s",
                   ares_strerror(status));
      error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(error_msg);
      gpr_free(error_msg);
      goto error_cleanup;
    }
  }
  r->pending_queries = 1;
  if (grpc_ares_query_ipv6()) {
    hr = create_hostbyname_request_locked(r, host.get(),
                                          grpc_strhtons(port.get()),
                                          /*is_balancer=*/false);
    ares_gethostbyname(*channel, hr->host, AF_INET6, on_hostbyname_done_locked,
                       hr);
  }
  hr =
      create_hostbyname_request_locked(r, host.get(), grpc_strhtons(port.get()),
                                       /*is_balancer=*/false);
  ares_gethostbyname(*channel, hr->host, AF_INET, on_hostbyname_done_locked,
                     hr);
  if (check_grpclb) {
    /* Query the SRV record */
    grpc_ares_request_ref_locked(r);
    char* service_name;
    gpr_asprintf(&service_name, "_grpclb._tcp.%s", host.get());
    ares_query(*channel, service_name, ns_c_in, ns_t_srv,
               on_srv_query_done_locked, r);
    gpr_free(service_name);
  }
  if (r->service_config_json_out != nullptr) {
    grpc_ares_request_ref_locked(r);
    char* config_name;
    gpr_asprintf(&config_name, "_grpc_config.%s", host.get());
    ares_search(*channel, config_name, ns_c_in, ns_t_txt, on_txt_done_locked,
                r);
    gpr_free(config_name);
  }
  grpc_ares_ev_driver_start_locked(r->ev_driver);
  grpc_ares_request_unref_locked(r);
  return;

error_cleanup:
  GRPC_CLOSURE_SCHED(r->on_done, error);
}

static bool inner_resolve_as_ip_literal_locked(
    const char* name, const char* default_port,
    grpc_core::UniquePtr<grpc_core::ServerAddressList>* addrs,
    grpc_core::UniquePtr<char>* host, grpc_core::UniquePtr<char>* port,
    grpc_core::UniquePtr<char>* hostport) {
  grpc_core::SplitHostPort(name, host, port);
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
    port->reset(gpr_strdup(default_port));
  }
  grpc_resolved_address addr;
  GPR_ASSERT(grpc_core::JoinHostPort(hostport, host->get(), atoi(port->get())));
  if (grpc_parse_ipv4_hostport(hostport->get(), &addr,
                               false /* log errors */) ||
      grpc_parse_ipv6_hostport(hostport->get(), &addr,
                               false /* log errors */)) {
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
  grpc_core::UniquePtr<char> host;
  grpc_core::UniquePtr<char> port;
  grpc_core::UniquePtr<char> hostport;
  bool out = inner_resolve_as_ip_literal_locked(name, default_port, addrs,
                                                &host, &port, &hostport);
  return out;
}

static bool target_matches_localhost_inner(const char* name,
                                           grpc_core::UniquePtr<char>* host,
                                           grpc_core::UniquePtr<char>* port) {
  if (!grpc_core::SplitHostPort(name, host, port)) {
    gpr_log(GPR_ERROR, "Unable to split host and port for name: %s", name);
    return false;
  }
  if (gpr_stricmp(host->get(), "localhost") == 0) {
    return true;
  } else {
    return false;
  }
}

static bool target_matches_localhost(const char* name) {
  grpc_core::UniquePtr<char> host;
  grpc_core::UniquePtr<char> port;
  return target_matches_localhost_inner(name, &host, &port);
}

#ifdef GRPC_ARES_RESOLVE_LOCALHOST_MANUALLY
static bool inner_maybe_resolve_localhost_manually_locked(
    const char* name, const char* default_port,
    grpc_core::UniquePtr<grpc_core::ServerAddressList>* addrs,
    grpc_core::UniquePtr<char>* host, grpc_core::UniquePtr<char>* port) {
  grpc_core::SplitHostPort(name, host, port);
  if (*host == nullptr) {
    gpr_log(GPR_ERROR,
            "Failed to parse %s into host:port during manual localhost "
            "resolution check.",
            name);
    return false;
  }
  if (*port == nullptr) {
    if (default_port == nullptr) {
      gpr_log(GPR_ERROR,
              "No port or default port for %s during manual localhost "
              "resolution check.",
              name);
      return false;
    }
    port->reset(gpr_strdup(default_port));
  }
  if (gpr_stricmp(host->get(), "localhost") == 0) {
    GPR_ASSERT(*addrs == nullptr);
    *addrs = grpc_core::MakeUnique<grpc_core::ServerAddressList>();
    uint16_t numeric_port = grpc_strhtons(port->get());
    // Append the ipv6 loopback address.
    struct sockaddr_in6 ipv6_loopback_addr;
    memset(&ipv6_loopback_addr, 0, sizeof(ipv6_loopback_addr));
    ((char*)&ipv6_loopback_addr.sin6_addr)[15] = 1;
    ipv6_loopback_addr.sin6_family = AF_INET6;
    ipv6_loopback_addr.sin6_port = numeric_port;
    (*addrs)->emplace_back(&ipv6_loopback_addr, sizeof(ipv6_loopback_addr),
                           nullptr /* args */);
    // Append the ipv4 loopback address.
    struct sockaddr_in ipv4_loopback_addr;
    memset(&ipv4_loopback_addr, 0, sizeof(ipv4_loopback_addr));
    ((char*)&ipv4_loopback_addr.sin_addr)[0] = 0x7f;
    ((char*)&ipv4_loopback_addr.sin_addr)[3] = 0x01;
    ipv4_loopback_addr.sin_family = AF_INET;
    ipv4_loopback_addr.sin_port = numeric_port;
    (*addrs)->emplace_back(&ipv4_loopback_addr, sizeof(ipv4_loopback_addr),
                           nullptr /* args */);
    // Let the address sorter figure out which one should be tried first.
    grpc_cares_wrapper_address_sorting_sort(addrs->get());
    return true;
  }
  return false;
}

static bool grpc_ares_maybe_resolve_localhost_manually_locked(
    const char* name, const char* default_port,
    grpc_core::UniquePtr<grpc_core::ServerAddressList>* addrs) {
  grpc_core::UniquePtr<char> host;
  grpc_core::UniquePtr<char> port;
  return inner_maybe_resolve_localhost_manually_locked(name, default_port,
                                                       addrs, &host, &port);
}
#else  /* GRPC_ARES_RESOLVE_LOCALHOST_MANUALLY */
static bool grpc_ares_maybe_resolve_localhost_manually_locked(
    const char* name, const char* default_port,
    grpc_core::UniquePtr<grpc_core::ServerAddressList>* addrs) {
  return false;
}
#endif /* GRPC_ARES_RESOLVE_LOCALHOST_MANUALLY */

static grpc_ares_request* grpc_dns_lookup_ares_locked_impl(
    const char* dns_server, const char* name, const char* default_port,
    grpc_pollset_set* interested_parties, grpc_closure* on_done,
    grpc_core::UniquePtr<grpc_core::ServerAddressList>* addrs,
    bool check_grpclb, char** service_config_json, int query_timeout_ms,
    grpc_combiner* combiner) {
  grpc_ares_request* r =
      static_cast<grpc_ares_request*>(gpr_zalloc(sizeof(grpc_ares_request)));
  r->ev_driver = nullptr;
  r->on_done = on_done;
  r->addresses_out = addrs;
  r->service_config_json_out = service_config_json;
  r->error = GRPC_ERROR_NONE;
  r->pending_queries = 0;
  r->combiner = GRPC_COMBINER_REF(combiner, "dns lookup ares begin");
  GRPC_CARES_TRACE_LOG(
      "request:%p c-ares grpc_dns_lookup_ares_locked_impl dns_server=%s name=%s, "
      "default_port=%s, check_grpclb=%d, **service_config_json=%p "
      "query_timeout_ms=%d",
      r, dns_server, name, default_port, check_grpclb, service_config_json, query_timeout_ms);
  r->key = GrpcAresPendingRequestKey(dns_server, name, default_port, query_timeout_ms, check_grpclb,
      service_config_json != nullptr ? true : false /* check service config */);
  if (push_pending_request(r)) {
    // A resolution request with the same key is already pending. Wait until
    // that one finishes and then complete this one with a copy of the results.
    return r;
  }
  // Early out if the target is an ipv4 or ipv6 literal.
  if (resolve_as_ip_literal_locked(name, default_port, addrs)) {
    grpc_ares_complete_request_locked(r);
    return r;
  }
  // Early out if the target is localhost and we're on Windows.
  if (grpc_ares_maybe_resolve_localhost_manually_locked(name, default_port,
                                                        addrs)) {
    grpc_ares_complete_request_locked(r);
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
  if (r->ev_driver != nullptr) {
    grpc_ares_ev_driver_shutdown_locked(r->ev_driver);
  }
}

void (*grpc_cancel_ares_request_locked)(grpc_ares_request* r) =
    grpc_cancel_ares_request_locked_impl;

// ares_library_init and ares_library_cleanup are currently no-op except under
// Windows. Calling them may cause race conditions when other parts of the
// binary calls these functions concurrently.
#ifdef GPR_WINDOWS
grpc_error* grpc_ares_init(void) {
  int status = ares_library_init(ARES_LIB_INIT_ALL);
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

void grpc_ares_cleanup(void) { ares_library_cleanup(); }
#else
grpc_error* grpc_ares_init(void) { return GRPC_ERROR_NONE; }
void grpc_ares_cleanup(void) {}
#endif  // GPR_WINDOWS

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
  gpr_free(r->ares_request);
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

#endif /* GRPC_ARES == 1 */

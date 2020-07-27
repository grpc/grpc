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

#include "absl/container/inlined_vector.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"

#include <ares.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>

#include <address_sorting/address_sorting.h>
#include "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_balancer_addresses.h"
#include "src/core/ext/filters/client_channel/parse_address.h"
#include "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_ev_driver.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/host_port.h"
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

struct grpc_ares_request {
  /** indicates the DNS server to use, if specified */
  struct ares_addr_port_node dns_server_addr;
  /** following members are set in grpc_resolve_address_ares_impl */
  /** closure to call when the request completes */
  grpc_closure* on_done;
  /** the pointer to receive the resolved addresses */
  std::unique_ptr<grpc_core::ServerAddressList>* addresses_out;
  /** the pointer to receive the resolved balancer addresses */
  std::unique_ptr<grpc_core::ServerAddressList>* balancer_addresses_out;
  /** the pointer to receive the service config in JSON */
  char** service_config_json_out;
  /** the evernt driver used by this request */
  grpc_ares_ev_driver* ev_driver;
  /** number of ongoing queries */
  size_t pending_queries;

  /** the errors explaining query failures, appended to in query callbacks */
  grpc_error* error;
};

// TODO(apolcyn): make grpc_ares_hostbyname_request a sub-class
// of GrpcAresQuery.
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
  /** for logging and errors: the query type ("A" or "AAAA") */
  const char* qtype;
} grpc_ares_hostbyname_request;

static void grpc_ares_request_ref_locked(grpc_ares_request* r);
static void grpc_ares_request_unref_locked(grpc_ares_request* r);

// TODO(apolcyn): as a part of C++-ification, find a way to
// organize per-query and per-resolution information in such a way
// that doesn't involve allocating a number of different data
// structures.
class GrpcAresQuery {
 public:
  explicit GrpcAresQuery(grpc_ares_request* r, const std::string& name)
      : r_(r), name_(name) {
    grpc_ares_request_ref_locked(r_);
  }

  ~GrpcAresQuery() { grpc_ares_request_unref_locked(r_); }

  grpc_ares_request* parent_request() { return r_; }

  const std::string& name() { return name_; }

 private:
  /* the top level request instance */
  grpc_ares_request* r_;
  /** for logging and errors */
  const std::string name_;
};

static void log_address_sorting_list(const grpc_ares_request* r,
                                     const ServerAddressList& addresses,
                                     const char* input_output_str) {
  for (size_t i = 0; i < addresses.size(); i++) {
    std::string addr_str =
        grpc_sockaddr_to_string(&addresses[i].address(), true);
    gpr_log(GPR_INFO,
            "(c-ares resolver) request:%p c-ares address sorting: %s[%" PRIuPTR
            "]=%s",
            r, input_output_str, i, addr_str.c_str());
  }
}

void grpc_cares_wrapper_address_sorting_sort(const grpc_ares_request* r,
                                             ServerAddressList* addresses) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_cares_address_sorting)) {
    log_address_sorting_list(r, *addresses, "input");
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
    log_address_sorting_list(r, *addresses, "output");
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
    grpc_cares_wrapper_address_sorting_sort(r, addresses);
    GRPC_ERROR_UNREF(r->error);
    r->error = GRPC_ERROR_NONE;
    // TODO(apolcyn): allow c-ares to return a service config
    // with no addresses along side it
  }
  if (r->balancer_addresses_out != nullptr) {
    ServerAddressList* balancer_addresses = r->balancer_addresses_out->get();
    if (balancer_addresses != nullptr) {
      grpc_cares_wrapper_address_sorting_sort(r, balancer_addresses);
    }
  }
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, r->on_done, r->error);
}

/* Note that the returned object takes a reference to qtype, so
 * qtype must outlive it. */
static grpc_ares_hostbyname_request* create_hostbyname_request_locked(
    grpc_ares_request* parent_request, const char* host, uint16_t port,
    bool is_balancer, const char* qtype) {
  GRPC_CARES_TRACE_LOG(
      "request:%p create_hostbyname_request_locked host:%s port:%d "
      "is_balancer:%d qtype:%s",
      parent_request, host, port, is_balancer, qtype);
  grpc_ares_hostbyname_request* hr = new grpc_ares_hostbyname_request();
  hr->parent_request = parent_request;
  hr->host = gpr_strdup(host);
  hr->port = port;
  hr->is_balancer = is_balancer;
  hr->qtype = qtype;
  grpc_ares_request_ref_locked(parent_request);
  return hr;
}

static void destroy_hostbyname_request_locked(
    grpc_ares_hostbyname_request* hr) {
  grpc_ares_request_unref_locked(hr->parent_request);
  gpr_free(hr->host);
  delete hr;
}

static void on_hostbyname_done_locked(void* arg, int status, int /*timeouts*/,
                                      struct hostent* hostent) {
  grpc_ares_hostbyname_request* hr =
      static_cast<grpc_ares_hostbyname_request*>(arg);
  grpc_ares_request* r = hr->parent_request;
  if (status == ARES_SUCCESS) {
    GRPC_CARES_TRACE_LOG(
        "request:%p on_hostbyname_done_locked qtype=%s host=%s ARES_SUCCESS", r,
        hr->qtype, hr->host);
    std::unique_ptr<ServerAddressList>* address_list_ptr =
        hr->is_balancer ? r->balancer_addresses_out : r->addresses_out;
    if (*address_list_ptr == nullptr) {
      *address_list_ptr = absl::make_unique<ServerAddressList>();
    }
    ServerAddressList& addresses = **address_list_ptr;
    for (size_t i = 0; hostent->h_addr_list[i] != nullptr; ++i) {
      absl::InlinedVector<grpc_arg, 1> args_to_add;
      if (hr->is_balancer) {
        args_to_add.emplace_back(
            grpc_core::CreateGrpclbBalancerNameArg(hr->host));
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
    std::string error_msg = absl::StrFormat(
        "C-ares status is not ARES_SUCCESS qtype=%s name=%s is_balancer=%d: %s",
        hr->qtype, hr->host, hr->is_balancer, ares_strerror(status));
    GRPC_CARES_TRACE_LOG("request:%p on_hostbyname_done_locked: %s", r,
                         error_msg.c_str());
    grpc_error* error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(error_msg.c_str());
    r->error = grpc_error_add_child(error, r->error);
  }
  destroy_hostbyname_request_locked(hr);
}

static void on_srv_query_done_locked(void* arg, int status, int /*timeouts*/,
                                     unsigned char* abuf, int alen) {
  GrpcAresQuery* q = static_cast<GrpcAresQuery*>(arg);
  grpc_ares_request* r = q->parent_request();
  if (status == ARES_SUCCESS) {
    GRPC_CARES_TRACE_LOG(
        "request:%p on_srv_query_done_locked name=%s ARES_SUCCESS", r,
        q->name().c_str());
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
              r, srv_it->host, htons(srv_it->port), true /* is_balancer */,
              "AAAA");
          ares_gethostbyname(*channel, hr->host, AF_INET6,
                             on_hostbyname_done_locked, hr);
        }
        grpc_ares_hostbyname_request* hr = create_hostbyname_request_locked(
            r, srv_it->host, htons(srv_it->port), true /* is_balancer */, "A");
        ares_gethostbyname(*channel, hr->host, AF_INET,
                           on_hostbyname_done_locked, hr);
        grpc_ares_ev_driver_start_locked(r->ev_driver);
      }
    }
    if (reply != nullptr) {
      ares_free_data(reply);
    }
  } else {
    std::string error_msg = absl::StrFormat(
        "C-ares status is not ARES_SUCCESS qtype=SRV name=%s: %s", q->name(),
        ares_strerror(status));
    GRPC_CARES_TRACE_LOG("request:%p on_srv_query_done_locked: %s", r,
                         error_msg.c_str());
    grpc_error* error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(error_msg.c_str());
    r->error = grpc_error_add_child(error, r->error);
  }
  delete q;
}

static const char g_service_config_attribute_prefix[] = "grpc_config=";

static void on_txt_done_locked(void* arg, int status, int /*timeouts*/,
                               unsigned char* buf, int len) {
  GrpcAresQuery* q = static_cast<GrpcAresQuery*>(arg);
  std::unique_ptr<GrpcAresQuery> query_deleter(q);
  grpc_ares_request* r = q->parent_request();
  const size_t prefix_len = sizeof(g_service_config_attribute_prefix) - 1;
  struct ares_txt_ext* result = nullptr;
  struct ares_txt_ext* reply = nullptr;
  grpc_error* error = GRPC_ERROR_NONE;
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
  return;
fail:
  std::string error_msg =
      absl::StrFormat("C-ares status is not ARES_SUCCESS qtype=TXT name=%s: %s",
                      q->name(), ares_strerror(status));
  error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(error_msg.c_str());
  GRPC_CARES_TRACE_LOG("request:%p on_txt_done_locked %s", r,
                       error_msg.c_str());
  r->error = grpc_error_add_child(error, r->error);
}

void grpc_dns_lookup_ares_continue_after_check_localhost_and_ip_literals_locked(
    grpc_ares_request* r, const char* dns_server, const char* name,
    const char* default_port, grpc_pollset_set* interested_parties,
    int query_timeout_ms,
    std::shared_ptr<grpc_core::WorkSerializer> work_serializer) {
  grpc_error* error = GRPC_ERROR_NONE;
  grpc_ares_hostbyname_request* hr = nullptr;
  ares_channel* channel = nullptr;
  /* parse name, splitting it into host and port parts */
  std::string host;
  std::string port;
  grpc_core::SplitHostPort(name, &host, &port);
  if (host.empty()) {
    error = grpc_error_set_str(
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("unparseable host:port"),
        GRPC_ERROR_STR_TARGET_ADDRESS, grpc_slice_from_copied_string(name));
    goto error_cleanup;
  } else if (port.empty()) {
    if (default_port == nullptr) {
      error = grpc_error_set_str(
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("no port in name"),
          GRPC_ERROR_STR_TARGET_ADDRESS, grpc_slice_from_copied_string(name));
      goto error_cleanup;
    }
    port = default_port;
  }
  error = grpc_ares_ev_driver_create_locked(&r->ev_driver, interested_parties,
                                            query_timeout_ms,
                                            std::move(work_serializer), r);
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
      error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(
          absl::StrCat("C-ares status is not ARES_SUCCESS: ",
                       ares_strerror(status))
              .c_str());
      goto error_cleanup;
    }
  }
  r->pending_queries = 1;
  if (grpc_ares_query_ipv6()) {
    hr = create_hostbyname_request_locked(r, host.c_str(),
                                          grpc_strhtons(port.c_str()),
                                          /*is_balancer=*/false, "AAAA");
    ares_gethostbyname(*channel, hr->host, AF_INET6, on_hostbyname_done_locked,
                       hr);
  }
  hr = create_hostbyname_request_locked(r, host.c_str(),
                                        grpc_strhtons(port.c_str()),
                                        /*is_balancer=*/false, "A");
  ares_gethostbyname(*channel, hr->host, AF_INET, on_hostbyname_done_locked,
                     hr);
  if (r->balancer_addresses_out != nullptr) {
    /* Query the SRV record */
    std::string service_name = absl::StrCat("_grpclb._tcp.", host);
    GrpcAresQuery* srv_query = new GrpcAresQuery(r, service_name);
    ares_query(*channel, service_name.c_str(), ns_c_in, ns_t_srv,
               on_srv_query_done_locked, srv_query);
  }
  if (r->service_config_json_out != nullptr) {
    std::string config_name = absl::StrCat("_grpc_config.", host);
    GrpcAresQuery* txt_query = new GrpcAresQuery(r, config_name);
    ares_search(*channel, config_name.c_str(), ns_c_in, ns_t_txt,
                on_txt_done_locked, txt_query);
  }
  grpc_ares_ev_driver_start_locked(r->ev_driver);
  grpc_ares_request_unref_locked(r);
  return;

error_cleanup:
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, r->on_done, error);
}

static bool inner_resolve_as_ip_literal_locked(
    const char* name, const char* default_port,
    std::unique_ptr<grpc_core::ServerAddressList>* addrs, std::string* host,
    std::string* port, std::string* hostport) {
  if (!grpc_core::SplitHostPort(name, host, port)) {
    gpr_log(GPR_ERROR,
            "Failed to parse %s to host:port while attempting to resolve as ip "
            "literal.",
            name);
    return false;
  }
  if (port->empty()) {
    if (default_port == nullptr) {
      gpr_log(GPR_ERROR,
              "No port or default port for %s while attempting to resolve as "
              "ip literal.",
              name);
      return false;
    }
    *port = default_port;
  }
  grpc_resolved_address addr;
  *hostport = grpc_core::JoinHostPort(*host, atoi(port->c_str()));
  if (grpc_parse_ipv4_hostport(hostport->c_str(), &addr,
                               false /* log errors */) ||
      grpc_parse_ipv6_hostport(hostport->c_str(), &addr,
                               false /* log errors */)) {
    GPR_ASSERT(*addrs == nullptr);
    *addrs = absl::make_unique<ServerAddressList>();
    (*addrs)->emplace_back(addr.addr, addr.len, nullptr /* args */);
    return true;
  }
  return false;
}

static bool resolve_as_ip_literal_locked(
    const char* name, const char* default_port,
    std::unique_ptr<grpc_core::ServerAddressList>* addrs) {
  std::string host;
  std::string port;
  std::string hostport;
  bool out = inner_resolve_as_ip_literal_locked(name, default_port, addrs,
                                                &host, &port, &hostport);
  return out;
}

static bool target_matches_localhost_inner(const char* name, std::string* host,
                                           std::string* port) {
  if (!grpc_core::SplitHostPort(name, host, port)) {
    gpr_log(GPR_ERROR, "Unable to split host and port for name: %s", name);
    return false;
  }
  if (gpr_stricmp(host->c_str(), "localhost") == 0) {
    return true;
  } else {
    return false;
  }
}

static bool target_matches_localhost(const char* name) {
  std::string host;
  std::string port;
  return target_matches_localhost_inner(name, &host, &port);
}

#ifdef GRPC_ARES_RESOLVE_LOCALHOST_MANUALLY
static bool inner_maybe_resolve_localhost_manually_locked(
    const grpc_ares_request* r, const char* name, const char* default_port,
    std::unique_ptr<grpc_core::ServerAddressList>* addrs, std::string* host,
    std::string* port) {
  grpc_core::SplitHostPort(name, host, port);
  if (host->empty()) {
    gpr_log(GPR_ERROR,
            "Failed to parse %s into host:port during manual localhost "
            "resolution check.",
            name);
    return false;
  }
  if (port->empty()) {
    if (default_port == nullptr) {
      gpr_log(GPR_ERROR,
              "No port or default port for %s during manual localhost "
              "resolution check.",
              name);
      return false;
    }
    *port = default_port;
  }
  if (gpr_stricmp(host->c_str(), "localhost") == 0) {
    GPR_ASSERT(*addrs == nullptr);
    *addrs = absl::make_unique<grpc_core::ServerAddressList>();
    uint16_t numeric_port = grpc_strhtons(port->c_str());
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
    grpc_cares_wrapper_address_sorting_sort(r, addrs->get());
    return true;
  }
  return false;
}

static bool grpc_ares_maybe_resolve_localhost_manually_locked(
    const grpc_ares_request* r, const char* name, const char* default_port,
    std::unique_ptr<grpc_core::ServerAddressList>* addrs) {
  std::string host;
  std::string port;
  return inner_maybe_resolve_localhost_manually_locked(r, name, default_port,
                                                       addrs, &host, &port);
}
#else  /* GRPC_ARES_RESOLVE_LOCALHOST_MANUALLY */
static bool grpc_ares_maybe_resolve_localhost_manually_locked(
    const grpc_ares_request* /*r*/, const char* /*name*/,
    const char* /*default_port*/,
    std::unique_ptr<grpc_core::ServerAddressList>* /*addrs*/) {
  return false;
}
#endif /* GRPC_ARES_RESOLVE_LOCALHOST_MANUALLY */

static grpc_ares_request* grpc_dns_lookup_ares_locked_impl(
    const char* dns_server, const char* name, const char* default_port,
    grpc_pollset_set* interested_parties, grpc_closure* on_done,
    std::unique_ptr<grpc_core::ServerAddressList>* addrs,
    std::unique_ptr<grpc_core::ServerAddressList>* balancer_addrs,
    char** service_config_json, int query_timeout_ms,
    std::shared_ptr<grpc_core::WorkSerializer> work_serializer) {
  grpc_ares_request* r =
      static_cast<grpc_ares_request*>(gpr_zalloc(sizeof(grpc_ares_request)));
  r->ev_driver = nullptr;
  r->on_done = on_done;
  r->addresses_out = addrs;
  r->balancer_addresses_out = balancer_addrs;
  r->service_config_json_out = service_config_json;
  r->error = GRPC_ERROR_NONE;
  r->pending_queries = 0;
  GRPC_CARES_TRACE_LOG(
      "request:%p c-ares grpc_dns_lookup_ares_locked_impl name=%s, "
      "default_port=%s",
      r, name, default_port);
  // Early out if the target is an ipv4 or ipv6 literal.
  if (resolve_as_ip_literal_locked(name, default_port, addrs)) {
    grpc_ares_complete_request_locked(r);
    return r;
  }
  // Early out if the target is localhost and we're on Windows.
  if (grpc_ares_maybe_resolve_localhost_manually_locked(r, name, default_port,
                                                        addrs)) {
    grpc_ares_complete_request_locked(r);
    return r;
  }
  // Don't query for SRV and TXT records if the target is "localhost", so
  // as to cut down on lookups over the network, especially in tests:
  // https://github.com/grpc/proposal/pull/79
  if (target_matches_localhost(name)) {
    r->balancer_addresses_out = nullptr;
    r->service_config_json_out = nullptr;
  }
  // Look up name using c-ares lib.
  grpc_dns_lookup_ares_continue_after_check_localhost_and_ip_literals_locked(
      r, dns_server, name, default_port, interested_parties, query_timeout_ms,
      std::move(work_serializer));
  return r;
}

grpc_ares_request* (*grpc_dns_lookup_ares_locked)(
    const char* dns_server, const char* name, const char* default_port,
    grpc_pollset_set* interested_parties, grpc_closure* on_done,
    std::unique_ptr<grpc_core::ServerAddressList>* addrs,
    std::unique_ptr<grpc_core::ServerAddressList>* balancer_addrs,
    char** service_config_json, int query_timeout_ms,
    std::shared_ptr<grpc_core::WorkSerializer> work_serializer) =
    grpc_dns_lookup_ares_locked_impl;

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
    return GRPC_ERROR_CREATE_FROM_COPIED_STRING(
        absl::StrCat("ares_library_init failed: ", ares_strerror(status))
            .c_str());
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
  /* work_serializer that queries and related callbacks run under */
  std::shared_ptr<grpc_core::WorkSerializer> work_serializer;
  /** the pointer to receive the resolved addresses */
  grpc_resolved_addresses** addrs_out;
  /** currently resolving addresses */
  std::unique_ptr<ServerAddressList> addresses;
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

static void on_dns_lookup_done_locked(grpc_resolve_address_ares_request* r,
                                      grpc_error* error) {
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
      memcpy(&(*resolved_addresses)->addrs[i], &(*r->addresses)[i].address(),
             sizeof(grpc_resolved_address));
    }
  }
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, r->on_resolve_address_done, error);
  delete r;
}

static void on_dns_lookup_done(void* arg, grpc_error* error) {
  grpc_resolve_address_ares_request* r =
      static_cast<grpc_resolve_address_ares_request*>(arg);
  GRPC_ERROR_REF(error);  // ref owned by lambda
  r->work_serializer->Run([r, error]() { on_dns_lookup_done_locked(r, error); },
                          DEBUG_LOCATION);
}

static void grpc_resolve_address_invoke_dns_lookup_ares_locked(void* arg) {
  grpc_resolve_address_ares_request* r =
      static_cast<grpc_resolve_address_ares_request*>(arg);
  GRPC_CLOSURE_INIT(&r->on_dns_lookup_done_locked, on_dns_lookup_done, r,
                    grpc_schedule_on_exec_ctx);
  r->ares_request = grpc_dns_lookup_ares_locked(
      nullptr /* dns_server */, r->name, r->default_port, r->interested_parties,
      &r->on_dns_lookup_done_locked, &r->addresses,
      nullptr /* balancer_addresses */, nullptr /* service_config_json */,
      GRPC_DNS_ARES_DEFAULT_QUERY_TIMEOUT_MS, r->work_serializer);
}

static void grpc_resolve_address_ares_impl(const char* name,
                                           const char* default_port,
                                           grpc_pollset_set* interested_parties,
                                           grpc_closure* on_done,
                                           grpc_resolved_addresses** addrs) {
  grpc_resolve_address_ares_request* r =
      new grpc_resolve_address_ares_request();
  r->work_serializer = std::make_shared<grpc_core::WorkSerializer>();
  r->addrs_out = addrs;
  r->on_resolve_address_done = on_done;
  r->name = name;
  r->default_port = default_port;
  r->interested_parties = interested_parties;
  r->work_serializer->Run(
      [r]() { grpc_resolve_address_invoke_dns_lookup_ares_locked(r); },
      DEBUG_LOCATION);
}

void (*grpc_resolve_address_ares)(
    const char* name, const char* default_port,
    grpc_pollset_set* interested_parties, grpc_closure* on_done,
    grpc_resolved_addresses** addrs) = grpc_resolve_address_ares_impl;

#endif /* GRPC_ARES == 1 */

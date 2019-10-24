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

static const char g_service_config_attribute_prefix[] = "grpc_config=";

void grpc_cares_wrapper_address_sorting_sort(ServerAddressList* addresses);

class GrpcAresRequest {
 public:
  GrpcAresRequest(RequestKey* key,
      grpc_core::UniquePtr<grpc_core::ServerAddressList>* addresses_out,
      char** service_config_json_out, grpc_closure* on_done, grpc_pollset_set* interested_party,
      grpc_combiner* combiner)
      : key(key), addresses_out(addresses_out),
        service_config_json_out(service_config_json_out),
        on_done(on_done), interested_party(interested_party),
        combiner(combiner)
         {}


  RequestKey* key_ = nullptr;

  /** following members are set in grpc_resolve_address_ares_impl */
  /** the pointer to receive the resolved addresses */
  grpc_core::UniquePtr<grpc_core::ServerAddressList>* addresses_out = nullptr;
  /** the pointer to receive the service config in JSON */
  char** service_config_json_out = nullptr;
  /** closure to call when the request completes */
  grpc_closure* on_done = nullptr;
  grpc_pollset_set* interested_party;
  /** synchronizes this resolution */
  grpc_combiner* combiner = nullptr;

 private:
};

class GrpcAresPendingRequestMap {
 public:
  // The key that identifies a unique ares request. Two requests with the same
  // request key are considered the same and can share the same response.
  class RequestKey {
   public:
    RequestKey(const char* dns_server, const char* name,
               const char* default_port, bool query_timeout_ms,
               bool check_grpclb, bool check_service_config)
        : dns_server_(grpc_core::UniquePtr<char>(gpr_strdup(dns_server))),
          name_(grpc_core::UniquePtr<char>(gpr_strdup(name))),
          default_port_(grpc_core::UniquePtr<char>(gpr_strdup(default_port))),
          query_timeout_ms_(query_timeout_ms),
          check_grpclb_(check_grpclb),
          check_service_config_(check_grpclb) {}

    RequestKey(const RequestKey& other) {
      dns_server_ =
          grpc_core::UniquePtr<char>(gpr_strdup(other.dns_server_.get()));
      name_ = grpc_core::UniquePtr<char>(gpr_strdup(other.name_.get()));
      default_port_ =
          grpc_core::UniquePtr<char>(gpr_strdup(other.default_port_.get()));
      query_timeout_ms_ = other.query_timeout_ms_;
      check_grpclb_ = other.check_grpclb_;
      check_service_config_ = other.check_grpclb_;
    }

    RequestKey& operator=(const RequestKey& other) {
      dns_server_ =
          grpc_core::UniquePtr<char>(gpr_strdup(other.dns_server_.get()));
      name_ = grpc_core::UniquePtr<char>(gpr_strdup(other.name_.get()));
      default_port_ =
          grpc_core::UniquePtr<char>(gpr_strdup(other.default_port_.get()));
      query_timeout_ms_ = other.query_timeout_ms_;
      check_grpclb_ = other.check_grpclb_;
      check_service_config_ = other.check_grpclb_;
      return *this;
    }

    // Returns true iff a and b are equal, otherwise returns false and
    // sets result to a valuable suitable for return by the < operator.
    bool CompareHandleNull(const char* a, const char* b, bool* result) const {
      if (a == nullptr || b == nullptr) {
        if (a != b) {
          *result = a == nullptr ? true : false;
          return false;
        }
        return true;
      }
      int res = gpr_stricmp(a, b);
      if (res == 0) {
        return true;
      }
      *result = res < 0 ? true : false;
      return false;
    }

    bool operator<(const RequestKey& other) const {
      bool res;
      if (!CompareHandleNull(dns_server_.get(), other.dns_server_.get(),
                             &res)) {
        return res;
      }
      if (!CompareHandleNull(name_.get(), other.name_.get(), &res)) {
        return res;
      }
      if (!CompareHandleNull(default_port_.get(), other.default_port_.get(),
                             &res)) {
        return res;
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

  /** A unique result for a lookup. This takes owneship over all fields. */
  class ResultUpdater {
   public:
    static void UpdateLocked(void* arg, grpc_error* error) {
      ResultUpdater* self = static_cast<ResultUpdater*>(arg);
      if (self->request->service_config_json_out != nullptr) {
        *self->request->service_config_json_out = self->service_config_json;
      } else {
        GPR_ASSERT(self->service_config_json == nullptr);
      }
      *self->request->addresses_out = std::move(self->address_list);
      GRPC_COMBINER_UNREF(self->request->combiner, "complete request locked");
      GRPC_ERROR_REF(error);
      GRPC_CLOSURE_SCHED(self->request->on_done, error);
      grpc_core::Delete(self);
    }

   private:
    grpc_core::UniquePtr<ServerAddressList> address_list;
    char* service_config_json;

    GrpcAresRequest* request;
  };

  class RequestWrapper {
   public:
    RequestWrapper() { memset(&dns_server_addr, 0, sizeof(dns_server_addr)); }

    void AddRequest(grpc_core::UniquePtr<GrpcAresRequest> request) {
      requests_[request.get()] = std::move(request);
      // FIXME
      grpc_pollset_set_add_pollset_set(interested_parties_, request->interested_party);
    }

    bool CancelRequest(GrpcAresRequest* request) {
      // FIXME: run on_done?
      requests_.erase(request);
      // FIXME
      grpc_pollset_set_del_pollset_set(interested_parties_, request->interested_party);
      // FIXME: other cleanup if empty
      return requests_.empty();
    }

    static void OnTxtQueryDoneLocked(void* arg, int status, int timeouts,
                                     unsigned char* buf, int len) {
      char* error_msg;
      RequestWrapper* self = static_cast<RequestWrapper*>(arg);
      const size_t prefix_len = sizeof(g_service_config_attribute_prefix) - 1;
      struct ares_txt_ext* result = nullptr;
      struct ares_txt_ext* reply = nullptr;
      grpc_error* error = GRPC_ERROR_NONE;
      if (status != ARES_SUCCESS) goto fail;
      GRPC_CARES_TRACE_LOG("request:%p OnTxtQueryDoneLocked ARES_SUCCESS", r);
      status = ares_parse_txt_reply_ext(buf, len, &reply);
      if (status != ARES_SUCCESS) goto fail;
      // Find service config in TXT record.
      for (result = reply; result != nullptr; result = result->next) {
        if (result->record_start &&
            memcmp(result->txt, g_service_config_attribute_prefix,
                   prefix_len) == 0) {
          break;
        }
      }
      // Found a service config record.
      if (result != nullptr) {
        size_t service_config_len = result->length - prefix_len;
        *self->service_config_json_out =
            static_cast<char*>(gpr_malloc(service_config_len + 1));
        memcpy(*self->service_config_json_out, result->txt + prefix_len,
               service_config_len);
        for (result = result->next; result != nullptr && !result->record_start;
             result = result->next) {
          *self->service_config_json_out = static_cast<char*>(
              gpr_realloc(*self->service_config_json_out,
                          service_config_len + result->length + 1));
          memcpy(*self->service_config_json_out + service_config_len,
                 result->txt, result->length);
          service_config_len += result->length;
        }
        (*self->service_config_json_out)[service_config_len] = '\0';
        GRPC_CARES_TRACE_LOG("request:%p found service config: %s", r,
                             *self->service_config_json_out);
      }
      // Clean up.
      ares_free_data(reply);
      goto done;
    fail:
      gpr_asprintf(&error_msg,
                   "C-ares TXT lookup status is not ARES_SUCCESS: %s",
                   ares_strerror(status));
      error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(error_msg);
      GRPC_CARES_TRACE_LOG("request:%p OnTxtQueryDoneLocked %s", r, error_msg);
      gpr_free(error_msg);
      self->error = grpc_error_add_child(error, self->error);
    done:
      grpc_ares_request_unref_locked(r);
    }

    static void OnSrvQueryDoneLocked(void* arg, int status, int timeouts,
                                     unsigned char* abuf, int alen) {
      GrpcAresPendingRequestMap::RequestWrapper* self =
          static_cast<RequestWrapper*>(arg);
      if (status == ARES_SUCCESS) {
        GRPC_CARES_TRACE_LOG("request:%p OnSrvQueryDoneLocked ARES_SUCCESS", r);
        struct ares_srv_reply* reply;
        const int parse_status = ares_parse_srv_reply(abuf, alen, &reply);
        GRPC_CARES_TRACE_LOG("request:%p ares_parse_srv_reply: %d", r,
                             parse_status);
        if (parse_status == ARES_SUCCESS) {
          ares_channel* channel =
              grpc_ares_ev_driver_get_channel_locked(self->ev_driver);
          for (struct ares_srv_reply* srv_it = reply; srv_it != nullptr;
               srv_it = srv_it->next) {
            if (grpc_ares_query_ipv6()) {
              grpc_ares_hostbyname_request* hr =
                  create_hostbyname_request_locked(r, srv_it->host,
                                                   htons(srv_it->port),
                                                   true /* is_balancer */);
              ares_gethostbyname(*channel, hself->host, AF_INET6,
                                 on_hostbyname_done_locked, hr);
            }
            grpc_ares_hostbyname_request* hr = create_hostbyname_request_locked(
                r, srv_it->host, htons(srv_it->port), true /* is_balancer */);
            ares_gethostbyname(*channel, hself->host, AF_INET,
                               on_hostbyname_done_locked, hr);
            grpc_ares_ev_driver_start_locked(self->ev_driver);
          }
        }
        if (reply != nullptr) {
          ares_free_data(reply);
        }
      } else {
        char* error_msg;
        gpr_asprintf(&error_msg, "C-ares status is not ARES_SUCCESS: %s",
                     ares_strerror(status));
        GRPC_CARES_TRACE_LOG("request:%p OnSrvQueryDoneLocked %s", r,
                             error_msg);
        grpc_error* error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(error_msg);
        gpr_free(error_msg);
        self->error = grpc_error_add_child(error, self->error);
      }
      self->Unref()
    }

    void LookUpWithAresLib() {
      grpc_error* local_error = GRPC_ERROR_NONE;
      grpc_ares_hostbyname_request* hr = nullptr;
      ares_channel* channel = nullptr;
      /* parse name, splitting it into host and port parts */
      grpc_core::UniquePtr<char> host;
      grpc_core::UniquePtr<char> port;
      grpc_core::SplitHostPort(name, &host, &port);
      if (host == nullptr) {
        local_error = grpc_error_set_str(
            GRPC_ERROR_CREATE_FROM_STATIC_STRING("unparseable host:port"),
            GRPC_ERROR_STR_TARGET_ADDRESS, grpc_slice_from_copied_string(name));
        goto error_cleanup;
      } else if (port == nullptr) {
        if (default_port == nullptr) {
          local_error = grpc_error_set_str(
              GRPC_ERROR_CREATE_FROM_STATIC_STRING("no port in name"),
              GRPC_ERROR_STR_TARGET_ADDRESS,
              grpc_slice_from_copied_string(name));
          goto error_cleanup;
        }
        port.reset(gpr_strdup(default_port));
      }
      local_error = grpc_ares_ev_driver_create_locked(
          &ev_driver, interested_parties, query_timeout_ms, combiner, this);
      if (local_error != GRPC_ERROR_NONE) goto error_cleanup;
      channel = grpc_ares_ev_driver_get_channel_locked(ev_driver);
      // If dns_server is specified, use it.
      if (dns_server != nullptr) {
        GRPC_CARES_TRACE_LOG("request:%p Using DNS server %s", this,
                             dns_server);
        grpc_resolved_address addr;
        if (grpc_parse_ipv4_hostport(dns_server, &addr,
                                     false /* log_errors */)) {
          dns_server_addr.family = AF_INET;
          struct sockaddr_in* in =
              reinterpret_cast<struct sockaddr_in*>(addr.addr);
          memcpy(&dns_server_addr.addr.addr4, &in->sin_addr,
                 sizeof(struct in_addr));
          dns_server_addr.tcp_port = grpc_sockaddr_get_port(&addr);
          dns_server_addr.udp_port = grpc_sockaddr_get_port(&addr);
        } else if (grpc_parse_ipv6_hostport(dns_server, &addr,
                                            false /* log_errors */)) {
          dns_server_addr.family = AF_INET6;
          struct sockaddr_in6* in6 =
              reinterpret_cast<struct sockaddr_in6*>(addr.addr);
          memcpy(&dns_server_addr.addr.addr6, &in6->sin6_addr,
                 sizeof(struct in6_addr));
          dns_server_addr.tcp_port = grpc_sockaddr_get_port(&addr);
          dns_server_addr.udp_port = grpc_sockaddr_get_port(&addr);
        } else {
          local_error = grpc_error_set_str(
              GRPC_ERROR_CREATE_FROM_STATIC_STRING("cannot parse authority"),
              GRPC_ERROR_STR_TARGET_ADDRESS,
              grpc_slice_from_copied_string(name));
          goto error_cleanup;
        }
        int status = ares_set_servers_ports(*channel, &dns_server_addr);
        if (status != ARES_SUCCESS) {
          char* error_msg;
          gpr_asprintf(&error_msg, "C-ares status is not ARES_SUCCESS: %s",
                       ares_strerror(status));
          local_error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(error_msg);
          gpr_free(error_msg);
          goto error_cleanup;
        }
      }
      pending_queries = 1;
      if (grpc_ares_query_ipv6()) {
        hr = create_hostbyname_request_locked(r, host.get(),
                                              grpc_strhtons(port.get()),
                                              /*is_balancer=*/false);
        ares_gethostbyname(*channel, hr->host, AF_INET6,
                           on_hostbyname_done_locked, hr);
      }
      hr = create_hostbyname_request_locked(r, host.get(),
                                            grpc_strhtons(port.get()),
                                            /*is_balancer=*/false);
      ares_gethostbyname(*channel, hr->host, AF_INET, on_hostbyname_done_locked,
                         hr);
      if (check_grpclb) {
        /* Query the SRV record */
        grpc_ares_request_ref_locked(r);
        char* service_name;
        gpr_asprintf(&service_name, "_grpclb._tcp.%s", host.get());
        ares_query(
            *channel, service_name, ns_c_in, ns_t_srv,
            GrpcAresPendingRequestMap::RequestWrapper::OnSrvQueryDoneLocked, r);
        gpr_free(service_name);
      }
      if (service_config_json_out != nullptr) {
        grpc_ares_request_ref_locked(r);
        char* config_name;
        gpr_asprintf(&config_name, "_grpc_config.%s", host.get());
        ares_search(*channel, config_name, ns_c_in, ns_t_txt,
                    OnTxtQueryDoneLocked, this);
        gpr_free(config_name);
      }
      grpc_ares_ev_driver_start_locked(ev_driver);
      grpc_ares_request_unref_locked(this);
      return;

    error_cleanup:
      error = local_error;
      RequestDoneLocked(r);
    }

    void OnResult() {
      ev_driver = nullptr;
      ServerAddressList* addresses = addresses_out->get();
      // Sort the addresses.
      if (addresses != nullptr) {
        grpc_cares_wrapper_address_sorting_sort(addresses.get());
        GRPC_ERROR_UNREF(error);
        error = GRPC_ERROR_NONE;
        // TODO(apolcyn): allow c-ares to return a service config
        // with no addresses along side it
      }
      char* service_config_json = service_config_json_out != nullptr
                                      ? *service_config_json_out
                                      : nullptr;
      // Copy and pass result to each request.
      for (auto& p : requests_) {
        auto& request = p.second;
        ResultUpdater* updater = grpc_core::New<ResultUpdater>();
        if (addresses != nullptr) {
          updater->address_list =
              grpc_core::MakeUnique<ServerAddressList>(*addresses);
        }
        updater->service_config_json = gpr_strdup(service_config_json);
        updater->request = request.get();
        GRPC_ERROR_REF(error);
        GRPC_CARES_TRACE_LOG("cur_request:%p OnResult", request.get());
        GRPC_CLOSURE_SCHED(
            GRPC_CLOSURE_CREATE(ResultUpdater::UpdateLocked, updater,
                                grpc_combiner_scheduler(request->combiner)),
            error);
      }
      // Clean up.
      GRPC_ERROR_UNREF(error);
      addresses_out->reset();
      if (service_config_json_out != nullptr) {
        gpr_free(*service_config_json_out);
      }
    }

    void Ref() { pending_queries++; }

    void Unref() {
      ;
      if (--pending_queries == 0u) {
        grpc_ares_ev_driver_on_queries_complete_locked(ev_driver);
      }
    }

    //   private:
    /** indicates the DNS server to use, if specified */
    struct ares_addr_port_node dns_server_addr;
    /** the evernt driver used by this request */
    grpc_ares_ev_driver* ev_driver = nullptr;
    /** number of ongoing queries */
    size_t pending_queries = 0;
    /** the errors explaining query failures, appended to in query callbacks */
    grpc_error* error = GRPC_ERROR_NONE;
    /** callbacks to invoke upon response */
    grpc_core::Map<Request*, grpc_core::UniquePtr<Request>> requests_;

    /** the pointer to receive the resolved addresses */
    grpc_core::UniquePtr<grpc_core::ServerAddressList>* addresses_out = nullptr;
    /** the pointer to receive the service config in JSON */
    char** service_config_json_out = nullptr;

    grpc_pollset_set* interested_parties_ = nullptr;
  };

  GrpcAresPendingRequestMap& instance() {
    if (instance_ == nullptr) {
      instance_ = grpc_core::New<GrpcAresPendingRequestMap>();
    }
    return *instance_;
  }

  bool AddRequest(grpc_core::UniquePtr<Request> request) {
    grpc_core::MutexLock lock(&mu_);
    auto iter = map_.find(request->key);
    bool already_exists = iter != map_.end();
    GRPC_CARES_TRACE_LOG("request:%p already_exists:%d", request.get(),
                         already_exists);
    auto wrapper = map_[key];
    wrapper.AddRequest(std::move(request));
    return already_exists;
  }

  void CancelRequest(GrpcAresRequest* request) {
    auto iter = map_.find(request->key_);
    auto wrapper = map_[request->key_];
    if (wrapper.CancelRequest(request)) {
      map_.erase(iter);
    }
  }

  void OnResult(const RequestKey& key) {
    grpc_core::MutexLock lock(&mu_);
    auto iter = map_.find(key);
    iter->second.OnResult();
    map_.erase(iter);
  }

  //  void RequestDoneLocked(RequestKey key,
  //                         grpc_core::UniquePtr<ServerAddressList> addresses,
  //                         char* service_config_json = nullptr) {
  //    auto iter = map_.find(key);
  //    auto& wrapper = iter->second;
  //  }

 private:
  static GrpcAresPendingRequestMap* instance_;

  grpc_core::Map<RequestKey, RequestWrapper> map_;
  grpc_core::Mutex mu_;
};

}  // namespace

typedef struct grpc_ares_hostbyname_request {
  /** following members are set in create_hostbyname_request_locked
   */
  /** the top-level request instance */
  GrpcAresRequest* parent_request;
  /** host to resolve, parsed from the name to resolve */
  char* host;
  /** port to fill in sockaddr_in, parsed from the name to resolve */
  uint16_t port;
  /** is it a grpclb address */
  bool is_balancer;
} grpc_ares_hostbyname_request;

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

static grpc_ares_hostbyname_request* create_hostbyname_request_locked(
    GrpcAresRequest* parent_request, char* host, uint16_t port,
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
  GrpcAresRequest* request = hr->parent_request;
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

static GrpcAresRequest* grpc_dns_lookup_ares_locked_impl(
    const char* dns_server, const char* name, const char* default_port,
    grpc_pollset_set* interested_parties, grpc_closure* on_done,
    grpc_core::UniquePtr<grpc_core::ServerAddressList>* addrs,
    bool check_grpclb, char** service_config_json, int query_timeout_ms,
    grpc_core::Combiner* combiner) {
  // Don't query for SRV and TXT records if the target is "localhost", so
  // as to cut down on lookups over the network, especially in tests:
  // https://github.com/grpc/proposal/pull/79
  if (target_matches_localhost(name)) {
    check_grpclb = false;
    service_config_json = nullptr;
  }
  GRPC_CARES_TRACE_LOG(
      "request:%p c-ares grpc_dns_lookup_ares_locked_impl dns_server=%s "
      "name=%s, "
      "default_port=%s, check_grpclb=%d, **service_config_json=%p "
      "query_timeout_ms=%d",
      r, dns_server, name, default_port, check_grpclb, service_config_json,
      query_timeout_ms);
  auto key = GrpcAresPendingRequestMap::RequestKey(
      dns_server, name, default_port, query_timeout_ms, check_grpclb,
      service_config_json != nullptr ? true : false /* check service config */);
  auto* request = grpc_core::New<GrpcAresRequest>(key,
      addrs, service_config_json, on_done, interested_parties,
      GRPC_COMBINER_REF(combiner, "dns lookup ares begin"));
  auto& map = GrpcAresPendingRequestMap::instance();
  if (map.AddRequest(request)) {
    // A resolution request with the same key is already pending. Wait until
    // that one finishes and then complete this one with a copy of the results.
    return request;
  }
  // Early out if the target is an ipv4 or ipv6 literal.
  if (resolve_as_ip_literal_locked(name, default_port, addrs)) {
    map.OnResult(key);
    return request;
  }
  // Early out if the target is localhost and we're on Windows.
  if (grpc_ares_maybe_resolve_localhost_manually_locked(name, default_port,
                                                        addrs)) {
    map.OnResult(key);
    return request;
  }
  // Look up name using c-ares lib.
  map.LookUpWithAresLib(key);
  return request;
}

GrpcAresRequest* (*grpc_dns_lookup_ares_locked)(
    const char* dns_server, const char* name, const char* default_port,
    grpc_pollset_set* interested_parties, grpc_closure* on_done,
    grpc_core::UniquePtr<grpc_core::ServerAddressList>* addrs,
    bool check_grpclb, char** service_config_json, int query_timeout_ms,
    grpc_core::Combiner* combiner) = grpc_dns_lookup_ares_locked_impl;

static void grpc_ares_request_destroy_locked_impl(GrpcAresRequest* request) {
  grpc_core::Delete(request);
}

void (*grpc_ares_request_destroy_locked)(GrpcAresRequest* request) =
    grpc_ares_request_destroy_locked_impl;

static void grpc_cancel_ares_request_locked_impl(GrpcAresRequest* request) {
  GPR_ASSERT(request != nullptr);
  auto& map = GrpcAresPendingRequestMap::instance();
  map.CancelRequest(request);
}

void (*grpc_cancel_ares_request_locked)(GrpcAresRequest* request) =
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
  grpc_core::Combiner* combiner;
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
  GrpcAresRequest* ares_request = nullptr;
} grpc_resolve_address_ares_request;

static void on_dns_lookup_done_locked(void* arg, grpc_error* error) {
  grpc_resolve_address_ares_request* r =
      static_cast<grpc_resolve_address_ares_request*>(arg);
  grpc_ares_request_destroy_locked(r->ares_request);
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

static void on_dns_lookup_done(void* arg, grpc_error* error) {
  grpc_resolve_address_ares_request* r =
      static_cast<grpc_resolve_address_ares_request*>(arg);
  r->combiner->Run(GRPC_CLOSURE_INIT(&r->on_dns_lookup_done_locked,
                                     on_dns_lookup_done_locked, r, nullptr),
                   GRPC_ERROR_REF(error));
}

static void grpc_resolve_address_invoke_dns_lookup_ares_locked(
    void* arg, grpc_error* unused_error) {
  grpc_resolve_address_ares_request* r =
      static_cast<grpc_resolve_address_ares_request*>(arg);
  GRPC_CLOSURE_INIT(&r->on_dns_lookup_done_locked, on_dns_lookup_done, r,
                    grpc_schedule_on_exec_ctx);
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
  r->name = name;
  r->default_port = default_port;
  r->interested_parties = interested_parties;
  r->combiner->Run(
      GRPC_CLOSURE_CREATE(grpc_resolve_address_invoke_dns_lookup_ares_locked, r,
                          nullptr),
      GRPC_ERROR_NONE);
}

void (*grpc_resolve_address_ares)(
    const char* name, const char* default_port,
    grpc_pollset_set* interested_parties, grpc_closure* on_done,
    grpc_resolved_addresses** addrs) = grpc_resolve_address_ares_impl;

#endif /* GRPC_ARES == 1 */

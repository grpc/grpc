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

#include <string.h>
#include <sys/types.h>

#include <address_sorting/address_sorting.h>
#include <ares.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>

#include "src/core/ext/filters/client_channel/parse_address.h"
#include "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_ev_driver.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/gprpp/map.h"
#include "src/core/lib/gprpp/set.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/iomgr/iomgr_internal.h"
#include "src/core/lib/iomgr/nameser.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"

grpc_core::TraceFlag grpc_trace_cares_address_sorting(false,
                                                      "cares_address_sorting");
grpc_core::TraceFlag grpc_trace_cares_resolver(false, "cares_resolver");

namespace grpc_core {

constexpr char kServiceConfigAttributePrefix[] = "grpc_config=";

// The key that identifies a unique ares request. Two requests with the same
// request key are considered the same and can share the same response.
class RequestKey {
 public:
  RequestKey() = default;
  RequestKey(const char* dns_server, const char* name, const char* default_port,
             bool query_timeout_ms, bool check_grpclb,
             bool check_service_config)
      : dns_server_(UniquePtr<char>(gpr_strdup(dns_server))),
        name_(UniquePtr<char>(gpr_strdup(name))),
        default_port_(UniquePtr<char>(gpr_strdup(default_port))),
        query_timeout_ms_(query_timeout_ms),
        check_grpclb_(check_grpclb),
        check_service_config_(check_grpclb) {}

  RequestKey(const RequestKey& other)
      : dns_server_(UniquePtr<char>(gpr_strdup(other.dns_server_.get()))),
        name_(UniquePtr<char>(gpr_strdup(other.name_.get()))),
        default_port_(UniquePtr<char>(gpr_strdup(other.default_port_.get()))),
        query_timeout_ms_(other.query_timeout_ms_),
        check_grpclb_(other.check_grpclb_),
        check_service_config_(other.check_grpclb_) {}

  RequestKey& operator=(const RequestKey& other) {
    dns_server_.reset(gpr_strdup(other.dns_server_.get()));
    name_.reset(gpr_strdup(other.name_.get()));
    default_port_.reset(gpr_strdup(other.default_port_.get()));
    query_timeout_ms_ = other.query_timeout_ms_;
    check_grpclb_ = other.check_grpclb_;
    check_service_config_ = other.check_grpclb_;
    return *this;
  }

  const char* dns_server() { return dns_server_.get(); }
  const char* name() { return name_.get(); }
  int query_timeout_ms() { return query_timeout_ms_; }
  bool check_grpclb() { return check_grpclb_; }

  grpc_error* ParseName(UniquePtr<char>* host, UniquePtr<char>* port) {
    SplitHostPort(name_.get(), host, port);
    if (*host == nullptr) {
      return grpc_error_set_str(
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("unparseable host:port"),
          GRPC_ERROR_STR_TARGET_ADDRESS,
          grpc_slice_from_copied_string(name_.get()));
    }
    if (*port == nullptr) {
      if (default_port_ == nullptr) {
        return grpc_error_set_str(
            GRPC_ERROR_CREATE_FROM_STATIC_STRING("no port in name"),
            GRPC_ERROR_STR_TARGET_ADDRESS,
            grpc_slice_from_copied_string(name_.get()));
      }
      port->reset(gpr_strdup(default_port_.get()));
    }
    return GRPC_ERROR_NONE;
  }

  bool operator<(const RequestKey& other) const {
    int res = gpr_stricmp(dns_server_.get(), other.dns_server_.get());
    if (res != 0) return res < 0;
    res = gpr_stricmp(name_.get(), other.name_.get());
    if (res != 0) return res < 0;
    res = gpr_stricmp(default_port_.get(), other.default_port_.get());
    if (res != 0) return res < 0;
    res = query_timeout_ms_ - other.query_timeout_ms_;
    if (res != 0) return res < 0;
    if (check_grpclb_ != other.check_grpclb_) return check_grpclb_;
    if (check_service_config_ != other.check_service_config_) {
      return check_service_config_;
    }
    return false;
  }

 private:
  // Specific DNS server to query.
  UniquePtr<char> dns_server_;
  // target name to query.
  UniquePtr<char> name_;
  // default port to use.
  UniquePtr<char> default_port_;
  // query timeout.
  int query_timeout_ms_;
  // whether or not to query SRV records.
  bool check_grpclb_;
  // wether or not to query for service config TXT records.
  bool check_service_config_;
};

//
// AresRequestWrapper
//

// A wrapper to handle a resolution request with ares.
// TODO(apolcyn): Maybe consolidate this class with AresRequest.
class AresRequestWrapper {
 public:
  AresRequestWrapper(const char* name, const char* default_port,
                     grpc_pollset_set* interested_parties,
                     grpc_closure* on_done, grpc_resolved_addresses** addrs_out)
      : name(name),
        default_port(default_port),
        interested_parties(interested_parties),
        on_done(on_done),
        addrs_out(addrs_out),
        combiner(grpc_combiner_create()) {}

  static void StartResolving(const char* name, const char* default_port,
                             grpc_pollset_set* interested_parties,
                             grpc_closure* on_done,
                             grpc_resolved_addresses** addrs) {
    auto* request = New<AresRequestWrapper>(name, default_port,
                                            interested_parties, on_done, addrs);
    request->combiner->Run(
        GRPC_CLOSURE_CREATE(StartDnsLookupLocked, request, nullptr),
        GRPC_ERROR_NONE);
  }

 private:
  static void StartDnsLookupLocked(void* arg, grpc_error* unused_error) {
    auto* self = static_cast<AresRequestWrapper*>(arg);
    GRPC_CLOSURE_INIT(&self->on_dns_lookup_done_, OnDnsLookupDone, self,
                      grpc_schedule_on_exec_ctx);
    self->ares_request = grpc_dns_lookup_ares_locked(
        nullptr /* dns_server */, self->name, self->default_port,
        self->interested_parties, &self->on_dns_lookup_done_, &self->addresses,
        false /* check_grpclb */, nullptr /* service_config_json */,
        GRPC_DNS_ARES_DEFAULT_QUERY_TIMEOUT_MS, self->combiner);
  }

  static void OnDnsLookupDone(void* arg, grpc_error* error) {
    auto* self = static_cast<AresRequestWrapper*>(arg);
    self->combiner->Run(GRPC_CLOSURE_INIT(&self->on_dns_lookup_done_,
                                          OnDnsLookupDoneLocked, self, nullptr),
                        GRPC_ERROR_REF(error));
  }

  static void OnDnsLookupDoneLocked(void* arg, grpc_error* error) {
    auto* self = static_cast<AresRequestWrapper*>(arg);
    grpc_ares_request_destroy_locked(self->ares_request);
    grpc_resolved_addresses** resolved_addresses = self->addrs_out;
    if (self->addresses == nullptr || self->addresses->empty()) {
      *resolved_addresses = nullptr;
    } else {
      *resolved_addresses = static_cast<grpc_resolved_addresses*>(
          gpr_zalloc(sizeof(grpc_resolved_addresses)));
      (*resolved_addresses)->naddrs = self->addresses->size();
      (*resolved_addresses)->addrs =
          static_cast<grpc_resolved_address*>(gpr_zalloc(
              sizeof(grpc_resolved_address) * (*resolved_addresses)->naddrs));
      for (size_t i = 0; i < (*resolved_addresses)->naddrs; ++i) {
        GPR_ASSERT(!(*self->addresses)[i].IsBalancer());
        memcpy(&(*resolved_addresses)->addrs[i],
               &(*self->addresses)[i].address(), sizeof(grpc_resolved_address));
      }
    }
    GRPC_CLOSURE_SCHED(self->on_done, GRPC_ERROR_REF(error));
    GRPC_COMBINER_UNREF(self->combiner, "on_dns_lookup_done_cb");
    Delete(self);
  }

  // combiner that queries and related callbacks run under.
  Combiner* combiner;
  // the pointer to receive the resolved addresses.
  grpc_resolved_addresses** addrs_out;
  // currently resolving addresses.
  UniquePtr<ServerAddressList> addresses;
  // closure to call when the resolve_address_ares request completes.
  grpc_closure* on_done;
  /** a closure wrapping on_resolve_address_done, which should be invoked when
     the grpc_dns_lookup_ares_locked operation is done. */
  grpc_closure on_dns_lookup_done_;
  // target name.
  const char* name;
  // default port to use if none is specified.
  const char* default_port;
  // pollset_set to be driven by.
  grpc_pollset_set* interested_parties;
  // underlying ares_request that the query is performed on.
  AresRequest* ares_request = nullptr;
};

class AresRequest {
 public:
  AresRequest(UniquePtr<ServerAddressList>* addresses_out,
              char** service_config_json_out, grpc_closure* on_done,
              grpc_pollset_set* interested_party, Combiner* combiner)
      : 
        addresses_out(addresses_out),
        service_config_json_out(service_config_json_out),
        on_done(on_done),
        interested_party(interested_party),
        combiner(combiner) {}

  static void Destroy(AresRequest* request) { Delete(request); }

  static AresRequest* DnsLookupLocked(
      const char* dns_server, const char* name, const char* default_port,
      grpc_pollset_set* interested_parties, grpc_closure* on_done,
      UniquePtr<ServerAddressList>* addrs, bool check_grpclb,
      char** service_config_json, int query_timeout_ms, Combiner* combiner);

  // Not owned.
  RequestKey* key;

  // the pointer to receive the resolved addresses.
  UniquePtr<ServerAddressList>* addresses_out;
  // the pointer to receive the service config in JSON.
  char** service_config_json_out;
  // closure to call when the request completes.
  grpc_closure* on_done;
  grpc_pollset_set* interested_party;
  // synchronizes this resolution request.
  Combiner* combiner;

 private:
};

class AresRequestOnWire {
 public:
  AresRequestOnWire()
      : interested_parties_(grpc_pollset_set_create()),
        combiner_(grpc_combiner_create()) {
    memset(&dns_server_addr, 0, sizeof(dns_server_addr));
  }

  ~AresRequestOnWire() { grpc_pollset_set_destroy(interested_parties_); }

  void AddRequest(AresRequest* request);

  bool CancelRequest(AresRequest* request);

  static void OnTxtQueryDoneLocked(void* arg, int status, int timeouts,
                                   unsigned char* buf, int len);

  static void OnSrvQueryDoneLocked(void* arg, int status, int timeouts,
                                   unsigned char* buf, int len);

  void LookUpWithAresLib();

  void OnRequestDoneLocked();

  void Ref() { pending_queries++; }

  void Unref() {
    if (--pending_queries == 0u) {
      grpc_ares_ev_driver_on_queries_complete_locked(ev_driver);
    }
  }

  Combiner* combiner() { return combiner_; }
  const RequestKey& key() { return key_; }

  //   private:
  // indicates the DNS server to use, if specified.
  struct ares_addr_port_node dns_server_addr;
  // the event driver used by this request.
  grpc_ares_ev_driver* ev_driver = nullptr;
  // number of ongoing queries.
  size_t pending_queries = 0;
  // the errors explaining query failures, appended to in query callbacks.
  grpc_error* error = GRPC_ERROR_NONE;
  // requests sharing the result of this instance.
  Set<AresRequest*> requests_;

  // the resolved addresses.
  UniquePtr<ServerAddressList> resolved_addresses_;
  // the service config in JSON.
  char* resolved_service_config_json = nullptr;

  grpc_pollset_set* interested_parties_;
  RequestKey key_;
  Combiner* combiner_;
};

class RequestMap {
 public:
  class ResultUpdater {
   public:
    static void UpdateLocked(void* arg, grpc_error* error) {
      auto* self = static_cast<ResultUpdater*>(arg);
      if (self->request->service_config_json_out != nullptr) {
        *self->request->service_config_json_out = self->service_config_json;
      } else {
        GPR_ASSERT(self->service_config_json == nullptr);
      }
      *self->request->addresses_out = std::move(self->address_list);
      GRPC_COMBINER_UNREF(self->request->combiner, "complete request locked");
      GRPC_ERROR_REF(error);
      GRPC_CLOSURE_SCHED(self->request->on_done, error);
      Delete(self);
    }

    //   private:
    // Owned until passed to the request.
    UniquePtr<ServerAddressList> address_list;
    char* service_config_json;

    AresRequest* request;
  };

  // FIXME: need sync
  static RequestMap& instance() {
    if (instance_ == nullptr) instance_ = New<RequestMap>();
    return *instance_;
  }

  static void CancelRequestImpl(AresRequest* request) {
    GPR_ASSERT(request != nullptr);
    instance().CancelRequest(request);
  }

  bool AddRequest(const RequestKey& key, AresRequest* request) {
    MutexLock lock(&mu_);
    auto iter = map_.find(key);
    bool already_exists = iter != map_.end();
    GRPC_CARES_TRACE_LOG("request:%p already_exists:%d", request,
                         already_exists);
    if (!already_exists) {
      map_[key].key_ = key;
      iter = map_.find(key);
    }
    auto& wrapper = iter->second;
    wrapper.AddRequest(request);
    return already_exists;
  }

  void CancelRequest(AresRequest* request) {
    MutexLock lock(&mu_);
    auto iter = map_.find(*request->key);
    auto& wrapper = iter->second;
    if (wrapper.CancelRequest(request)) {
      map_.erase(iter);
    }
  }

  void LookUpWithAresLib(const RequestKey& key);

  void OnResult(const RequestKey& key) {
    MutexLock lock(&mu_);
    auto iter = map_.find(key);
    iter->second.OnRequestDoneLocked();
    map_.erase(iter);
  }

 private:
  static RequestMap* instance_;

  Map<RequestKey, AresRequestOnWire> map_;
  Mutex mu_;
};

class HostByNameRequest {
 public:
  HostByNameRequest(AresRequestOnWire* parent_request, char* host,
                    uint16_t port, bool is_balancer)
      : parent_request(parent_request),
        host(host),
        port(port),
        is_balancer(is_balancer) {
    GRPC_CARES_TRACE_LOG(
        "request:%p create_hostbyname_request_locked host:%s port:%d "
        "is_balancer:%d",
        parent_request, host, port, is_balancer);
    parent_request->Ref();
  }

  ~HostByNameRequest() {
    parent_request->Unref();
    gpr_free(host);
  }

  static void OnHostbynameDoneLocked(void* arg, int status, int timeouts,
                                     struct hostent* hostent) {
    HostByNameRequest* hr = static_cast<HostByNameRequest*>(arg);
    AresRequestOnWire* wrapper = hr->parent_request;
    if (status == ARES_SUCCESS) {
      GRPC_CARES_TRACE_LOG(
          "request:%p OnHostbynameDoneLocked host=%s ARES_SUCCESS", wrapper,
          hr->host);
      if (wrapper->resolved_addresses_ == nullptr) {
        wrapper->resolved_addresses_ = MakeUnique<ServerAddressList>();
      }
      ServerAddressList& addresses = *wrapper->resolved_addresses_;
      for (size_t i = 0; hostent->h_addr_list[i] != nullptr; ++i) {
        InlinedVector<grpc_arg, 2> args_to_add;
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
                wrapper, output, ntohs(hr->port), addr.sin6_scope_id);
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
                wrapper, output, ntohs(hr->port));
            break;
          }
        }
      }
    } else {
      char* error_msg;
      gpr_asprintf(&error_msg, "C-ares status is not ARES_SUCCESS: %s",
                   ares_strerror(status));
      GRPC_CARES_TRACE_LOG("request:%p OnHostbynameDoneLocked host=%s %s",
                           wrapper, hr->host, error_msg);
      grpc_error* error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(error_msg);
      gpr_free(error_msg);
      wrapper->error = grpc_error_add_child(error, wrapper->error);
    }
    Delete(hr);
  }

  // the top-level request instance.
  AresRequestOnWire* parent_request;
  // host to resolve, parsed from the name to resolve.
  char* host;
  // port to fill in sockaddr_in, parsed from the name to resolve.
  uint16_t port;
  // is it a grpclb address.
  bool is_balancer;
};

RequestMap* RequestMap::instance_ = nullptr;

void RequestMap::LookUpWithAresLib(const RequestKey& key) {
  auto& wrapper = map_.find(key)->second;
  wrapper.LookUpWithAresLib();
}

namespace {

#ifdef GRPC_ARES_RESOLVE_LOCALHOST_MANUALLY

bool MaybeResolveLocalHostManuallyLocked(const char* name,
                                         const char* default_port,
                                         UniquePtr<ServerAddressList>* addrs) {
  UniquePtr<char> host;
  UniquePtr<char> port;
  SplitHostPort(name, &host, &port);
  if (host == nullptr) {
    gpr_log(GPR_ERROR,
            "Failed to parse %s into host:port during manual localhost "
            "resolution check.",
            name);
    return false;
  }
  if (port == nullptr) {
    if (default_port == nullptr) {
      gpr_log(GPR_ERROR,
              "No port or default port for %s during manual localhost "
              "resolution check.",
              name);
      return false;
    }
    port.reset(gpr_strdup(default_port));
  }
  if (gpr_stricmp(host.get(), "localhost") == 0) {
    GPR_ASSERT(*addrs == nullptr);
    *addrs = MakeUnique<ServerAddressList>();
    uint16_t numeric_port = grpc_strhtons(port.get());
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
#else  // GRPC_ARES_RESOLVE_LOCALHOST_MANUALLY.
bool MaybeResolveLocalHostManuallyLocked(const char* name,
                                         const char* default_port,
                                         UniquePtr<ServerAddressList>* addrs) {
  return false;
}
#endif // GRPC_ARES_RESOLVE_LOCALHOST_MANUALLY.

void LogAddresses(const ServerAddressList& addresses,
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

bool ResolveAsIpLiteralLocked(const char* name, const char* default_port,
                              UniquePtr<ServerAddressList>* addrs) {
  UniquePtr<char> host;
  UniquePtr<char> port;
  UniquePtr<char> hostport;
  SplitHostPort(name, &host, &port);
  if (host == nullptr) {
    gpr_log(GPR_ERROR,
            "Failed to parse %s to host:port while attempting to resolve as ip "
            "literal.",
            name);
    return false;
  }
  if (port == nullptr) {
    if (default_port == nullptr) {
      gpr_log(GPR_ERROR,
              "No port or default port for %s while attempting to resolve as "
              "ip literal.",
              name);
      return false;
    }
    port.reset(gpr_strdup(default_port));
  }
  grpc_resolved_address addr;
  GPR_ASSERT(JoinHostPort(&hostport, host.get(), atoi(port.get())));
  if (grpc_parse_ipv4_hostport(hostport.get(), &addr, false /* log errors */) ||
      grpc_parse_ipv6_hostport(hostport.get(), &addr, false /* log errors */)) {
    GPR_ASSERT(*addrs == nullptr);
    *addrs = MakeUnique<ServerAddressList>();
    (*addrs)->emplace_back(addr.addr, addr.len, nullptr /* args */);
    return true;
  }
  return false;
}

bool TargetMatchesLocalhost(const char* name) {
  UniquePtr<char> host;
  UniquePtr<char> port;
  if (!SplitHostPort(name, &host, &port)) {
    gpr_log(GPR_ERROR, "Unable to split host and port for name: %s", name);
    return false;
  }
  return gpr_stricmp(host.get(), "localhost") == 0;
}

}  // namespace

AresRequest* AresRequest::DnsLookupLocked(
    const char* dns_server, const char* name, const char* default_port,
    grpc_pollset_set* interested_parties, grpc_closure* on_done,
    UniquePtr<ServerAddressList>* addrs, bool check_grpclb,
    char** service_config_json, int query_timeout_ms, Combiner* combiner) {
  // Don't query for SRV and TXT records if the target is "localhost", so
  // as to cut down on lookups over the network, especially in tests:
  // https://github.com/grpc/proposal/pull/79
  if (TargetMatchesLocalhost(name)) {
    check_grpclb = false;
    service_config_json = nullptr;
  }
  RequestKey key(
      dns_server, name, default_port, query_timeout_ms, check_grpclb,
      service_config_json != nullptr /* check service config */);
  auto* request = New<AresRequest>(addrs, service_config_json, on_done, interested_parties,
      GRPC_COMBINER_REF(combiner, "dns lookup ares begin"));
  GRPC_CARES_TRACE_LOG(
      "request:%p c-ares DnsLookupLocked dns_server=%s "
      "name=%s, "
      "default_port=%s, check_grpclb=%d, **service_config_json=%p "
      "query_timeout_ms=%d",
      request, dns_server, name, default_port, check_grpclb,
      service_config_json, query_timeout_ms);
  auto& map = RequestMap::instance();
  if (map.AddRequest(key, request)) {
    // A resolution request with the same key is already pending. Wait until
    // that one finishes and then complete this one with a copy of the results.
    return request;
  }
  // Early out if the target is an ipv4 or ipv6 literal.
  if (ResolveAsIpLiteralLocked(name, default_port, addrs)) {
    map.OnResult(key);
    return request;
  }
  // Early out if the target is localhost and we're on Windows.
  if (MaybeResolveLocalHostManuallyLocked(name, default_port, addrs)) {
    map.OnResult(key);
    return request;
  }
  // Look up name using c-ares lib.
  map.LookUpWithAresLib(key);
  return request;
}

void AresRequestOnWire::AddRequest(AresRequest* request) {
  grpc_pollset_set_add_pollset_set(interested_parties_,
                                   request->interested_party);
  requests_.insert(request);
  // FIXME
}

bool AresRequestOnWire::CancelRequest(AresRequest* request) {
  // FIXME: run on_done?
  OnRequestDoneLocked();
  Delete(request);
  requests_.erase(request);
  // FIXME
  grpc_pollset_set_del_pollset_set(interested_parties_,
                                   request->interested_party);
  // FIXME: other cleanup if empty
  return requests_.empty();
}

void AresRequestOnWire::LookUpWithAresLib() {
  HostByNameRequest* hr = nullptr;
  ares_channel* channel = nullptr;
  UniquePtr<char> host;
  UniquePtr<char> port;
  grpc_error* local_error = key_.ParseName(&host, &port);
  if (local_error != GRPC_ERROR_NONE) goto error_cleanup;
  local_error = grpc_ares_ev_driver_create_locked(
      &ev_driver, interested_parties_, key_.query_timeout_ms(), combiner(),
      this);
  if (local_error != GRPC_ERROR_NONE) goto error_cleanup;
  channel = grpc_ares_ev_driver_get_channel_locked(ev_driver);
  // If dns_server is specified, use it.
  if (key_.dns_server() != nullptr) {
    GRPC_CARES_TRACE_LOG("request:%p Using DNS server %s", this,
                         key_.dns_server());
    grpc_resolved_address addr;
    if (grpc_parse_ipv4_hostport(key_.dns_server(), &addr,
                                 false /* log_errors */)) {
      dns_server_addr.family = AF_INET;
      auto* in = reinterpret_cast<struct sockaddr_in*>(addr.addr);
      memcpy(&dns_server_addr.addr.addr4, &in->sin_addr,
             sizeof(struct in_addr));
      dns_server_addr.tcp_port = grpc_sockaddr_get_port(&addr);
      dns_server_addr.udp_port = grpc_sockaddr_get_port(&addr);
    } else if (grpc_parse_ipv6_hostport(key_.dns_server(), &addr,
                                        false /* log_errors */)) {
      dns_server_addr.family = AF_INET6;
      auto* in6 = reinterpret_cast<struct sockaddr_in6*>(addr.addr);
      memcpy(&dns_server_addr.addr.addr6, &in6->sin6_addr,
             sizeof(struct in6_addr));
      dns_server_addr.tcp_port = grpc_sockaddr_get_port(&addr);
      dns_server_addr.udp_port = grpc_sockaddr_get_port(&addr);
    } else {
      local_error = grpc_error_set_str(
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("cannot parse authority"),
          GRPC_ERROR_STR_TARGET_ADDRESS,
          grpc_slice_from_copied_string(key_.name()));
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
    hr = New<HostByNameRequest>(this, host.get(), grpc_strhtons(port.get()),
                                /*is_balancer=*/false);
    ares_gethostbyname(*channel, hr->host, AF_INET6,
                       HostByNameRequest::OnHostbynameDoneLocked, hr);
  }
  hr = New<HostByNameRequest>(this, host.get(), grpc_strhtons(port.get()),
                              /*is_balancer=*/false);
  ares_gethostbyname(*channel, hr->host, AF_INET,
                     HostByNameRequest::OnHostbynameDoneLocked, hr);
  if (key_.check_grpclb()) {
    // Query the SRV record.
    Ref();
    char* service_name;
    gpr_asprintf(&service_name, "_grpclb._tcp.%s", host.get());
    ares_query(*channel, service_name, ns_c_in, ns_t_srv,
               AresRequestOnWire::OnSrvQueryDoneLocked, this);
    gpr_free(service_name);
  }
  if (resolved_service_config_json != nullptr) {
    // Query the TXT record.
    Ref();
    char* config_name;
    gpr_asprintf(&config_name, "_grpc_config.%s", host.get());
    ares_search(*channel, config_name, ns_c_in, ns_t_txt, OnTxtQueryDoneLocked,
                this);
    gpr_free(config_name);
  }
  grpc_ares_ev_driver_start_locked(ev_driver);
  Unref();
  return;
error_cleanup:
  error = local_error;
  OnRequestDoneLocked();
}

void AresRequestOnWire::OnRequestDoneLocked() {
  ev_driver = nullptr;
  ServerAddressList* addresses = resolved_addresses_.get();
  // Sort the addresses.
  if (addresses != nullptr) {
    grpc_cares_wrapper_address_sorting_sort(addresses);
    GRPC_ERROR_UNREF(error);
    error = GRPC_ERROR_NONE;
    // TODO(apolcyn): allow c-ares to return a service config
    // with no addresses along side it
  }
  // Copy and pass result to each request.
  for (auto& request : requests_) {
    auto* updater = New<RequestMap::ResultUpdater>();
    if (addresses != nullptr) {
      updater->address_list = MakeUnique<ServerAddressList>(*addresses);
    }
    updater->service_config_json = gpr_strdup(resolved_service_config_json);
    updater->request = request;
    GRPC_ERROR_REF(error);
    GRPC_CARES_TRACE_LOG("cur_request:%p OnRequestDoneLocked", request);
    request->combiner->Run(
        GRPC_CLOSURE_CREATE(RequestMap::ResultUpdater::UpdateLocked, updater,
                            nullptr),
        error);
  }
  // Clean up.
  GRPC_ERROR_UNREF(error);
  resolved_addresses_.reset();
  gpr_free(resolved_service_config_json);
}

void AresRequestOnWire::OnTxtQueryDoneLocked(void* arg, int status,
                                             int /*timeouts*/,
                                             unsigned char* buf, int len) {
  char* error_msg;
  AresRequestOnWire* self = static_cast<AresRequestOnWire*>(arg);
  const size_t prefix_len = sizeof(kServiceConfigAttributePrefix) - 1;
  struct ares_txt_ext* result = nullptr;
  struct ares_txt_ext* reply = nullptr;
  grpc_error* error = GRPC_ERROR_NONE;
  if (status != ARES_SUCCESS) goto fail;
  GRPC_CARES_TRACE_LOG("request:%p OnTxtQueryDoneLocked ARES_SUCCESS", self);
  status = ares_parse_txt_reply_ext(buf, len, &reply);
  if (status != ARES_SUCCESS) goto fail;
  // Find service config in TXT record.
  for (result = reply; result != nullptr; result = result->next) {
    if (result->record_start &&
        memcmp(result->txt, kServiceConfigAttributePrefix, prefix_len) == 0) {
      break;
    }
  }
  // Found a service config record.
  if (result != nullptr) {
    size_t service_config_len = result->length - prefix_len;
    self->resolved_service_config_json =
        static_cast<char*>(gpr_malloc(service_config_len + 1));
    memcpy(self->resolved_service_config_json, result->txt + prefix_len,
           service_config_len);
    for (result = result->next; result != nullptr && !result->record_start;
         result = result->next) {
      self->resolved_service_config_json = static_cast<char*>(
          gpr_realloc(self->resolved_service_config_json,
                      service_config_len + result->length + 1));
      memcpy(self->resolved_service_config_json + service_config_len,
             result->txt, result->length);
      service_config_len += result->length;
    }
    (self->resolved_service_config_json)[service_config_len] = '\0';
    GRPC_CARES_TRACE_LOG("request:%p found service config: %s", self,
                         self->resolved_service_config_json);
  }
  // Clean up.
  ares_free_data(reply);
  goto done;
fail:
  gpr_asprintf(&error_msg, "C-ares TXT lookup status is not ARES_SUCCESS: %s",
               ares_strerror(status));
  error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(error_msg);
  GRPC_CARES_TRACE_LOG("request:%p OnTxtQueryDoneLocked %s", self, error_msg);
  gpr_free(error_msg);
  self->error = grpc_error_add_child(error, self->error);
done:
  self->Unref();
}

void AresRequestOnWire::OnSrvQueryDoneLocked(void* arg, int status,
                                             int /*timeouts*/,
                                             unsigned char* buf, int len) {
  AresRequestOnWire* self = static_cast<AresRequestOnWire*>(arg);
  if (status == ARES_SUCCESS) {
    GRPC_CARES_TRACE_LOG("request:%p OnSrvQueryDoneLocked ARES_SUCCESS", self);
    struct ares_srv_reply* reply;
    const int parse_status = ares_parse_srv_reply(buf, len, &reply);
    GRPC_CARES_TRACE_LOG("request:%p ares_parse_srv_reply: %d", self,
                         parse_status);
    if (parse_status == ARES_SUCCESS) {
      ares_channel* channel =
          grpc_ares_ev_driver_get_channel_locked(self->ev_driver);
      for (struct ares_srv_reply* srv_it = reply; srv_it != nullptr;
           srv_it = srv_it->next) {
        if (grpc_ares_query_ipv6()) {
          auto* hr = New<HostByNameRequest>(
              self, srv_it->host, htons(srv_it->port), true /* is_balancer */);
          ares_gethostbyname(*channel, hr->host, AF_INET6,
                             HostByNameRequest::OnHostbynameDoneLocked, hr);
        }
        auto* hr = New<HostByNameRequest>(
            self, srv_it->host, htons(srv_it->port), true /* is_balancer */);
        ares_gethostbyname(*channel, hr->host, AF_INET,
                           HostByNameRequest::OnHostbynameDoneLocked, hr);
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
    GRPC_CARES_TRACE_LOG("request:%p OnSrvQueryDoneLocked %s", self, error_msg);
    grpc_error* error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(error_msg);
    gpr_free(error_msg);
    self->error = grpc_error_add_child(error, self->error);
  }
  self->Unref();
}

}  // namespace grpc_core

void (*grpc_resolve_address_ares)(const char* name, const char* default_port,
                                  grpc_pollset_set* interested_parties,
                                  grpc_closure* on_done,
                                  grpc_resolved_addresses** addrs) =
    grpc_core::AresRequestWrapper::StartResolving;

grpc_core::AresRequest* (*grpc_dns_lookup_ares_locked)(
    const char* dns_server, const char* name, const char* default_port,
    grpc_pollset_set* interested_parties, grpc_closure* on_done,
    grpc_core::UniquePtr<grpc_core::ServerAddressList>* addrs,
    bool check_grpclb, char** service_config_json, int query_timeout_ms,
    grpc_core::Combiner* combiner) = grpc_core::AresRequest::DnsLookupLocked;

void (*grpc_cancel_ares_request_locked)(grpc_core::AresRequest* request) =
    grpc_core::RequestMap::CancelRequestImpl;

void (*grpc_ares_request_destroy_locked)(grpc_core::AresRequest* request) =
    grpc_core::AresRequest::Destroy;

void GrpcAresRequestDoneLocked(grpc_core::AresRequestOnWire* wrapper) {
  auto& map = grpc_core::RequestMap::instance();
  map.OnResult(wrapper->key());
}

void grpc_cares_wrapper_address_sorting_sort(
    grpc_core::ServerAddressList* addresses) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_cares_address_sorting)) {
    grpc_core::LogAddresses(*addresses, "input");
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
  grpc_core::ServerAddressList sorted;
  sorted.reserve(addresses->size());
  for (size_t i = 0; i < addresses->size(); ++i) {
    sorted.emplace_back(
        *static_cast<grpc_core::ServerAddress*>(sortables[i].user_data));
  }
  gpr_free(sortables);
  *addresses = std::move(sorted);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_cares_address_sorting)) {
    grpc_core::LogAddresses(*addresses, "output");
  }
}

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

#endif // GRPC_ARES == 1.

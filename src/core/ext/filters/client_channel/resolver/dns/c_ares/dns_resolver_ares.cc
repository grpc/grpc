//
// Copyright 2015 gRPC authors.
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
//

#include <grpc/support/port_platform.h>

#if GRPC_ARES == 1

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include <address_sorting/address_sorting.h>

#include "absl/container/inlined_vector.h"
#include "absl/strings/str_cat.h"

#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/filters/client_channel/http_connect_handshaker.h"
#include "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_balancer_addresses.h"
#include "src/core/ext/filters/client_channel/lb_policy_registry.h"
#include "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper.h"
#include "src/core/ext/filters/client_channel/resolver/dns/dns_resolver_selection.h"
#include "src/core/ext/filters/client_channel/resolver_registry.h"
#include "src/core/ext/filters/client_channel/server_address.h"
#include "src/core/ext/service_config/service_config.h"
#include "src/core/lib/backoff/backoff.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/manual_constructor.h"
#include "src/core/lib/iomgr/gethostname.h"
#include "src/core/lib/iomgr/iomgr_custom.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/iomgr/work_serializer.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/transport/error_utils.h"

#define GRPC_DNS_INITIAL_CONNECT_BACKOFF_SECONDS 1
#define GRPC_DNS_RECONNECT_BACKOFF_MULTIPLIER 1.6
#define GRPC_DNS_RECONNECT_MAX_BACKOFF_SECONDS 120
#define GRPC_DNS_RECONNECT_JITTER 0.2

namespace grpc_core {

namespace {

class AresClientChannelDNSResolver : public Resolver {
 public:
  explicit AresClientChannelDNSResolver(ResolverArgs args);

  void StartLocked() override;

  void RequestReresolutionLocked() override;

  void ResetBackoffLocked() override;

  void ShutdownLocked() override;

 private:
  ~AresClientChannelDNSResolver() override;

  void MaybeStartResolvingLocked();
  void StartResolvingLocked();

  static void OnNextResolution(void* arg, grpc_error_handle error);
  static void OnResolved(void* arg, grpc_error_handle error);
  void OnNextResolutionLocked(grpc_error_handle error);
  void OnResolvedLocked(grpc_error_handle error);

  /// DNS server to use (if not system default)
  std::string dns_server_;
  /// name to resolve (usually the same as target_name)
  std::string name_to_resolve_;
  /// channel args
  grpc_channel_args* channel_args_;
  std::shared_ptr<WorkSerializer> work_serializer_;
  std::unique_ptr<ResultHandler> result_handler_;
  /// pollset_set to drive the name resolution process
  grpc_pollset_set* interested_parties_;

  /// whether to request the service config
  bool request_service_config_;
  // whether or not to enable SRV DNS queries
  bool enable_srv_queries_;
  // timeout in milliseconds for active DNS queries
  int query_timeout_ms_;
  /// min interval between DNS requests
  grpc_millis min_time_between_resolutions_;

  /// closures used by the work_serializer
  grpc_closure on_next_resolution_;
  grpc_closure on_resolved_;
  /// are we currently resolving?
  bool resolving_ = false;
  /// the pending resolving request
  grpc_ares_request* pending_request_ = nullptr;
  /// next resolution timer
  bool have_next_resolution_timer_ = false;
  grpc_timer next_resolution_timer_;
  /// timestamp of last DNS request
  grpc_millis last_resolution_timestamp_ = -1;
  /// retry backoff state
  BackOff backoff_;
  /// currently resolving backend addresses
  std::unique_ptr<ServerAddressList> addresses_;
  /// currently resolving balancer addresses
  std::unique_ptr<ServerAddressList> balancer_addresses_;
  /// currently resolving service config
  char* service_config_json_ = nullptr;
  // has shutdown been initiated
  bool shutdown_initiated_ = false;
};

AresClientChannelDNSResolver::AresClientChannelDNSResolver(ResolverArgs args)
    : dns_server_(args.uri.authority()),
      name_to_resolve_(absl::StripPrefix(args.uri.path(), "/")),
      channel_args_(grpc_channel_args_copy(args.args)),
      work_serializer_(std::move(args.work_serializer)),
      result_handler_(std::move(args.result_handler)),
      interested_parties_(args.pollset_set),
      request_service_config_(!grpc_channel_args_find_bool(
          channel_args_, GRPC_ARG_SERVICE_CONFIG_DISABLE_RESOLUTION, true)),
      enable_srv_queries_(grpc_channel_args_find_bool(
          channel_args_, GRPC_ARG_DNS_ENABLE_SRV_QUERIES, false)),
      query_timeout_ms_(grpc_channel_args_find_integer(
          channel_args_, GRPC_ARG_DNS_ARES_QUERY_TIMEOUT_MS,
          {GRPC_DNS_ARES_DEFAULT_QUERY_TIMEOUT_MS, 0, INT_MAX})),
      min_time_between_resolutions_(grpc_channel_args_find_integer(
          channel_args_, GRPC_ARG_DNS_MIN_TIME_BETWEEN_RESOLUTIONS_MS,
          {1000 * 30, 0, INT_MAX})),
      backoff_(
          BackOff::Options()
              .set_initial_backoff(GRPC_DNS_INITIAL_CONNECT_BACKOFF_SECONDS *
                                   1000)
              .set_multiplier(GRPC_DNS_RECONNECT_BACKOFF_MULTIPLIER)
              .set_jitter(GRPC_DNS_RECONNECT_JITTER)
              .set_max_backoff(GRPC_DNS_RECONNECT_MAX_BACKOFF_SECONDS * 1000)) {
  // Closure initialization.
  GRPC_CLOSURE_INIT(&on_next_resolution_, OnNextResolution, this,
                    grpc_schedule_on_exec_ctx);
  GRPC_CLOSURE_INIT(&on_resolved_, OnResolved, this, grpc_schedule_on_exec_ctx);
}

AresClientChannelDNSResolver::~AresClientChannelDNSResolver() {
  GRPC_CARES_TRACE_LOG("resolver:%p destroying AresClientChannelDNSResolver",
                       this);
  grpc_channel_args_destroy(channel_args_);
}

void AresClientChannelDNSResolver::StartLocked() {
  GRPC_CARES_TRACE_LOG(
      "resolver:%p AresClientChannelDNSResolver::StartLocked() is called.",
      this);
  MaybeStartResolvingLocked();
}

void AresClientChannelDNSResolver::RequestReresolutionLocked() {
  if (!resolving_) {
    MaybeStartResolvingLocked();
  }
}

void AresClientChannelDNSResolver::ResetBackoffLocked() {
  if (have_next_resolution_timer_) {
    grpc_timer_cancel(&next_resolution_timer_);
  }
  backoff_.Reset();
}

void AresClientChannelDNSResolver::ShutdownLocked() {
  shutdown_initiated_ = true;
  if (have_next_resolution_timer_) {
    grpc_timer_cancel(&next_resolution_timer_);
  }
  if (pending_request_ != nullptr) {
    grpc_cancel_ares_request(pending_request_);
  }
}

void AresClientChannelDNSResolver::OnNextResolution(void* arg,
                                                    grpc_error_handle error) {
  AresClientChannelDNSResolver* r =
      static_cast<AresClientChannelDNSResolver*>(arg);
  (void)GRPC_ERROR_REF(error);  // ref owned by lambda
  r->work_serializer_->Run([r, error]() { r->OnNextResolutionLocked(error); },
                           DEBUG_LOCATION);
}

void AresClientChannelDNSResolver::OnNextResolutionLocked(
    grpc_error_handle error) {
  GRPC_CARES_TRACE_LOG(
      "resolver:%p re-resolution timer fired. error: %s. shutdown_initiated_: "
      "%d",
      this, grpc_error_std_string(error).c_str(), shutdown_initiated_);
  have_next_resolution_timer_ = false;
  if (error == GRPC_ERROR_NONE && !shutdown_initiated_) {
    if (!resolving_) {
      GRPC_CARES_TRACE_LOG(
          "resolver:%p start resolving due to re-resolution timer", this);
      StartResolvingLocked();
    }
  }
  Unref(DEBUG_LOCATION, "next_resolution_timer");
  GRPC_ERROR_UNREF(error);
}

bool ValueInJsonArray(const Json::Array& array, const char* value) {
  for (const Json& entry : array) {
    if (entry.type() == Json::Type::STRING && entry.string_value() == value) {
      return true;
    }
  }
  return false;
}

std::string ChooseServiceConfig(char* service_config_choice_json,
                                grpc_error_handle* error) {
  Json json = Json::Parse(service_config_choice_json, error);
  if (*error != GRPC_ERROR_NONE) return "";
  if (json.type() != Json::Type::ARRAY) {
    *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Service Config Choices, error: should be of type array");
    return "";
  }
  const Json* service_config = nullptr;
  absl::InlinedVector<grpc_error_handle, 4> error_list;
  for (const Json& choice : json.array_value()) {
    if (choice.type() != Json::Type::OBJECT) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "Service Config Choice, error: should be of type object"));
      continue;
    }
    // Check client language, if specified.
    auto it = choice.object_value().find("clientLanguage");
    if (it != choice.object_value().end()) {
      if (it->second.type() != Json::Type::ARRAY) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:clientLanguage error:should be of type array"));
      } else if (!ValueInJsonArray(it->second.array_value(), "c++")) {
        continue;
      }
    }
    // Check client hostname, if specified.
    it = choice.object_value().find("clientHostname");
    if (it != choice.object_value().end()) {
      if (it->second.type() != Json::Type::ARRAY) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:clientHostname error:should be of type array"));
      } else {
        char* hostname = grpc_gethostname();
        if (hostname == nullptr ||
            !ValueInJsonArray(it->second.array_value(), hostname)) {
          continue;
        }
      }
    }
    // Check percentage, if specified.
    it = choice.object_value().find("percentage");
    if (it != choice.object_value().end()) {
      if (it->second.type() != Json::Type::NUMBER) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:percentage error:should be of type number"));
      } else {
        int random_pct = rand() % 100;
        int percentage;
        if (sscanf(it->second.string_value().c_str(), "%d", &percentage) != 1) {
          error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
              "field:percentage error:should be of type integer"));
        } else if (random_pct > percentage || percentage == 0) {
          continue;
        }
      }
    }
    // Found service config.
    it = choice.object_value().find("serviceConfig");
    if (it == choice.object_value().end()) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:serviceConfig error:required field missing"));
    } else if (it->second.type() != Json::Type::OBJECT) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:serviceConfig error:should be of type object"));
    } else if (service_config == nullptr) {
      service_config = &it->second;
    }
  }
  if (!error_list.empty()) {
    service_config = nullptr;
    *error = GRPC_ERROR_CREATE_FROM_VECTOR("Service Config Choices Parser",
                                           &error_list);
  }
  if (service_config == nullptr) return "";
  return service_config->Dump();
}

void AresClientChannelDNSResolver::OnResolved(void* arg,
                                              grpc_error_handle error) {
  AresClientChannelDNSResolver* r =
      static_cast<AresClientChannelDNSResolver*>(arg);
  (void)GRPC_ERROR_REF(error);  // ref owned by lambda
  r->work_serializer_->Run([r, error]() { r->OnResolvedLocked(error); },
                           DEBUG_LOCATION);
}

void AresClientChannelDNSResolver::OnResolvedLocked(grpc_error_handle error) {
  GPR_ASSERT(resolving_);
  resolving_ = false;
  delete pending_request_;
  pending_request_ = nullptr;
  if (shutdown_initiated_) {
    Unref(DEBUG_LOCATION, "OnResolvedLocked() shutdown");
    GRPC_ERROR_UNREF(error);
    return;
  }
  // TODO(roth): Change logic to be able to report failures for addresses
  // and service config independently of each other.
  if (addresses_ != nullptr || balancer_addresses_ != nullptr) {
    Result result;
    if (addresses_ != nullptr) {
      result.addresses = std::move(*addresses_);
    } else {
      result.addresses = ServerAddressList();
    }
    if (service_config_json_ != nullptr) {
      grpc_error_handle service_config_error = GRPC_ERROR_NONE;
      std::string service_config_string =
          ChooseServiceConfig(service_config_json_, &service_config_error);
      gpr_free(service_config_json_);
      RefCountedPtr<ServiceConfig> service_config;
      if (service_config_error == GRPC_ERROR_NONE &&
          !service_config_string.empty()) {
        GRPC_CARES_TRACE_LOG("resolver:%p selected service config choice: %s",
                             this, service_config_string.c_str());
        service_config = ServiceConfig::Create(
            channel_args_, service_config_string, &service_config_error);
      }
      if (service_config_error != GRPC_ERROR_NONE) {
        result.service_config = absl::UnavailableError(
            absl::StrCat("failed to parse service config: ",
                         grpc_error_std_string(service_config_error)));
        GRPC_ERROR_UNREF(service_config_error);
      } else {
        result.service_config = std::move(service_config);
      }
    }
    absl::InlinedVector<grpc_arg, 1> new_args;
    if (balancer_addresses_ != nullptr) {
      new_args.push_back(
          CreateGrpclbBalancerAddressesArg(balancer_addresses_.get()));
    }
    result.args = grpc_channel_args_copy_and_add(channel_args_, new_args.data(),
                                                 new_args.size());
    result_handler_->ReportResult(std::move(result));
    addresses_.reset();
    balancer_addresses_.reset();
    // Reset backoff state so that we start from the beginning when the
    // next request gets triggered.
    backoff_.Reset();
  } else {
    GRPC_CARES_TRACE_LOG("resolver:%p dns resolution failed: %s", this,
                         grpc_error_std_string(error).c_str());
    std::string error_message;
    grpc_error_get_str(error, GRPC_ERROR_STR_DESCRIPTION, &error_message);
    absl::Status status = absl::UnavailableError(absl::StrCat(
        "DNS resolution failed for ", name_to_resolve_, ": ", error_message));
    Result result;
    result.addresses = status;
    result.service_config = status;
    result.args = grpc_channel_args_copy(channel_args_);
    result_handler_->ReportResult(std::move(result));
    // Set retry timer.
    // InvalidateNow to avoid getting stuck re-initializing this timer
    // in a loop while draining the currently-held WorkSerializer.
    // Also see https://github.com/grpc/grpc/issues/26079.
    ExecCtx::Get()->InvalidateNow();
    grpc_millis next_try = backoff_.NextAttemptTime();
    grpc_millis timeout = next_try - ExecCtx::Get()->Now();
    GRPC_CARES_TRACE_LOG("resolver:%p dns resolution failed (will retry): %s",
                         this, grpc_error_std_string(error).c_str());
    GPR_ASSERT(!have_next_resolution_timer_);
    have_next_resolution_timer_ = true;
    // TODO(roth): We currently deal with this ref manually.  Once the
    // new closure API is done, find a way to track this ref with the timer
    // callback as part of the type system.
    Ref(DEBUG_LOCATION, "retry-timer").release();
    if (timeout > 0) {
      GRPC_CARES_TRACE_LOG("resolver:%p retrying in %" PRId64 " milliseconds",
                           this, timeout);
    } else {
      GRPC_CARES_TRACE_LOG("resolver:%p retrying immediately", this);
    }
    grpc_timer_init(&next_resolution_timer_, next_try, &on_next_resolution_);
  }
  Unref(DEBUG_LOCATION, "dns-resolving");
  GRPC_ERROR_UNREF(error);
}

void AresClientChannelDNSResolver::MaybeStartResolvingLocked() {
  // If there is an existing timer, the time it fires is the earliest time we
  // can start the next resolution.
  if (have_next_resolution_timer_) return;
  if (last_resolution_timestamp_ >= 0) {
    // InvalidateNow to avoid getting stuck re-initializing this timer
    // in a loop while draining the currently-held WorkSerializer.
    // Also see https://github.com/grpc/grpc/issues/26079.
    ExecCtx::Get()->InvalidateNow();
    const grpc_millis earliest_next_resolution =
        last_resolution_timestamp_ + min_time_between_resolutions_;
    const grpc_millis ms_until_next_resolution =
        earliest_next_resolution - ExecCtx::Get()->Now();
    if (ms_until_next_resolution > 0) {
      const grpc_millis last_resolution_ago =
          ExecCtx::Get()->Now() - last_resolution_timestamp_;
      GRPC_CARES_TRACE_LOG(
          "resolver:%p In cooldown from last resolution (from %" PRId64
          " ms ago). Will resolve again in %" PRId64 " ms",
          this, last_resolution_ago, ms_until_next_resolution);
      have_next_resolution_timer_ = true;
      // TODO(roth): We currently deal with this ref manually.  Once the
      // new closure API is done, find a way to track this ref with the timer
      // callback as part of the type system.
      Ref(DEBUG_LOCATION, "next_resolution_timer_cooldown").release();
      grpc_timer_init(&next_resolution_timer_,
                      ExecCtx::Get()->Now() + ms_until_next_resolution,
                      &on_next_resolution_);
      return;
    }
  }
  StartResolvingLocked();
}

void AresClientChannelDNSResolver::StartResolvingLocked() {
  // TODO(roth): We currently deal with this ref manually.  Once the
  // new closure API is done, find a way to track this ref with the timer
  // callback as part of the type system.
  Ref(DEBUG_LOCATION, "dns-resolving").release();
  GPR_ASSERT(!resolving_);
  resolving_ = true;
  service_config_json_ = nullptr;
  pending_request_ = grpc_dns_lookup_ares(
      dns_server_.c_str(), name_to_resolve_.c_str(), kDefaultSecurePort,
      interested_parties_, &on_resolved_, &addresses_,
      enable_srv_queries_ ? &balancer_addresses_ : nullptr,
      request_service_config_ ? &service_config_json_ : nullptr,
      query_timeout_ms_);
  last_resolution_timestamp_ = ExecCtx::Get()->Now();
  GRPC_CARES_TRACE_LOG("resolver:%p Started resolving. pending_request_:%p",
                       this, pending_request_);
}

//
// Factory
//
class AresClientChannelDNSResolverFactory : public ResolverFactory {
 public:
  bool IsValidUri(const URI& uri) const override {
    if (absl::StripPrefix(uri.path(), "/").empty()) {
      gpr_log(GPR_ERROR, "no server name supplied in dns URI");
      return false;
    }
    return true;
  }

  OrphanablePtr<Resolver> CreateResolver(ResolverArgs args) const override {
    return MakeOrphanable<AresClientChannelDNSResolver>(std::move(args));
  }

  const char* scheme() const override { return "dns"; }
};

class AresDNSResolver;

AresDNSResolver* g_ares_dns_resolver;

class AresDNSResolver : public DNSResolver {
 public:
  class AresRequest : public DNSResolver::Request {
   public:
    AresRequest(absl::string_view name, absl::string_view default_port,
                grpc_pollset_set* interested_parties,
                std::function<void(absl::StatusOr<grpc_resolved_addresses*>)>
                    on_resolve_address_done)
        : name_(std::string(name)),
          default_port_(std::string(default_port)),
          interested_parties_(interested_parties),
          on_resolve_address_done_(std::move(on_resolve_address_done)) {
      GRPC_CLOSURE_INIT(&on_dns_lookup_done_, OnDnsLookupDone, this,
                        grpc_schedule_on_exec_ctx);
    }

    ~AresRequest() override {
      GRPC_CARES_TRACE_LOG("AresRequest:%p dtor ares_request_:%p", this,
                           ares_request_.get());
    }

    void Start() override {
      absl::MutexLock lock(&mu_);
      Ref().release();  // ref held by resolution
      ares_request_ = std::unique_ptr<grpc_ares_request>(grpc_dns_lookup_ares(
          "" /* dns_server */, name_.c_str(), default_port_.c_str(),
          interested_parties_, &on_dns_lookup_done_, &addresses_,
          nullptr /* balancer_addresses */, nullptr /* service_config_json */,
          GRPC_DNS_ARES_DEFAULT_QUERY_TIMEOUT_MS));
      GRPC_CARES_TRACE_LOG("AresRequest:%p ctor ares_request_:%p", this,
                           ares_request_.get());
    }

    void Orphan() override {
      {
        absl::MutexLock lock(&mu_);
        GRPC_CARES_TRACE_LOG("AresRequest:%p Orphan ares_request_:%p", this,
                             ares_request_.get());
        grpc_cancel_ares_request(ares_request_.get());
      }
      Unref();
    }

   private:
    static void OnDnsLookupDone(void* arg, grpc_error_handle error) {
      OrphanablePtr<AresRequest> r =
          OrphanablePtr<AresRequest>(static_cast<AresRequest*>(arg));
      grpc_resolved_addresses* resolved_addresses;
      {
        absl::MutexLock lock(&r->mu_);
        GRPC_CARES_TRACE_LOG("AresRequest:%p OnDnsLookupDone error:%s", r.get(),
                             grpc_error_std_string(error).c_str());
        if (r->addresses_ == nullptr || r->addresses_->empty()) {
          resolved_addresses = nullptr;
        } else {
          resolved_addresses = static_cast<grpc_resolved_addresses*>(
              gpr_zalloc(sizeof(grpc_resolved_addresses)));
          resolved_addresses->naddrs = r->addresses_->size();
          resolved_addresses->addrs =
              static_cast<grpc_resolved_address*>(gpr_zalloc(
                  sizeof(grpc_resolved_address) * resolved_addresses->naddrs));
          for (size_t i = 0; i < resolved_addresses->naddrs; ++i) {
            memcpy(&resolved_addresses->addrs[i],
                   &(*r->addresses_)[i].address(),
                   sizeof(grpc_resolved_address));
          }
        }
      }
      if (error == GRPC_ERROR_NONE) {
        // it's safe to run this inline since the current method was scheduled
        // on the ExecCtx
        r->on_resolve_address_done_(resolved_addresses);
      } else {
        r->on_resolve_address_done_(grpc_error_to_absl_status(error));
      }
    }

    // mutex to synchronize access to this object (but not to the ares_request
    // object itself). TODO(apolcyn): we can get rid of this after cleaning up
    // grpc_dns_lookup_ares to use two-phased initialization.
    absl::Mutex mu_;
    // the name to resolve
    const std::string name_;
    // the default port to use if name doesn't have one
    const std::string default_port_;
    // parties interested in our I/O
    grpc_pollset_set* const interested_parties_;
    // user-provided completion callback
    const std::function<void(absl::StatusOr<grpc_resolved_addresses*>)>
        on_resolve_address_done_;
    // currently resolving addresses
    std::unique_ptr<ServerAddressList> addresses_ ABSL_GUARDED_BY(mu_);
    // closure to call when the resolve_address_ares request completes
    // a closure wrapping on_resolve_address_done, which should be invoked
    // when the grpc_dns_lookup_ares operation is done.
    grpc_closure on_dns_lookup_done_ ABSL_GUARDED_BY(mu_);
    // underlying ares_request that the query is performed on
    std::unique_ptr<grpc_ares_request> ares_request_ ABSL_GUARDED_BY(mu_);
  };

  static AresDNSResolver* GetOrCreate() {
    if (g_ares_dns_resolver == nullptr) {
      g_ares_dns_resolver = new AresDNSResolver();
    }
    return g_ares_dns_resolver;
  }

  OrphanablePtr<DNSResolver::Request> ResolveName(
      absl::string_view name, absl::string_view default_port,
      grpc_pollset_set* interested_parties,
      std::function<void(absl::StatusOr<grpc_resolved_addresses*>)> on_done)
      override {
    return MakeOrphanable<AresRequest>(name, default_port, interested_parties,
                                       std::move(on_done));
  }

  // Resolve addr in a blocking fashion. On success,
  // result must be freed with grpc_resolved_addresses_destroy.
  absl::StatusOr<grpc_resolved_addresses*> BlockingResolveAddress(
      absl::string_view name, absl::string_view default_port) override {
    return default_resolver_->BlockingResolveAddress(name, default_port);
  }

 private:
  DNSResolver* default_resolver_ = GetDNSResolver();
};

bool ShouldUseAres(const char* resolver_env) {
  // TODO(lidiz): Remove the "g_custom_iomgr_enabled" flag once c-ares support
  // custom IO managers (e.g. gevent).
  return !g_custom_iomgr_enabled &&
         (resolver_env == nullptr || strlen(resolver_env) == 0 ||
          gpr_stricmp(resolver_env, "ares") == 0);
}

bool g_use_ares_dns_resolver;

}  // namespace

}  // namespace grpc_core

void grpc_resolver_dns_ares_init() {
  grpc_core::UniquePtr<char> resolver =
      GPR_GLOBAL_CONFIG_GET(grpc_dns_resolver);
  if (grpc_core::ShouldUseAres(resolver.get())) {
    grpc_core::g_use_ares_dns_resolver = true;
    gpr_log(GPR_DEBUG, "Using ares dns resolver");
    address_sorting_init();
    grpc_error_handle error = grpc_ares_init();
    if (error != GRPC_ERROR_NONE) {
      GRPC_LOG_IF_ERROR("grpc_ares_init() failed", error);
      return;
    }
    grpc_core::SetDNSResolver(grpc_core::AresDNSResolver::GetOrCreate());
    grpc_core::ResolverRegistry::Builder::RegisterResolverFactory(
        absl::make_unique<grpc_core::AresClientChannelDNSResolverFactory>());
  } else {
    grpc_core::g_use_ares_dns_resolver = false;
  }
}

void grpc_resolver_dns_ares_shutdown() {
  if (grpc_core::g_use_ares_dns_resolver) {
    address_sorting_shutdown();
    grpc_ares_cleanup();
  }
}

#else /* GRPC_ARES == 1 */

void grpc_resolver_dns_ares_init(void) {}

void grpc_resolver_dns_ares_shutdown(void) {}

#endif /* GRPC_ARES == 1 */

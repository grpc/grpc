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

#include <stdint.h>
#include <stdlib.h>

#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/impl/codegen/grpc_types.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/global_config_generic.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/iomgr/resolved_address.h"
#include "src/core/lib/resolver/resolver.h"
#include "src/core/lib/resolver/resolver_factory.h"
#include "src/core/lib/service_config/service_config.h"
#include "src/core/lib/uri/uri_parser.h"

#if GRPC_ARES == 1

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include <address_sorting/address_sorting.h>

#include "absl/container/flat_hash_set.h"
#include "absl/container/inlined_vector.h"
#include "absl/strings/str_cat.h"

#include "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_balancer_addresses.h"
#include "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper.h"
#include "src/core/ext/filters/client_channel/resolver/dns/dns_resolver_selection.h"
#include "src/core/ext/filters/client_channel/resolver/polling_resolver.h"
#include "src/core/lib/backoff/backoff.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/event_engine/handle_containers.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/iomgr/gethostname.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/resolver/resolver_registry.h"
#include "src/core/lib/resolver/server_address.h"
#include "src/core/lib/service_config/service_config_impl.h"
#include "src/core/lib/transport/error_utils.h"

#define GRPC_DNS_INITIAL_CONNECT_BACKOFF_SECONDS 1
#define GRPC_DNS_RECONNECT_BACKOFF_MULTIPLIER 1.6
#define GRPC_DNS_RECONNECT_MAX_BACKOFF_SECONDS 120
#define GRPC_DNS_RECONNECT_JITTER 0.2

namespace grpc_core {

namespace {

class AresClientChannelDNSResolver : public PollingResolver {
 public:
  AresClientChannelDNSResolver(ResolverArgs args,
                               const grpc_channel_args* channel_args);

  OrphanablePtr<Orphanable> StartRequest() override;

 private:
  class AresRequestWrapper : public InternallyRefCounted<AresRequestWrapper> {
   public:
    explicit AresRequestWrapper(
        RefCountedPtr<AresClientChannelDNSResolver> resolver)
        : resolver_(std::move(resolver)) {
      Ref(DEBUG_LOCATION, "OnResolved").release();
      GRPC_CLOSURE_INIT(&on_resolved_, OnResolved, this, nullptr);
      request_.reset(grpc_dns_lookup_ares(
          resolver_->authority().c_str(), resolver_->name_to_resolve().c_str(),
          kDefaultSecurePort, resolver_->interested_parties(), &on_resolved_,
          &addresses_,
          resolver_->enable_srv_queries_ ? &balancer_addresses_ : nullptr,
          resolver_->request_service_config_ ? &service_config_json_ : nullptr,
          resolver_->query_timeout_ms_));
      GRPC_CARES_TRACE_LOG("resolver:%p Started resolving. request_:%p",
                           resolver_.get(), request_.get());
    }

    ~AresRequestWrapper() override {
      gpr_free(service_config_json_);
      resolver_.reset(DEBUG_LOCATION, "dns-resolving");
    }

    void Orphan() override {
      grpc_cancel_ares_request(request_.get());
      Unref(DEBUG_LOCATION, "Orphan");
    }

   private:
    static void OnResolved(void* arg, grpc_error_handle error);
    void OnResolved(grpc_error_handle error);

    RefCountedPtr<AresClientChannelDNSResolver> resolver_;
    std::unique_ptr<grpc_ares_request> request_;
    grpc_closure on_resolved_;
    // Output fields from ares request.
    std::unique_ptr<ServerAddressList> addresses_;
    std::unique_ptr<ServerAddressList> balancer_addresses_;
    char* service_config_json_ = nullptr;
  };

  ~AresClientChannelDNSResolver() override;

  /// whether to request the service config
  const bool request_service_config_;
  // whether or not to enable SRV DNS queries
  const bool enable_srv_queries_;
  // timeout in milliseconds for active DNS queries
  const int query_timeout_ms_;
};

AresClientChannelDNSResolver::AresClientChannelDNSResolver(
    ResolverArgs args, const grpc_channel_args* channel_args)
    : PollingResolver(
          std::move(args), channel_args,
          Duration::Milliseconds(grpc_channel_args_find_integer(
              channel_args, GRPC_ARG_DNS_MIN_TIME_BETWEEN_RESOLUTIONS_MS,
              {1000 * 30, 0, INT_MAX})),
          BackOff::Options()
              .set_initial_backoff(Duration::Milliseconds(
                  GRPC_DNS_INITIAL_CONNECT_BACKOFF_SECONDS * 1000))
              .set_multiplier(GRPC_DNS_RECONNECT_BACKOFF_MULTIPLIER)
              .set_jitter(GRPC_DNS_RECONNECT_JITTER)
              .set_max_backoff(Duration::Milliseconds(
                  GRPC_DNS_RECONNECT_MAX_BACKOFF_SECONDS * 1000)),
          &grpc_trace_cares_resolver),
      request_service_config_(!grpc_channel_args_find_bool(
          channel_args, GRPC_ARG_SERVICE_CONFIG_DISABLE_RESOLUTION, true)),
      enable_srv_queries_(grpc_channel_args_find_bool(
          channel_args, GRPC_ARG_DNS_ENABLE_SRV_QUERIES, false)),
      query_timeout_ms_(grpc_channel_args_find_integer(
          channel_args, GRPC_ARG_DNS_ARES_QUERY_TIMEOUT_MS,
          {GRPC_DNS_ARES_DEFAULT_QUERY_TIMEOUT_MS, 0, INT_MAX})) {}

AresClientChannelDNSResolver::~AresClientChannelDNSResolver() {
  GRPC_CARES_TRACE_LOG("resolver:%p destroying AresClientChannelDNSResolver",
                       this);
}

OrphanablePtr<Orphanable> AresClientChannelDNSResolver::StartRequest() {
  return MakeOrphanable<AresRequestWrapper>(
      Ref(DEBUG_LOCATION, "dns-resolving"));
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
  if (!GRPC_ERROR_IS_NONE(*error)) return "";
  if (json.type() != Json::Type::ARRAY) {
    *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Service Config Choices, error: should be of type array");
    return "";
  }
  const Json* service_config = nullptr;
  std::vector<grpc_error_handle> error_list;
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

void AresClientChannelDNSResolver::AresRequestWrapper::OnResolved(
    void* arg, grpc_error_handle error) {
  auto* self = static_cast<AresRequestWrapper*>(arg);
  self->OnResolved(error);
}

void AresClientChannelDNSResolver::AresRequestWrapper::OnResolved(
    grpc_error_handle error) {
  GRPC_CARES_TRACE_LOG("resolver:%p OnResolved()", this);
  Result result;
  absl::InlinedVector<grpc_arg, 1> new_args;
  // TODO(roth): Change logic to be able to report failures for addresses
  // and service config independently of each other.
  if (addresses_ != nullptr || balancer_addresses_ != nullptr) {
    if (addresses_ != nullptr) {
      result.addresses = std::move(*addresses_);
    } else {
      result.addresses = ServerAddressList();
    }
    if (service_config_json_ != nullptr) {
      grpc_error_handle service_config_error = GRPC_ERROR_NONE;
      std::string service_config_string =
          ChooseServiceConfig(service_config_json_, &service_config_error);
      RefCountedPtr<ServiceConfig> service_config;
      if (GRPC_ERROR_IS_NONE(service_config_error) &&
          !service_config_string.empty()) {
        GRPC_CARES_TRACE_LOG("resolver:%p selected service config choice: %s",
                             this, service_config_string.c_str());
        service_config = ServiceConfigImpl::Create(resolver_->channel_args(),
                                                   service_config_string,
                                                   &service_config_error);
      }
      if (!GRPC_ERROR_IS_NONE(service_config_error)) {
        result.service_config = absl::UnavailableError(
            absl::StrCat("failed to parse service config: ",
                         grpc_error_std_string(service_config_error)));
        GRPC_ERROR_UNREF(service_config_error);
      } else {
        result.service_config = std::move(service_config);
      }
    }
    if (balancer_addresses_ != nullptr) {
      new_args.push_back(
          CreateGrpclbBalancerAddressesArg(balancer_addresses_.get()));
    }
  } else {
    GRPC_CARES_TRACE_LOG("resolver:%p dns resolution failed: %s", this,
                         grpc_error_std_string(error).c_str());
    std::string error_message;
    grpc_error_get_str(error, GRPC_ERROR_STR_DESCRIPTION, &error_message);
    absl::Status status = absl::UnavailableError(
        absl::StrCat("DNS resolution failed for ", resolver_->name_to_resolve(),
                     ": ", error_message));
    result.addresses = status;
    result.service_config = status;
  }
  result.args = grpc_channel_args_copy_and_add(
      resolver_->channel_args(), new_args.data(), new_args.size());
  resolver_->OnRequestComplete(std::move(result));
  Unref(DEBUG_LOCATION, "OnResolved");
}

//
// Factory
//

class AresClientChannelDNSResolverFactory : public ResolverFactory {
 public:
  absl::string_view scheme() const override { return "dns"; }

  bool IsValidUri(const URI& uri) const override {
    if (absl::StripPrefix(uri.path(), "/").empty()) {
      gpr_log(GPR_ERROR, "no server name supplied in dns URI");
      return false;
    }
    return true;
  }

  OrphanablePtr<Resolver> CreateResolver(ResolverArgs args) const override {
    const grpc_channel_args* channel_args = args.args;
    return MakeOrphanable<AresClientChannelDNSResolver>(std::move(args),
                                                        channel_args);
  }
};

class AresDNSResolver : public DNSResolver {
 public:
  class AresRequest {
   public:
    AresRequest(
        absl::string_view name, absl::string_view default_port,
        grpc_pollset_set* interested_parties,
        std::function<void(absl::StatusOr<std::vector<grpc_resolved_address>>)>
            on_resolve_address_done,
        AresDNSResolver* resolver, intptr_t aba_token)
        : name_(std::string(name)),
          default_port_(std::string(default_port)),
          interested_parties_(interested_parties),
          pollset_set_(grpc_pollset_set_create()),
          on_resolve_address_done_(std::move(on_resolve_address_done)),
          completed_(false),
          resolver_(resolver),
          aba_token_(aba_token) {
      GRPC_CARES_TRACE_LOG("AresRequest:%p ctor", this);
      GRPC_CLOSURE_INIT(&on_dns_lookup_done_, OnDnsLookupDone, this,
                        grpc_schedule_on_exec_ctx);
      MutexLock lock(&mu_);
      grpc_pollset_set_add_pollset_set(pollset_set_, interested_parties);
      ares_request_ = std::unique_ptr<grpc_ares_request>(grpc_dns_lookup_ares(
          /*dns_server=*/"", name_.c_str(), default_port_.c_str(), pollset_set_,
          &on_dns_lookup_done_, &addresses_,
          /*balancer_addresses=*/nullptr, /*service_config_json=*/nullptr,
          GRPC_DNS_ARES_DEFAULT_QUERY_TIMEOUT_MS));
      GRPC_CARES_TRACE_LOG("AresRequest:%p Start ares_request_:%p", this,
                           ares_request_.get());
    }

    ~AresRequest() {
      GRPC_CARES_TRACE_LOG("AresRequest:%p dtor ares_request_:%p", this,
                           ares_request_.get());
      resolver_->UnregisterRequest(task_handle());
      grpc_pollset_set_destroy(pollset_set_);
    }

    bool Cancel() {
      MutexLock lock(&mu_);
      GRPC_CARES_TRACE_LOG("AresRequest:%p Cancel ares_request_:%p", this,
                           ares_request_.get());
      if (completed_) return false;
      // OnDnsLookupDone will still be run
      grpc_cancel_ares_request(ares_request_.get());
      completed_ = true;
      grpc_pollset_set_del_pollset_set(pollset_set_, interested_parties_);
      return true;
    }

    TaskHandle task_handle() {
      return {reinterpret_cast<intptr_t>(this), aba_token_};
    }

   private:
    // Called by ares when lookup has completed or when cancelled. It is always
    // called exactly once.
    static void OnDnsLookupDone(void* arg, grpc_error_handle error) {
      AresRequest* request = static_cast<AresRequest*>(arg);
      GRPC_CARES_TRACE_LOG("AresRequest:%p OnDnsLookupDone", request);
      // This request is deleted and unregistered upon any exit.
      std::unique_ptr<AresRequest> deleter(request);
      std::vector<grpc_resolved_address> resolved_addresses;
      {
        MutexLock lock(&request->mu_);
        if (request->completed_) return;
        request->completed_ = true;
        if (request->addresses_ != nullptr) {
          resolved_addresses.reserve(request->addresses_->size());
          for (const auto& server_address : *request->addresses_) {
            resolved_addresses.push_back(server_address.address());
          }
        }
      }
      grpc_pollset_set_del_pollset_set(request->pollset_set_,
                                       request->interested_parties_);
      if (!GRPC_ERROR_IS_NONE(error)) {
        request->on_resolve_address_done_(grpc_error_to_absl_status(error));
        return;
      }
      request->on_resolve_address_done_(std::move(resolved_addresses));
    }

    // mutex to synchronize access to this object (but not to the ares_request
    // object itself).
    Mutex mu_;
    // the name to resolve
    const std::string name_;
    // the default port to use if name doesn't have one
    const std::string default_port_;
    // parties interested in our I/O
    grpc_pollset_set* const interested_parties_;
    // locally owned pollset_set, required to support cancellation of requests
    // while ares still needs a valid pollset_set.
    grpc_pollset_set* pollset_set_;
    // user-provided completion callback
    const std::function<void(
        absl::StatusOr<std::vector<grpc_resolved_address>>)>
        on_resolve_address_done_;
    // currently resolving addresses
    std::unique_ptr<ServerAddressList> addresses_ ABSL_GUARDED_BY(mu_);
    // closure to call when the resolve_address_ares request completes
    // a closure wrapping on_resolve_address_done, which should be invoked
    // when the grpc_dns_lookup_ares operation is done.
    grpc_closure on_dns_lookup_done_ ABSL_GUARDED_BY(mu_);
    // underlying ares_request that the query is performed on
    std::unique_ptr<grpc_ares_request> ares_request_ ABSL_GUARDED_BY(mu_);
    bool completed_ ABSL_GUARDED_BY(mu_);
    // Parent resolver that created this request
    AresDNSResolver* resolver_;
    // Unique token to help distinguish this request from others that may later
    // be created in the same memory location.
    intptr_t aba_token_;
  };

  // gets the singleton instance, possibly creating it first
  static AresDNSResolver* GetOrCreate() {
    static AresDNSResolver* instance = new AresDNSResolver();
    return instance;
  }

  TaskHandle ResolveName(
      absl::string_view name, absl::string_view default_port,
      grpc_pollset_set* interested_parties,
      std::function<void(absl::StatusOr<std::vector<grpc_resolved_address>>)>
          on_done) override {
    MutexLock lock(&mu_);
    auto* request = new AresRequest(name, default_port, interested_parties,
                                    std::move(on_done), this, aba_token_++);
    auto handle = request->task_handle();
    open_requests_.insert(handle);
    return handle;
  }

  absl::StatusOr<std::vector<grpc_resolved_address>> ResolveNameBlocking(
      absl::string_view name, absl::string_view default_port) override {
    // TODO(apolcyn): change this to wrap the async version of the c-ares
    // API with a promise, and remove the reference to the previous resolver.
    return default_resolver_->ResolveNameBlocking(name, default_port);
  }

  bool Cancel(TaskHandle handle) override {
    MutexLock lock(&mu_);
    if (!open_requests_.contains(handle)) {
      // Unknown request, possibly completed already, or an invalid handle.
      GRPC_CARES_TRACE_LOG(
          "AresDNSResolver:%p attempt to cancel unknown TaskHandle:%s", this,
          HandleToString(handle).c_str());
      return false;
    }
    auto* request = reinterpret_cast<AresRequest*>(handle.keys[0]);
    GRPC_CARES_TRACE_LOG("AresDNSResolver:%p cancel ares_request:%p", this,
                         request);
    return request->Cancel();
  }

 private:
  // Called exclusively from the AresRequest destructor.
  void UnregisterRequest(TaskHandle handle) {
    MutexLock lock(&mu_);
    open_requests_.erase(handle);
  }

  // the previous default DNS resolver, used to delegate blocking DNS calls to
  DNSResolver* default_resolver_ = GetDNSResolver();
  Mutex mu_;
  grpc_event_engine::experimental::LookupTaskHandleSet open_requests_
      ABSL_GUARDED_BY(mu_);
  intptr_t aba_token_ ABSL_GUARDED_BY(mu_) = 0;
};

bool ShouldUseAres(const char* resolver_env) {
  return resolver_env == nullptr || strlen(resolver_env) == 0 ||
         gpr_stricmp(resolver_env, "ares") == 0;
}

bool UseAresDnsResolver() {
  static const bool result = []() {
    UniquePtr<char> resolver = GPR_GLOBAL_CONFIG_GET(grpc_dns_resolver);
    bool result = ShouldUseAres(resolver.get());
    if (result) gpr_log(GPR_DEBUG, "Using ares dns resolver");
    return result;
  }();
  return result;
}

}  // namespace

void RegisterAresDnsResolver(CoreConfiguration::Builder* builder) {
  if (UseAresDnsResolver()) {
    builder->resolver_registry()->RegisterResolverFactory(
        absl::make_unique<AresClientChannelDNSResolverFactory>());
  }
}

}  // namespace grpc_core

void grpc_resolver_dns_ares_init() {
  if (grpc_core::UseAresDnsResolver()) {
    address_sorting_init();
    grpc_error_handle error = grpc_ares_init();
    if (!GRPC_ERROR_IS_NONE(error)) {
      GRPC_LOG_IF_ERROR("grpc_ares_init() failed", error);
      return;
    }
    grpc_core::SetDNSResolver(grpc_core::AresDNSResolver::GetOrCreate());
  }
}

void grpc_resolver_dns_ares_shutdown() {
  if (grpc_core::UseAresDnsResolver()) {
    address_sorting_shutdown();
    grpc_ares_cleanup();
  }
}

#else /* GRPC_ARES == 1 */

namespace grpc_core {
void RegisterAresDnsResolver(CoreConfiguration::Builder*) {}
}  // namespace grpc_core

void grpc_resolver_dns_ares_init() {}

void grpc_resolver_dns_ares_shutdown() {}

#endif /* GRPC_ARES == 1 */

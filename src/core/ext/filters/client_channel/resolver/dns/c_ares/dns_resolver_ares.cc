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
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/types/optional.h"

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

#include <stdio.h>
#include <string.h>

#include <address_sorting/address_sorting.h>

#include "absl/container/flat_hash_set.h"
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
                               const ChannelArgs& channel_args);

  OrphanablePtr<Orphanable> StartRequest() override;

 private:
  class AresRequestWrapper : public InternallyRefCounted<AresRequestWrapper> {
   public:
    explicit AresRequestWrapper(
        RefCountedPtr<AresClientChannelDNSResolver> resolver)
        : resolver_(std::move(resolver)) {
      // TODO(hork): replace this callback bookkeeping with promises.
      // Locking to prevent completion before all records are queried
      MutexLock lock(&on_resolved_mu_);
      Ref(DEBUG_LOCATION, "OnHostnameResolved").release();
      GRPC_CLOSURE_INIT(&on_hostname_resolved_, OnHostnameResolved, this,
                        nullptr);
      hostname_request_.reset(grpc_dns_lookup_hostname_ares(
          resolver_->authority().c_str(), resolver_->name_to_resolve().c_str(),
          kDefaultSecurePort, resolver_->interested_parties(),
          &on_hostname_resolved_, &addresses_, resolver_->query_timeout_ms_));
      GRPC_CARES_TRACE_LOG(
          "resolver:%p Started resolving hostnames. hostname_request_:%p",
          resolver_.get(), hostname_request_.get());
      if (resolver_->enable_srv_queries_) {
        Ref(DEBUG_LOCATION, "OnSRVResolved").release();
        GRPC_CLOSURE_INIT(&on_srv_resolved_, OnSRVResolved, this, nullptr);
        srv_request_.reset(grpc_dns_lookup_srv_ares(
            resolver_->authority().c_str(),
            resolver_->name_to_resolve().c_str(),
            resolver_->interested_parties(), &on_srv_resolved_,
            &balancer_addresses_, resolver_->query_timeout_ms_));
        GRPC_CARES_TRACE_LOG(
            "resolver:%p Started resolving SRV records. srv_request_:%p",
            resolver_.get(), srv_request_.get());
      }
      if (resolver_->request_service_config_) {
        Ref(DEBUG_LOCATION, "OnTXTResolved").release();
        GRPC_CLOSURE_INIT(&on_txt_resolved_, OnTXTResolved, this, nullptr);
        txt_request_.reset(grpc_dns_lookup_txt_ares(
            resolver_->authority().c_str(),
            resolver_->name_to_resolve().c_str(),
            resolver_->interested_parties(), &on_txt_resolved_,
            &service_config_json_, resolver_->query_timeout_ms_));
        GRPC_CARES_TRACE_LOG(
            "resolver:%p Started resolving TXT records. txt_request_:%p",
            resolver_.get(), srv_request_.get());
      }
    }

    ~AresRequestWrapper() override {
      gpr_free(service_config_json_);
      resolver_.reset(DEBUG_LOCATION, "dns-resolving");
    }

    // Note that thread safety cannot be analyzed due to this being invoked from
    // OrphanablePtr<>, and there's no way to pass the lock annotation through
    // there.
    void Orphan() override ABSL_NO_THREAD_SAFETY_ANALYSIS {
      {
        MutexLock lock(&on_resolved_mu_);
        if (hostname_request_ != nullptr) {
          grpc_cancel_ares_request(hostname_request_.get());
        }
        if (srv_request_ != nullptr) {
          grpc_cancel_ares_request(srv_request_.get());
        }
        if (txt_request_ != nullptr) {
          grpc_cancel_ares_request(txt_request_.get());
        }
      }
      Unref(DEBUG_LOCATION, "Orphan");
    }

   private:
    static void OnHostnameResolved(void* arg, grpc_error_handle error);
    static void OnSRVResolved(void* arg, grpc_error_handle error);
    static void OnTXTResolved(void* arg, grpc_error_handle error);
    absl::optional<Result> OnResolvedLocked(grpc_error_handle error)
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(on_resolved_mu_);

    Mutex on_resolved_mu_;
    RefCountedPtr<AresClientChannelDNSResolver> resolver_;
    grpc_closure on_hostname_resolved_;
    std::unique_ptr<grpc_ares_request> hostname_request_
        ABSL_GUARDED_BY(on_resolved_mu_);
    grpc_closure on_srv_resolved_;
    std::unique_ptr<grpc_ares_request> srv_request_
        ABSL_GUARDED_BY(on_resolved_mu_);
    grpc_closure on_txt_resolved_;
    std::unique_ptr<grpc_ares_request> txt_request_
        ABSL_GUARDED_BY(on_resolved_mu_);
    // Output fields from ares request.
    std::unique_ptr<ServerAddressList> addresses_
        ABSL_GUARDED_BY(on_resolved_mu_);
    std::unique_ptr<ServerAddressList> balancer_addresses_
        ABSL_GUARDED_BY(on_resolved_mu_);
    char* service_config_json_ ABSL_GUARDED_BY(on_resolved_mu_) = nullptr;
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
    ResolverArgs args, const ChannelArgs& channel_args)
    : PollingResolver(
          std::move(args), channel_args,
          std::max(Duration::Zero(),
                   channel_args
                       .GetDurationFromIntMillis(
                           GRPC_ARG_DNS_MIN_TIME_BETWEEN_RESOLUTIONS_MS)
                       .value_or(Duration::Seconds(30))),
          BackOff::Options()
              .set_initial_backoff(Duration::Milliseconds(
                  GRPC_DNS_INITIAL_CONNECT_BACKOFF_SECONDS * 1000))
              .set_multiplier(GRPC_DNS_RECONNECT_BACKOFF_MULTIPLIER)
              .set_jitter(GRPC_DNS_RECONNECT_JITTER)
              .set_max_backoff(Duration::Milliseconds(
                  GRPC_DNS_RECONNECT_MAX_BACKOFF_SECONDS * 1000)),
          &grpc_trace_cares_resolver),
      request_service_config_(
          !channel_args.GetBool(GRPC_ARG_SERVICE_CONFIG_DISABLE_RESOLUTION)
               .value_or(true)),
      enable_srv_queries_(channel_args.GetBool(GRPC_ARG_DNS_ENABLE_SRV_QUERIES)
                              .value_or(false)),
      query_timeout_ms_(
          std::max(0, channel_args.GetInt(GRPC_ARG_DNS_ARES_QUERY_TIMEOUT_MS)
                          .value_or(GRPC_DNS_ARES_DEFAULT_QUERY_TIMEOUT_MS))) {}

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
  auto json = Json::Parse(service_config_choice_json);
  if (!json.ok()) {
    *error = absl_status_to_grpc_error(json.status());
    return "";
  }
  if (json->type() != Json::Type::ARRAY) {
    *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Service Config Choices, error: should be of type array");
    return "";
  }
  const Json* service_config = nullptr;
  std::vector<grpc_error_handle> error_list;
  for (const Json& choice : json->array_value()) {
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

void AresClientChannelDNSResolver::AresRequestWrapper::OnHostnameResolved(
    void* arg, grpc_error_handle error) {
  auto* self = static_cast<AresRequestWrapper*>(arg);
  absl::optional<Result> result;
  {
    MutexLock lock(&self->on_resolved_mu_);
    self->hostname_request_.reset();
    result = self->OnResolvedLocked(error);
  }
  if (result.has_value()) {
    self->resolver_->OnRequestComplete(std::move(*result));
  }
  self->Unref(DEBUG_LOCATION, "OnHostnameResolved");
}

void AresClientChannelDNSResolver::AresRequestWrapper::OnSRVResolved(
    void* arg, grpc_error_handle error) {
  auto* self = static_cast<AresRequestWrapper*>(arg);
  absl::optional<Result> result;
  {
    MutexLock lock(&self->on_resolved_mu_);
    self->srv_request_.reset();
    result = self->OnResolvedLocked(error);
  }
  if (result.has_value()) {
    self->resolver_->OnRequestComplete(std::move(*result));
  }
  self->Unref(DEBUG_LOCATION, "OnSRVResolved");
}

void AresClientChannelDNSResolver::AresRequestWrapper::OnTXTResolved(
    void* arg, grpc_error_handle error) {
  auto* self = static_cast<AresRequestWrapper*>(arg);
  absl::optional<Result> result;
  {
    MutexLock lock(&self->on_resolved_mu_);
    self->txt_request_.reset();
    result = self->OnResolvedLocked(error);
  }
  if (result.has_value()) {
    self->resolver_->OnRequestComplete(std::move(*result));
  }
  self->Unref(DEBUG_LOCATION, "OnTXTResolved");
}

// Returns a Result if resolution is complete.
// callers must release the lock and call OnRequestComplete if a Result is
// returned. This is because OnRequestComplete may Orphan the resolver, which
// requires taking the lock.
absl::optional<AresClientChannelDNSResolver::Result>
AresClientChannelDNSResolver::AresRequestWrapper::OnResolvedLocked(
    grpc_error_handle error) ABSL_EXCLUSIVE_LOCKS_REQUIRED(on_resolved_mu_) {
  if (hostname_request_ != nullptr || srv_request_ != nullptr ||
      txt_request_ != nullptr) {
    GRPC_CARES_TRACE_LOG(
        "resolver:%p OnResolved() waiting for results (hostname: %s, srv: %s, "
        "txt: %s)",
        this, hostname_request_ != nullptr ? "waiting" : "done",
        srv_request_ != nullptr ? "waiting" : "done",
        txt_request_ != nullptr ? "waiting" : "done");
    return absl::nullopt;
  }
  GRPC_CARES_TRACE_LOG("resolver:%p OnResolved() proceeding", this);
  Result result;
  result.args = resolver_->channel_args();
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
      if (!GRPC_ERROR_IS_NONE(service_config_error)) {
        result.service_config = absl::UnavailableError(
            absl::StrCat("failed to parse service config: ",
                         grpc_error_std_string(service_config_error)));
        GRPC_ERROR_UNREF(service_config_error);
      } else if (!service_config_string.empty()) {
        GRPC_CARES_TRACE_LOG("resolver:%p selected service config choice: %s",
                             this, service_config_string.c_str());
        result.service_config = ServiceConfigImpl::Create(
            resolver_->channel_args(), service_config_string);
        if (!result.service_config.ok()) {
          result.service_config = absl::UnavailableError(
              absl::StrCat("failed to parse service config: ",
                           result.service_config.status().message()));
        }
      }
    }
    if (balancer_addresses_ != nullptr) {
      result.args = SetGrpcLbBalancerAddresses(
          result.args, ServerAddressList(*balancer_addresses_));
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

  return std::move(result);
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
    ChannelArgs channel_args = args.args;
    return MakeOrphanable<AresClientChannelDNSResolver>(std::move(args),
                                                        channel_args);
  }
};

class AresDNSResolver : public DNSResolver {
 public:
  // Abstract class that centralizes common request handling logic via the
  // template method pattern.
  // This requires a two-phase initialization, where 1) a request is created via
  // a subclass constructor, and 2) the request is initiated via Run()
  class AresRequest {
   public:
    virtual ~AresRequest() {
      GRPC_CARES_TRACE_LOG("AresRequest:%p dtor ares_request_:%p", this,
                           grpc_ares_request_.get());
      resolver_->UnregisterRequest(task_handle());
      grpc_pollset_set_destroy(pollset_set_);
    }

    // Initiates the low-level c-ares request and returns its handle.
    virtual std::unique_ptr<grpc_ares_request> MakeRequestLocked() = 0;
    // Called on ares resolution, but not upon cancellation.
    // After execution, the AresRequest will perform any final cleanup and
    // delete itself.
    virtual void OnComplete(grpc_error_handle error) = 0;

    // Called to initiate the request.
    void Run() {
      MutexLock lock(&mu_);
      grpc_ares_request_ = MakeRequestLocked();
    }

    bool Cancel() {
      MutexLock lock(&mu_);
      if (grpc_ares_request_ != nullptr) {
        GRPC_CARES_TRACE_LOG("AresRequest:%p Cancel ares_request_:%p", this,
                             grpc_ares_request_.get());
        if (completed_) return false;
        // OnDnsLookupDone will still be run
        completed_ = true;
        grpc_cancel_ares_request(grpc_ares_request_.get());
      } else {
        completed_ = true;
        OnDnsLookupDone(this, GRPC_ERROR_CANCELLED);
      }
      grpc_pollset_set_del_pollset_set(pollset_set_, interested_parties_);
      return true;
    }

    TaskHandle task_handle() {
      return {reinterpret_cast<intptr_t>(this), aba_token_};
    }

   protected:
    AresRequest(absl::string_view name, absl::string_view name_server,
                Duration timeout, grpc_pollset_set* interested_parties,
                AresDNSResolver* resolver, intptr_t aba_token)
        : name_(name),
          name_server_(name_server),
          timeout_(timeout),
          interested_parties_(interested_parties),
          completed_(false),
          resolver_(resolver),
          aba_token_(aba_token),
          pollset_set_(grpc_pollset_set_create()) {
      GRPC_CLOSURE_INIT(&on_dns_lookup_done_, OnDnsLookupDone, this,
                        grpc_schedule_on_exec_ctx);
      grpc_pollset_set_add_pollset_set(pollset_set_, interested_parties_);
    }

    grpc_pollset_set* pollset_set() { return pollset_set_; };
    grpc_closure* on_dns_lookup_done() { return &on_dns_lookup_done_; };
    const std::string& name() { return name_; }
    const std::string& name_server() { return name_server_; }
    const Duration& timeout() { return timeout_; }

   private:
    // Called by ares when lookup has completed or when cancelled. It is always
    // called exactly once, and it triggers self-deletion.
    static void OnDnsLookupDone(void* arg, grpc_error_handle error) {
      AresRequest* r = static_cast<AresRequest*>(arg);
      auto deleter = std::unique_ptr<AresRequest>(r);
      {
        MutexLock lock(&r->mu_);
        grpc_pollset_set_del_pollset_set(r->pollset_set_,
                                         r->interested_parties_);
        if (r->completed_) {
          return;
        }
        r->completed_ = true;
      }
      r->OnComplete(error);
    }

    // the name to resolve
    const std::string name_;
    // the name server to query
    const std::string name_server_;
    // request-specific timeout
    Duration timeout_;
    // mutex to synchronize access to this object (but not to the ares_request
    // object itself).
    Mutex mu_;
    // parties interested in our I/O
    grpc_pollset_set* const interested_parties_;
    // underlying cares_request that the query is performed on
    std::unique_ptr<grpc_ares_request> grpc_ares_request_ ABSL_GUARDED_BY(mu_);
    // Set when the callback is either cancelled or executed.
    // It is not the subclasses' responsibility to set this flag.
    bool completed_ ABSL_GUARDED_BY(mu_);
    // Parent resolver that created this request
    AresDNSResolver* resolver_;
    // Unique token to help distinguish this request from others that may later
    // be created in the same memory location.
    intptr_t aba_token_;
    // closure to call when the ares resolution request completes. Subclasses
    // should use this as the ares callback in MakeRequestLocked()
    grpc_closure on_dns_lookup_done_ ABSL_GUARDED_BY(mu_);
    // locally owned pollset_set, required to support cancellation of requests
    // while ares still needs a valid pollset_set. Subclasses should give this
    // pollset to ares in MakeRequestLocked();
    grpc_pollset_set* pollset_set_;
  };

  class AresHostnameRequest : public AresRequest {
   public:
    AresHostnameRequest(
        absl::string_view name, absl::string_view default_port,
        absl::string_view name_server, Duration timeout,
        grpc_pollset_set* interested_parties,
        std::function<void(absl::StatusOr<std::vector<grpc_resolved_address>>)>
            on_resolve_address_done,
        AresDNSResolver* resolver, intptr_t aba_token)
        : AresRequest(name, name_server, timeout, interested_parties, resolver,
                      aba_token),
          default_port_(default_port),
          on_resolve_address_done_(std::move(on_resolve_address_done)) {
      GRPC_CARES_TRACE_LOG("AresHostnameRequest:%p ctor", this);
    }

    std::unique_ptr<grpc_ares_request> MakeRequestLocked() override {
      auto ares_request =
          std::unique_ptr<grpc_ares_request>(grpc_dns_lookup_hostname_ares(
              name_server().c_str(), name().c_str(), default_port_.c_str(),
              pollset_set(), on_dns_lookup_done(), &addresses_,
              timeout().millis()));
      GRPC_CARES_TRACE_LOG("AresHostnameRequest:%p Start ares_request_:%p",
                           this, ares_request.get());
      return ares_request;
    }

    void OnComplete(grpc_error_handle error) override {
      GRPC_CARES_TRACE_LOG("AresHostnameRequest:%p OnComplete", this);
      if (!GRPC_ERROR_IS_NONE(error)) {
        on_resolve_address_done_(grpc_error_to_absl_status(error));
        return;
      }
      std::vector<grpc_resolved_address> resolved_addresses;
      if (addresses_ != nullptr) {
        resolved_addresses.reserve(addresses_->size());
        for (const auto& server_address : *addresses_) {
          resolved_addresses.push_back(server_address.address());
        }
      }
      on_resolve_address_done_(std::move(resolved_addresses));
    }

    // the default port to use if name doesn't have one
    const std::string default_port_;
    // user-provided completion callback
    const std::function<void(
        absl::StatusOr<std::vector<grpc_resolved_address>>)>
        on_resolve_address_done_;
    // currently resolving addresses
    std::unique_ptr<ServerAddressList> addresses_;
  };

  class AresSRVRequest : public AresRequest {
   public:
    AresSRVRequest(
        absl::string_view name, absl::string_view name_server, Duration timeout,
        grpc_pollset_set* interested_parties,
        std::function<void(absl::StatusOr<std::vector<grpc_resolved_address>>)>
            on_resolve_address_done,
        AresDNSResolver* resolver, intptr_t aba_token)
        : AresRequest(name, name_server, timeout, interested_parties, resolver,
                      aba_token),
          on_resolve_address_done_(std::move(on_resolve_address_done)) {
      GRPC_CARES_TRACE_LOG("AresSRVRequest:%p ctor", this);
    }

    std::unique_ptr<grpc_ares_request> MakeRequestLocked() override {
      auto ares_request =
          std::unique_ptr<grpc_ares_request>(grpc_dns_lookup_srv_ares(
              name_server().c_str(), name().c_str(), pollset_set(),
              on_dns_lookup_done(), &balancer_addresses_, timeout().millis()));
      GRPC_CARES_TRACE_LOG("AresSRVRequest:%p Start ares_request_:%p", this,
                           ares_request.get());
      return ares_request;
    }

    void OnComplete(grpc_error_handle error) override {
      GRPC_CARES_TRACE_LOG("AresSRVRequest:%p OnComplete", this);
      if (!GRPC_ERROR_IS_NONE(error)) {
        on_resolve_address_done_(grpc_error_to_absl_status(error));
        return;
      }
      std::vector<grpc_resolved_address> resolved_addresses;
      if (balancer_addresses_ != nullptr) {
        resolved_addresses.reserve(balancer_addresses_->size());
        for (const auto& server_address : *balancer_addresses_) {
          resolved_addresses.push_back(server_address.address());
        }
      }
      on_resolve_address_done_(std::move(resolved_addresses));
    }

    // user-provided completion callback
    const std::function<void(
        absl::StatusOr<std::vector<grpc_resolved_address>>)>
        on_resolve_address_done_;
    // currently resolving addresses
    std::unique_ptr<ServerAddressList> balancer_addresses_;
  };

  class AresTXTRequest : public AresRequest {
   public:
    AresTXTRequest(absl::string_view name, absl::string_view name_server,
                   Duration timeout, grpc_pollset_set* interested_parties,
                   std::function<void(absl::StatusOr<std::string>)> on_resolved,
                   AresDNSResolver* resolver, intptr_t aba_token)
        : AresRequest(name, name_server, timeout, interested_parties, resolver,
                      aba_token),
          on_resolved_(std::move(on_resolved)) {
      GRPC_CARES_TRACE_LOG("AresTXTRequest:%p ctor", this);
    }

    ~AresTXTRequest() override { gpr_free(service_config_json_); }

    std::unique_ptr<grpc_ares_request> MakeRequestLocked() override {
      auto ares_request =
          std::unique_ptr<grpc_ares_request>(grpc_dns_lookup_txt_ares(
              name_server().c_str(), name().c_str(), pollset_set(),
              on_dns_lookup_done(), &service_config_json_, timeout().millis()));
      GRPC_CARES_TRACE_LOG("AresSRVRequest:%p Start ares_request_:%p", this,
                           ares_request.get());
      return ares_request;
    }

    void OnComplete(grpc_error_handle error) override {
      GRPC_CARES_TRACE_LOG("AresSRVRequest:%p OnComplete", this);
      if (!GRPC_ERROR_IS_NONE(error)) {
        on_resolved_(grpc_error_to_absl_status(error));
        return;
      }
      on_resolved_(service_config_json_);
    }

    // service config from the TXT record
    char* service_config_json_ = nullptr;
    // user-provided completion callback
    const std::function<void(absl::StatusOr<std::string>)> on_resolved_;
  };

  // gets the singleton instance, possibly creating it first
  static AresDNSResolver* GetOrCreate() {
    static AresDNSResolver* instance = new AresDNSResolver();
    return instance;
  }

  TaskHandle LookupHostname(
      std::function<void(absl::StatusOr<std::vector<grpc_resolved_address>>)>
          on_resolved,
      absl::string_view name, absl::string_view default_port, Duration timeout,
      grpc_pollset_set* interested_parties,
      absl::string_view name_server) override {
    MutexLock lock(&mu_);
    auto* request = new AresHostnameRequest(
        name, default_port, name_server, timeout, interested_parties,
        std::move(on_resolved), this, aba_token_++);
    request->Run();
    auto handle = request->task_handle();
    open_requests_.insert(handle);
    return handle;
  }

  absl::StatusOr<std::vector<grpc_resolved_address>> LookupHostnameBlocking(
      absl::string_view name, absl::string_view default_port) override {
    // TODO(apolcyn): change this to wrap the async version of the c-ares
    // API with a promise, and remove the reference to the previous resolver.
    return default_resolver_->LookupHostnameBlocking(name, default_port);
  }

  TaskHandle LookupSRV(
      std::function<void(absl::StatusOr<std::vector<grpc_resolved_address>>)>
          on_resolved,
      absl::string_view name, Duration timeout,
      grpc_pollset_set* interested_parties,
      absl::string_view name_server) override {
    MutexLock lock(&mu_);
    auto* request =
        new AresSRVRequest(name, name_server, timeout, interested_parties,
                           std::move(on_resolved), this, aba_token_++);
    request->Run();
    auto handle = request->task_handle();
    open_requests_.insert(handle);
    return handle;
  };

  TaskHandle LookupTXT(
      std::function<void(absl::StatusOr<std::string>)> on_resolved,
      absl::string_view name, Duration timeout,
      grpc_pollset_set* interested_parties,
      absl::string_view name_server) override {
    MutexLock lock(&mu_);
    auto* request =
        new AresTXTRequest(name, name_server, timeout, interested_parties,
                           std::move(on_resolved), this, aba_token_++);
    request->Run();
    auto handle = request->task_handle();
    open_requests_.insert(handle);
    return handle;
  };

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

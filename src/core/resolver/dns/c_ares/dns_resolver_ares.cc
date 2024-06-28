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

#include <stdint.h>

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/types/optional.h"

#include <grpc/impl/channel_arg_names.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>

#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/iomgr/resolved_address.h"
#include "src/core/lib/uri/uri_parser.h"
#include "src/core/resolver/dns/event_engine/service_config_helper.h"
#include "src/core/resolver/resolver.h"
#include "src/core/resolver/resolver_factory.h"
#include "src/core/service_config/service_config.h"

#if GRPC_ARES == 1

#include <address_sorting/address_sorting.h>

#include "absl/strings/str_cat.h"

#include "src/core/lib/backoff/backoff.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/config/config_vars.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/load_balancing/grpclb/grpclb_balancer_addresses.h"
#include "src/core/resolver/dns/c_ares/grpc_ares_wrapper.h"
#include "src/core/resolver/endpoint_addresses.h"
#include "src/core/resolver/polling_resolver.h"
#include "src/core/service_config/service_config_impl.h"

#define GRPC_DNS_INITIAL_CONNECT_BACKOFF_SECONDS 1
#define GRPC_DNS_RECONNECT_BACKOFF_MULTIPLIER 1.6
#define GRPC_DNS_RECONNECT_MAX_BACKOFF_SECONDS 120
#define GRPC_DNS_RECONNECT_JITTER 0.2

namespace grpc_core {

namespace {

class AresClientChannelDNSResolver final : public PollingResolver {
 public:
  AresClientChannelDNSResolver(ResolverArgs args,
                               Duration min_time_between_resolutions);

  OrphanablePtr<Orphanable> StartRequest() override;

 private:
  class AresRequestWrapper final
      : public InternallyRefCounted<AresRequestWrapper> {
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
            resolver_.get(), txt_request_.get());
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
    std::unique_ptr<EndpointAddressesList> addresses_
        ABSL_GUARDED_BY(on_resolved_mu_);
    std::unique_ptr<EndpointAddressesList> balancer_addresses_
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
    ResolverArgs args, Duration min_time_between_resolutions)
    : PollingResolver(std::move(args), min_time_between_resolutions,
                      BackOff::Options()
                          .set_initial_backoff(Duration::Milliseconds(
                              GRPC_DNS_INITIAL_CONNECT_BACKOFF_SECONDS * 1000))
                          .set_multiplier(GRPC_DNS_RECONNECT_BACKOFF_MULTIPLIER)
                          .set_jitter(GRPC_DNS_RECONNECT_JITTER)
                          .set_max_backoff(Duration::Milliseconds(
                              GRPC_DNS_RECONNECT_MAX_BACKOFF_SECONDS * 1000)),
                      &cares_resolver_trace),
      request_service_config_(
          !channel_args()
               .GetBool(GRPC_ARG_SERVICE_CONFIG_DISABLE_RESOLUTION)
               .value_or(true)),
      enable_srv_queries_(channel_args()
                              .GetBool(GRPC_ARG_DNS_ENABLE_SRV_QUERIES)
                              .value_or(false)),
      query_timeout_ms_(
          std::max(0, channel_args()
                          .GetInt(GRPC_ARG_DNS_ARES_QUERY_TIMEOUT_MS)
                          .value_or(GRPC_DNS_ARES_DEFAULT_QUERY_TIMEOUT_MS))) {}

AresClientChannelDNSResolver::~AresClientChannelDNSResolver() {
  GRPC_CARES_TRACE_LOG("resolver:%p destroying AresClientChannelDNSResolver",
                       this);
}

OrphanablePtr<Orphanable> AresClientChannelDNSResolver::StartRequest() {
  return MakeOrphanable<AresRequestWrapper>(
      RefAsSubclass<AresClientChannelDNSResolver>(DEBUG_LOCATION,
                                                  "dns-resolving"));
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
      result.addresses.emplace();
    }
    if (service_config_json_ != nullptr) {
      auto service_config_string = ChooseServiceConfig(service_config_json_);
      if (!service_config_string.ok()) {
        result.service_config = absl::UnavailableError(
            absl::StrCat("failed to parse service config: ",
                         StatusToString(service_config_string.status())));
      } else if (!service_config_string->empty()) {
        GRPC_CARES_TRACE_LOG("resolver:%p selected service config choice: %s",
                             this, service_config_string->c_str());
        result.service_config = ServiceConfigImpl::Create(
            resolver_->channel_args(), *service_config_string);
        if (!result.service_config.ok()) {
          result.service_config = absl::UnavailableError(
              absl::StrCat("failed to parse service config: ",
                           result.service_config.status().message()));
        }
      }
    }
    if (balancer_addresses_ != nullptr) {
      result.args =
          SetGrpcLbBalancerAddresses(result.args, *balancer_addresses_);
    }
  } else {
    GRPC_CARES_TRACE_LOG("resolver:%p dns resolution failed: %s", this,
                         StatusToString(error).c_str());
    std::string error_message;
    grpc_error_get_str(error, StatusStrProperty::kDescription, &error_message);
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

class AresClientChannelDNSResolverFactory final : public ResolverFactory {
 public:
  absl::string_view scheme() const override { return "dns"; }

  bool IsValidUri(const URI& uri) const override {
    if (absl::StripPrefix(uri.path(), "/").empty()) {
      LOG(ERROR) << "no server name supplied in dns URI";
      return false;
    }
    return true;
  }

  OrphanablePtr<Resolver> CreateResolver(ResolverArgs args) const override {
    Duration min_time_between_resolutions = std::max(
        Duration::Zero(), args.args
                              .GetDurationFromIntMillis(
                                  GRPC_ARG_DNS_MIN_TIME_BETWEEN_RESOLUTIONS_MS)
                              .value_or(Duration::Seconds(30)));
    return MakeOrphanable<AresClientChannelDNSResolver>(
        std::move(args), min_time_between_resolutions);
  }
};

class AresDNSResolver final : public DNSResolver {
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
        OnDnsLookupDone(this, absl::CancelledError());
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

  class AresHostnameRequest final : public AresRequest {
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
      if (!error.ok()) {
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
    std::unique_ptr<EndpointAddressesList> addresses_;
  };

  class AresSRVRequest final : public AresRequest {
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
      if (!error.ok()) {
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
    std::unique_ptr<EndpointAddressesList> balancer_addresses_;
  };

  class AresTXTRequest final : public AresRequest {
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
      if (!error.ok()) {
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
  std::shared_ptr<DNSResolver> default_resolver_ = GetDNSResolver();
  Mutex mu_;
  TaskHandleSet open_requests_ ABSL_GUARDED_BY(mu_);
  intptr_t aba_token_ ABSL_GUARDED_BY(mu_) = 0;
};

}  // namespace

bool ShouldUseAresDnsResolver(absl::string_view resolver_env) {
  return resolver_env.empty() || absl::EqualsIgnoreCase(resolver_env, "ares");
}

void RegisterAresDnsResolver(CoreConfiguration::Builder* builder) {
  builder->resolver_registry()->RegisterResolverFactory(
      std::make_unique<AresClientChannelDNSResolverFactory>());
}

}  // namespace grpc_core

void grpc_resolver_dns_ares_init() {
  if (grpc_core::ShouldUseAresDnsResolver(
          grpc_core::ConfigVars::Get().DnsResolver())) {
    address_sorting_init();
    grpc_error_handle error = grpc_ares_init();
    if (!error.ok()) {
      GRPC_LOG_IF_ERROR("grpc_ares_init() failed", error);
      return;
    }
    grpc_core::ResetDNSResolver(std::make_unique<grpc_core::AresDNSResolver>());
  }
}

void grpc_resolver_dns_ares_shutdown() {
  if (grpc_core::ShouldUseAresDnsResolver(
          grpc_core::ConfigVars::Get().DnsResolver())) {
    address_sorting_shutdown();
    grpc_ares_cleanup();
  }
}

void grpc_resolver_dns_ares_reset_dns_resolver() {
  if (grpc_core::ShouldUseAresDnsResolver(
          grpc_core::ConfigVars::Get().DnsResolver())) {
    grpc_core::ResetDNSResolver(std::make_unique<grpc_core::AresDNSResolver>());
  }
}

#else  // GRPC_ARES == 1

namespace grpc_core {
bool ShouldUseAresDnsResolver(absl::string_view /* resolver_env */) {
  return false;
}
void RegisterAresDnsResolver(CoreConfiguration::Builder*) {}
}  // namespace grpc_core

void grpc_resolver_dns_ares_init() {}

void grpc_resolver_dns_ares_shutdown() {}

void grpc_resolver_dns_ares_reset_dns_resolver() {}

#endif  // GRPC_ARES == 1

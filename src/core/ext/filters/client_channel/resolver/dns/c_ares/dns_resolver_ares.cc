/*
 *
 * Copyright 2015 gRPC authors.
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

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>

#include <address_sorting/address_sorting.h>

#include "src/core/ext/filters/client_channel/http_connect_handshaker.h"
#include "src/core/ext/filters/client_channel/lb_policy_registry.h"
#include "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper.h"
#include "src/core/ext/filters/client_channel/resolver_registry.h"
#include "src/core/lib/backoff/backoff.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gpr/host_port.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/manual_constructor.h"
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/gethostname.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/transport/service_config.h"

#define GRPC_DNS_INITIAL_CONNECT_BACKOFF_SECONDS 1
#define GRPC_DNS_RECONNECT_BACKOFF_MULTIPLIER 1.6
#define GRPC_DNS_RECONNECT_MAX_BACKOFF_SECONDS 120
#define GRPC_DNS_RECONNECT_JITTER 0.2

namespace grpc_core {

namespace {

const char kDefaultPort[] = "https";

class AresDnsResolver : public Resolver {
 public:
  explicit AresDnsResolver(const ResolverArgs& args);

  void NextLocked(grpc_channel_args** result,
                  grpc_closure* on_complete) override;

  void RequestReresolutionLocked() override;

  void ShutdownLocked() override;

 private:
  virtual ~AresDnsResolver();

  void MaybeStartResolvingLocked();
  void StartResolvingLocked();
  void MaybeFinishNextLocked();

  static void OnNextResolutionLocked(void* arg, grpc_error* error);
  static void OnResolvedLocked(void* arg, grpc_error* error);

  /// DNS server to use (if not system default)
  char* dns_server_;
  /// name to resolve (usually the same as target_name)
  char* name_to_resolve_;
  /// channel args
  grpc_channel_args* channel_args_;
  /// whether to request the service config
  bool request_service_config_;
  /// pollset_set to drive the name resolution process
  grpc_pollset_set* interested_parties_;
  /// closures used by the combiner
  grpc_closure on_next_resolution_;
  grpc_closure on_resolved_;
  /// are we currently resolving?
  bool resolving_ = false;
  /// the pending resolving request
  grpc_ares_request* pending_request_ = nullptr;
  /// which version of the result have we published?
  int published_version_ = 0;
  /// which version of the result is current?
  int resolved_version_ = 0;
  /// pending next completion, or NULL
  grpc_closure* next_completion_ = nullptr;
  /// target result address for next completion
  grpc_channel_args** target_result_ = nullptr;
  /// current (fully resolved) result
  grpc_channel_args* resolved_result_ = nullptr;
  /// next resolution timer
  bool have_next_resolution_timer_ = false;
  grpc_timer next_resolution_timer_;
  /// min interval between DNS requests
  grpc_millis min_time_between_resolutions_;
  /// timestamp of last DNS request
  grpc_millis last_resolution_timestamp_ = -1;
  /// retry backoff state
  BackOff backoff_;
  /// currently resolving addresses
  grpc_lb_addresses* lb_addresses_ = nullptr;
  /// currently resolving service config
  char* service_config_json_ = nullptr;
};

AresDnsResolver::AresDnsResolver(const ResolverArgs& args)
    : Resolver(args.combiner),
      backoff_(
          BackOff::Options()
              .set_initial_backoff(GRPC_DNS_INITIAL_CONNECT_BACKOFF_SECONDS *
                                   1000)
              .set_multiplier(GRPC_DNS_RECONNECT_BACKOFF_MULTIPLIER)
              .set_jitter(GRPC_DNS_RECONNECT_JITTER)
              .set_max_backoff(GRPC_DNS_RECONNECT_MAX_BACKOFF_SECONDS * 1000)) {
  // Get name to resolve from URI path.
  const char* path = args.uri->path;
  if (path[0] == '/') ++path;
  name_to_resolve_ = gpr_strdup(path);
  // Get DNS server from URI authority.
  if (0 != strcmp(args.uri->authority, "")) {
    dns_server_ = gpr_strdup(args.uri->authority);
  }
  channel_args_ = grpc_channel_args_copy(args.args);
  const grpc_arg* arg = grpc_channel_args_find(
      channel_args_, GRPC_ARG_SERVICE_CONFIG_DISABLE_RESOLUTION);
  request_service_config_ = !grpc_channel_arg_get_integer(
      arg, (grpc_integer_options){false, false, true});
  arg = grpc_channel_args_find(channel_args_,
                               GRPC_ARG_DNS_MIN_TIME_BETWEEN_RESOLUTIONS_MS);
  min_time_between_resolutions_ =
      grpc_channel_arg_get_integer(arg, {1000, 0, INT_MAX});
  interested_parties_ = grpc_pollset_set_create();
  if (args.pollset_set != nullptr) {
    grpc_pollset_set_add_pollset_set(interested_parties_, args.pollset_set);
  }
  GRPC_CLOSURE_INIT(&on_next_resolution_, OnNextResolutionLocked, this,
                    grpc_combiner_scheduler(combiner()));
  GRPC_CLOSURE_INIT(&on_resolved_, OnResolvedLocked, this,
                    grpc_combiner_scheduler(combiner()));
}

AresDnsResolver::~AresDnsResolver() {
  gpr_log(GPR_DEBUG, "destroying AresDnsResolver");
  if (resolved_result_ != nullptr) {
    grpc_channel_args_destroy(resolved_result_);
  }
  grpc_pollset_set_destroy(interested_parties_);
  gpr_free(dns_server_);
  gpr_free(name_to_resolve_);
  grpc_channel_args_destroy(channel_args_);
}

void AresDnsResolver::NextLocked(grpc_channel_args** target_result,
                                 grpc_closure* on_complete) {
  gpr_log(GPR_DEBUG, "AresDnsResolver::NextLocked() is called.");
  GPR_ASSERT(next_completion_ == nullptr);
  next_completion_ = on_complete;
  target_result_ = target_result;
  if (resolved_version_ == 0 && !resolving_) {
    MaybeStartResolvingLocked();
  } else {
    MaybeFinishNextLocked();
  }
}

void AresDnsResolver::RequestReresolutionLocked() {
  if (!resolving_) {
    MaybeStartResolvingLocked();
  }
}

void AresDnsResolver::ShutdownLocked() {
  if (have_next_resolution_timer_) {
    grpc_timer_cancel(&next_resolution_timer_);
  }
  if (pending_request_ != nullptr) {
    grpc_cancel_ares_request(pending_request_);
  }
  if (next_completion_ != nullptr) {
    *target_result_ = nullptr;
    GRPC_CLOSURE_SCHED(next_completion_, GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                                             "Resolver Shutdown"));
    next_completion_ = nullptr;
  }
}

void AresDnsResolver::OnNextResolutionLocked(void* arg, grpc_error* error) {
  AresDnsResolver* r = static_cast<AresDnsResolver*>(arg);
  r->have_next_resolution_timer_ = false;
  if (error == GRPC_ERROR_NONE) {
    if (!r->resolving_) {
      r->StartResolvingLocked();
    }
  }
  r->Unref(DEBUG_LOCATION, "next_resolution_timer");
}

bool ValueInJsonArray(grpc_json* array, const char* value) {
  for (grpc_json* entry = array->child; entry != nullptr; entry = entry->next) {
    if (entry->type == GRPC_JSON_STRING && strcmp(entry->value, value) == 0) {
      return true;
    }
  }
  return false;
}

char* ChooseServiceConfig(char* service_config_choice_json) {
  grpc_json* choices_json = grpc_json_parse_string(service_config_choice_json);
  if (choices_json == nullptr || choices_json->type != GRPC_JSON_ARRAY) {
    gpr_log(GPR_ERROR, "cannot parse service config JSON string");
    return nullptr;
  }
  char* service_config = nullptr;
  for (grpc_json* choice = choices_json->child; choice != nullptr;
       choice = choice->next) {
    if (choice->type != GRPC_JSON_OBJECT) {
      gpr_log(GPR_ERROR, "cannot parse service config JSON string");
      break;
    }
    grpc_json* service_config_json = nullptr;
    for (grpc_json* field = choice->child; field != nullptr;
         field = field->next) {
      // Check client language, if specified.
      if (strcmp(field->key, "clientLanguage") == 0) {
        if (field->type != GRPC_JSON_ARRAY || !ValueInJsonArray(field, "c++")) {
          service_config_json = nullptr;
          break;
        }
      }
      // Check client hostname, if specified.
      if (strcmp(field->key, "clientHostname") == 0) {
        char* hostname = grpc_gethostname();
        if (hostname == nullptr || field->type != GRPC_JSON_ARRAY ||
            !ValueInJsonArray(field, hostname)) {
          service_config_json = nullptr;
          break;
        }
      }
      // Check percentage, if specified.
      if (strcmp(field->key, "percentage") == 0) {
        if (field->type != GRPC_JSON_NUMBER) {
          service_config_json = nullptr;
          break;
        }
        int random_pct = rand() % 100;
        int percentage;
        if (sscanf(field->value, "%d", &percentage) != 1 ||
            random_pct > percentage || percentage == 0) {
          service_config_json = nullptr;
          break;
        }
      }
      // Save service config.
      if (strcmp(field->key, "serviceConfig") == 0) {
        if (field->type == GRPC_JSON_OBJECT) {
          service_config_json = field;
        }
      }
    }
    if (service_config_json != nullptr) {
      service_config = grpc_json_dump_to_string(service_config_json, 0);
      break;
    }
  }
  grpc_json_destroy(choices_json);
  return service_config;
}

void AresDnsResolver::OnResolvedLocked(void* arg, grpc_error* error) {
  AresDnsResolver* r = static_cast<AresDnsResolver*>(arg);
  grpc_channel_args* result = nullptr;
  GPR_ASSERT(r->resolving_);
  r->resolving_ = false;
  r->pending_request_ = nullptr;
  if (r->lb_addresses_ != nullptr) {
    static const char* args_to_remove[2];
    size_t num_args_to_remove = 0;
    grpc_arg new_args[3];
    size_t num_args_to_add = 0;
    new_args[num_args_to_add++] =
        grpc_lb_addresses_create_channel_arg(r->lb_addresses_);
    grpc_core::UniquePtr<grpc_core::ServiceConfig> service_config;
    char* service_config_string = nullptr;
    if (r->service_config_json_ != nullptr) {
      service_config_string = ChooseServiceConfig(r->service_config_json_);
      gpr_free(r->service_config_json_);
      if (service_config_string != nullptr) {
        gpr_log(GPR_INFO, "selected service config choice: %s",
                service_config_string);
        args_to_remove[num_args_to_remove++] = GRPC_ARG_SERVICE_CONFIG;
        new_args[num_args_to_add++] = grpc_channel_arg_string_create(
            (char*)GRPC_ARG_SERVICE_CONFIG, service_config_string);
        service_config =
            grpc_core::ServiceConfig::Create(service_config_string);
        if (service_config != nullptr) {
          const char* lb_policy_name =
              service_config->GetLoadBalancingPolicyName();
          if (lb_policy_name != nullptr) {
            args_to_remove[num_args_to_remove++] = GRPC_ARG_LB_POLICY_NAME;
            new_args[num_args_to_add++] = grpc_channel_arg_string_create(
                (char*)GRPC_ARG_LB_POLICY_NAME,
                const_cast<char*>(lb_policy_name));
          }
        }
      }
    }
    result = grpc_channel_args_copy_and_add_and_remove(
        r->channel_args_, args_to_remove, num_args_to_remove, new_args,
        num_args_to_add);
    gpr_free(service_config_string);
    grpc_lb_addresses_destroy(r->lb_addresses_);
    // Reset backoff state so that we start from the beginning when the
    // next request gets triggered.
    r->backoff_.Reset();
  } else {
    const char* msg = grpc_error_string(error);
    gpr_log(GPR_DEBUG, "dns resolution failed: %s", msg);
    grpc_millis next_try = r->backoff_.NextAttemptTime();
    grpc_millis timeout = next_try - ExecCtx::Get()->Now();
    gpr_log(GPR_INFO, "dns resolution failed (will retry): %s",
            grpc_error_string(error));
    GPR_ASSERT(!r->have_next_resolution_timer_);
    r->have_next_resolution_timer_ = true;
    // TODO(roth): We currently deal with this ref manually.  Once the
    // new closure API is done, find a way to track this ref with the timer
    // callback as part of the type system.
    RefCountedPtr<Resolver> self = r->Ref(DEBUG_LOCATION, "retry-timer");
    self.release();
    if (timeout > 0) {
      gpr_log(GPR_DEBUG, "retrying in %" PRIdPTR " milliseconds", timeout);
    } else {
      gpr_log(GPR_DEBUG, "retrying immediately");
    }
    grpc_timer_init(&r->next_resolution_timer_, next_try,
                    &r->on_next_resolution_);
  }
  if (r->resolved_result_ != nullptr) {
    grpc_channel_args_destroy(r->resolved_result_);
  }
  r->resolved_result_ = result;
  ++r->resolved_version_;
  r->MaybeFinishNextLocked();
  r->Unref(DEBUG_LOCATION, "dns-resolving");
}

void AresDnsResolver::MaybeStartResolvingLocked() {
  if (last_resolution_timestamp_ >= 0) {
    const grpc_millis earliest_next_resolution =
        last_resolution_timestamp_ + min_time_between_resolutions_;
    const grpc_millis ms_until_next_resolution =
        earliest_next_resolution - grpc_core::ExecCtx::Get()->Now();
    if (ms_until_next_resolution > 0) {
      const grpc_millis last_resolution_ago =
          grpc_core::ExecCtx::Get()->Now() - last_resolution_timestamp_;
      gpr_log(GPR_DEBUG,
              "In cooldown from last resolution (from %" PRIdPTR
              " ms ago). Will resolve again in %" PRIdPTR " ms",
              last_resolution_ago, ms_until_next_resolution);
      if (!have_next_resolution_timer_) {
        have_next_resolution_timer_ = true;
        // TODO(roth): We currently deal with this ref manually.  Once the
        // new closure API is done, find a way to track this ref with the timer
        // callback as part of the type system.
        RefCountedPtr<Resolver> self =
            Ref(DEBUG_LOCATION, "next_resolution_timer_cooldown");
        self.release();
        grpc_timer_init(&next_resolution_timer_, ms_until_next_resolution,
                        &on_next_resolution_);
      }
      // TODO(dgq): remove the following two lines once Pick First stops
      // discarding subchannels after selecting.
      ++resolved_version_;
      MaybeFinishNextLocked();
      return;
    }
  }
  StartResolvingLocked();
}

void AresDnsResolver::StartResolvingLocked() {
  // TODO(roth): We currently deal with this ref manually.  Once the
  // new closure API is done, find a way to track this ref with the timer
  // callback as part of the type system.
  RefCountedPtr<Resolver> self = Ref(DEBUG_LOCATION, "dns-resolving");
  self.release();
  GPR_ASSERT(!resolving_);
  resolving_ = true;
  lb_addresses_ = nullptr;
  service_config_json_ = nullptr;
  pending_request_ = grpc_dns_lookup_ares(
      dns_server_, name_to_resolve_, kDefaultPort, interested_parties_,
      &on_resolved_, &lb_addresses_, true /* check_grpclb */,
      request_service_config_ ? &service_config_json_ : nullptr);
  last_resolution_timestamp_ = grpc_core::ExecCtx::Get()->Now();
}

void AresDnsResolver::MaybeFinishNextLocked() {
  if (next_completion_ != nullptr && resolved_version_ != published_version_) {
    *target_result_ = resolved_result_ == nullptr
                          ? nullptr
                          : grpc_channel_args_copy(resolved_result_);
    gpr_log(GPR_DEBUG, "AresDnsResolver::MaybeFinishNextLocked()");
    GRPC_CLOSURE_SCHED(next_completion_, GRPC_ERROR_NONE);
    next_completion_ = nullptr;
    published_version_ = resolved_version_;
  }
}

//
// Factory
//

class AresDnsResolverFactory : public ResolverFactory {
 public:
  OrphanablePtr<Resolver> CreateResolver(
      const ResolverArgs& args) const override {
    return OrphanablePtr<Resolver>(New<AresDnsResolver>(args));
  }

  const char* scheme() const override { return "dns"; }
};

}  // namespace

}  // namespace grpc_core

extern grpc_address_resolver_vtable* grpc_resolve_address_impl;
static grpc_address_resolver_vtable* default_resolver;

static grpc_error* blocking_resolve_address_ares(
    const char* name, const char* default_port,
    grpc_resolved_addresses** addresses) {
  return default_resolver->blocking_resolve_address(name, default_port,
                                                    addresses);
}

static grpc_address_resolver_vtable ares_resolver = {
    grpc_resolve_address_ares, blocking_resolve_address_ares};

void grpc_resolver_dns_ares_init() {
  char* resolver_env = gpr_getenv("GRPC_DNS_RESOLVER");
  /* TODO(zyc): Turn on c-ares based resolver by default after the address
     sorter and the CNAME support are added. */
  if (resolver_env != nullptr && gpr_stricmp(resolver_env, "ares") == 0) {
    address_sorting_init();
    grpc_error* error = grpc_ares_init();
    if (error != GRPC_ERROR_NONE) {
      GRPC_LOG_IF_ERROR("ares_library_init() failed", error);
      return;
    }
    default_resolver = grpc_resolve_address_impl;
    grpc_set_resolver_impl(&ares_resolver);
    grpc_core::ResolverRegistry::Builder::RegisterResolverFactory(
        grpc_core::UniquePtr<grpc_core::ResolverFactory>(
            grpc_core::New<grpc_core::AresDnsResolverFactory>()));
  }
  gpr_free(resolver_env);
}

void grpc_resolver_dns_ares_shutdown() {
  char* resolver_env = gpr_getenv("GRPC_DNS_RESOLVER");
  if (resolver_env != nullptr && gpr_stricmp(resolver_env, "ares") == 0) {
    address_sorting_shutdown();
    grpc_ares_cleanup();
  }
  gpr_free(resolver_env);
}

#else /* GRPC_ARES == 1 && !defined(GRPC_UV) */

void grpc_resolver_dns_ares_init(void) {}

void grpc_resolver_dns_ares_shutdown(void) {}

#endif /* GRPC_ARES == 1 && !defined(GRPC_UV) */

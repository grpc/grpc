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
#include <grpc/support/host_port.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/filters/client_channel/http_connect_handshaker.h"
#include "src/core/ext/filters/client_channel/lb_policy_registry.h"
#include "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper.h"
#include "src/core/ext/filters/client_channel/resolver_registry.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/gethostname.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/support/backoff.h"
#include "src/core/lib/support/env.h"
#include "src/core/lib/support/string.h"
#include "src/core/lib/transport/service_config.h"

#define GRPC_DNS_MIN_CONNECT_TIMEOUT_SECONDS 1
#define GRPC_DNS_INITIAL_CONNECT_BACKOFF_SECONDS 1
#define GRPC_DNS_RECONNECT_BACKOFF_MULTIPLIER 1.6
#define GRPC_DNS_RECONNECT_MAX_BACKOFF_SECONDS 120
#define GRPC_DNS_RECONNECT_JITTER 0.2

typedef struct {
  /** base class: must be first */
  grpc_resolver base;
  /** DNS server to use (if not system default) */
  char *dns_server;
  /** name to resolve (usually the same as target_name) */
  char *name_to_resolve;
  /** default port to use */
  char *default_port;
  /** channel args. */
  grpc_channel_args *channel_args;
  /** whether to request the service config */
  bool request_service_config;
  /** pollset_set to drive the name resolution process */
  grpc_pollset_set *interested_parties;

  /** Closures used by the combiner */
  grpc_closure dns_ares_on_retry_timer_locked;
  grpc_closure dns_ares_on_resolved_locked;

  /** Combiner guarding the rest of the state */
  grpc_combiner *combiner;
  /** are we currently resolving? */
  bool resolving;
  /** the pending resolving request */
  grpc_ares_request *pending_request;
  /** which version of the result have we published? */
  int published_version;
  /** which version of the result is current? */
  int resolved_version;
  /** pending next completion, or NULL */
  grpc_closure *next_completion;
  /** target result address for next completion */
  grpc_channel_args **target_result;
  /** current (fully resolved) result */
  grpc_channel_args *resolved_result;
  /** retry timer */
  bool have_retry_timer;
  grpc_timer retry_timer;
  /** retry backoff state */
  gpr_backoff backoff_state;

  /** currently resolving addresses */
  grpc_lb_addresses *lb_addresses;
  /** currently resolving service config */
  char *service_config_json;
} ares_dns_resolver;

static void dns_ares_destroy(grpc_exec_ctx *exec_ctx, grpc_resolver *r);

static void dns_ares_start_resolving_locked(grpc_exec_ctx *exec_ctx,
                                            ares_dns_resolver *r);
static void dns_ares_maybe_finish_next_locked(grpc_exec_ctx *exec_ctx,
                                              ares_dns_resolver *r);

static void dns_ares_shutdown_locked(grpc_exec_ctx *exec_ctx, grpc_resolver *r);
static void dns_ares_channel_saw_error_locked(grpc_exec_ctx *exec_ctx,
                                              grpc_resolver *r);
static void dns_ares_next_locked(grpc_exec_ctx *exec_ctx, grpc_resolver *r,
                                 grpc_channel_args **target_result,
                                 grpc_closure *on_complete);

static const grpc_resolver_vtable dns_ares_resolver_vtable = {
    dns_ares_destroy, dns_ares_shutdown_locked,
    dns_ares_channel_saw_error_locked, dns_ares_next_locked};

static void dns_ares_shutdown_locked(grpc_exec_ctx *exec_ctx,
                                     grpc_resolver *resolver) {
  ares_dns_resolver *r = (ares_dns_resolver *)resolver;
  if (r->have_retry_timer) {
    grpc_timer_cancel(exec_ctx, &r->retry_timer);
  }
  if (r->pending_request != NULL) {
    grpc_cancel_ares_request(exec_ctx, r->pending_request);
  }
  if (r->next_completion != NULL) {
    *r->target_result = NULL;
    GRPC_CLOSURE_SCHED(
        exec_ctx, r->next_completion,
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("Resolver Shutdown"));
    r->next_completion = NULL;
  }
}

static void dns_ares_channel_saw_error_locked(grpc_exec_ctx *exec_ctx,
                                              grpc_resolver *resolver) {
  ares_dns_resolver *r = (ares_dns_resolver *)resolver;
  if (!r->resolving) {
    gpr_backoff_reset(&r->backoff_state);
    dns_ares_start_resolving_locked(exec_ctx, r);
  }
}

static void dns_ares_on_retry_timer_locked(grpc_exec_ctx *exec_ctx, void *arg,
                                           grpc_error *error) {
  ares_dns_resolver *r = arg;
  r->have_retry_timer = false;
  if (error == GRPC_ERROR_NONE) {
    if (!r->resolving) {
      dns_ares_start_resolving_locked(exec_ctx, r);
    }
  }
  GRPC_RESOLVER_UNREF(exec_ctx, &r->base, "retry-timer");
}

static bool value_in_json_array(grpc_json *array, const char *value) {
  for (grpc_json *entry = array->child; entry != NULL; entry = entry->next) {
    if (entry->type == GRPC_JSON_STRING && strcmp(entry->value, value) == 0) {
      return true;
    }
  }
  return false;
}

static char *choose_service_config(char *service_config_choice_json) {
  grpc_json *choices_json = grpc_json_parse_string(service_config_choice_json);
  if (choices_json == NULL || choices_json->type != GRPC_JSON_ARRAY) {
    gpr_log(GPR_ERROR, "cannot parse service config JSON string");
    return NULL;
  }
  char *service_config = NULL;
  for (grpc_json *choice = choices_json->child; choice != NULL;
       choice = choice->next) {
    if (choice->type != GRPC_JSON_OBJECT) {
      gpr_log(GPR_ERROR, "cannot parse service config JSON string");
      break;
    }
    grpc_json *service_config_json = NULL;
    for (grpc_json *field = choice->child; field != NULL; field = field->next) {
      // Check client language, if specified.
      if (strcmp(field->key, "clientLanguage") == 0) {
        if (field->type != GRPC_JSON_ARRAY ||
            !value_in_json_array(field, "c++")) {
          service_config_json = NULL;
          break;
        }
      }
      // Check client hostname, if specified.
      if (strcmp(field->key, "clientHostname") == 0) {
        char *hostname = grpc_gethostname();
        if (hostname == NULL || field->type != GRPC_JSON_ARRAY ||
            !value_in_json_array(field, hostname)) {
          service_config_json = NULL;
          break;
        }
      }
      // Check percentage, if specified.
      if (strcmp(field->key, "percentage") == 0) {
        if (field->type != GRPC_JSON_NUMBER) {
          service_config_json = NULL;
          break;
        }
        int random_pct = rand() % 100;
        int percentage;
        if (sscanf(field->value, "%d", &percentage) != 1 ||
            random_pct > percentage) {
          service_config_json = NULL;
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
    if (service_config_json != NULL) {
      service_config = grpc_json_dump_to_string(service_config_json, 0);
      break;
    }
  }
  grpc_json_destroy(choices_json);
  return service_config;
}

static void dns_ares_on_resolved_locked(grpc_exec_ctx *exec_ctx, void *arg,
                                        grpc_error *error) {
  ares_dns_resolver *r = arg;
  grpc_channel_args *result = NULL;
  GPR_ASSERT(r->resolving);
  r->resolving = false;
  r->pending_request = NULL;
  if (r->lb_addresses != NULL) {
    static const char *args_to_remove[2];
    size_t num_args_to_remove = 0;
    grpc_arg new_args[3];
    size_t num_args_to_add = 0;
    new_args[num_args_to_add++] =
        grpc_lb_addresses_create_channel_arg(r->lb_addresses);
    grpc_service_config *service_config = NULL;
    char *service_config_string = NULL;
    if (r->service_config_json != NULL) {
      service_config_string = choose_service_config(r->service_config_json);
      gpr_free(r->service_config_json);
      if (service_config_string != NULL) {
        gpr_log(GPR_INFO, "selected service config choice: %s",
                service_config_string);
        args_to_remove[num_args_to_remove++] = GRPC_ARG_SERVICE_CONFIG;
        new_args[num_args_to_add++] = grpc_channel_arg_string_create(
            GRPC_ARG_SERVICE_CONFIG, service_config_string);
        service_config = grpc_service_config_create(service_config_string);
        if (service_config != NULL) {
          const char *lb_policy_name =
              grpc_service_config_get_lb_policy_name(service_config);
          if (lb_policy_name != NULL) {
            args_to_remove[num_args_to_remove++] = GRPC_ARG_LB_POLICY_NAME;
            new_args[num_args_to_add++] = grpc_channel_arg_string_create(
                GRPC_ARG_LB_POLICY_NAME, (char *)lb_policy_name);
          }
        }
      }
    }
    result = grpc_channel_args_copy_and_add_and_remove(
        r->channel_args, args_to_remove, num_args_to_remove, new_args,
        num_args_to_add);
    if (service_config != NULL) grpc_service_config_destroy(service_config);
    gpr_free(service_config_string);
    grpc_lb_addresses_destroy(exec_ctx, r->lb_addresses);
  } else {
    const char *msg = grpc_error_string(error);
    gpr_log(GPR_DEBUG, "dns resolution failed: %s", msg);
    gpr_timespec now = gpr_now(GPR_CLOCK_MONOTONIC);
    gpr_timespec next_try = gpr_backoff_step(&r->backoff_state, now);
    gpr_timespec timeout = gpr_time_sub(next_try, now);
    gpr_log(GPR_INFO, "dns resolution failed (will retry): %s",
            grpc_error_string(error));
    GPR_ASSERT(!r->have_retry_timer);
    r->have_retry_timer = true;
    GRPC_RESOLVER_REF(&r->base, "retry-timer");
    if (gpr_time_cmp(timeout, gpr_time_0(timeout.clock_type)) > 0) {
      gpr_log(GPR_DEBUG, "retrying in %" PRId64 ".%09d seconds", timeout.tv_sec,
              timeout.tv_nsec);
    } else {
      gpr_log(GPR_DEBUG, "retrying immediately");
    }
    grpc_timer_init(exec_ctx, &r->retry_timer, next_try,
                    &r->dns_ares_on_retry_timer_locked, now);
  }
  if (r->resolved_result != NULL) {
    grpc_channel_args_destroy(exec_ctx, r->resolved_result);
  }
  r->resolved_result = result;
  r->resolved_version++;
  dns_ares_maybe_finish_next_locked(exec_ctx, r);
  GRPC_RESOLVER_UNREF(exec_ctx, &r->base, "dns-resolving");
}

static void dns_ares_next_locked(grpc_exec_ctx *exec_ctx,
                                 grpc_resolver *resolver,
                                 grpc_channel_args **target_result,
                                 grpc_closure *on_complete) {
  gpr_log(GPR_DEBUG, "dns_ares_next is called.");
  ares_dns_resolver *r = (ares_dns_resolver *)resolver;
  GPR_ASSERT(!r->next_completion);
  r->next_completion = on_complete;
  r->target_result = target_result;
  if (r->resolved_version == 0 && !r->resolving) {
    gpr_backoff_reset(&r->backoff_state);
    dns_ares_start_resolving_locked(exec_ctx, r);
  } else {
    dns_ares_maybe_finish_next_locked(exec_ctx, r);
  }
}

static void dns_ares_start_resolving_locked(grpc_exec_ctx *exec_ctx,
                                            ares_dns_resolver *r) {
  GRPC_RESOLVER_REF(&r->base, "dns-resolving");
  GPR_ASSERT(!r->resolving);
  r->resolving = true;
  r->lb_addresses = NULL;
  r->service_config_json = NULL;
  r->pending_request = grpc_dns_lookup_ares(
      exec_ctx, r->dns_server, r->name_to_resolve, r->default_port,
      r->interested_parties, &r->dns_ares_on_resolved_locked, &r->lb_addresses,
      true /* check_grpclb */,
      r->request_service_config ? &r->service_config_json : NULL);
}

static void dns_ares_maybe_finish_next_locked(grpc_exec_ctx *exec_ctx,
                                              ares_dns_resolver *r) {
  if (r->next_completion != NULL &&
      r->resolved_version != r->published_version) {
    *r->target_result = r->resolved_result == NULL
                            ? NULL
                            : grpc_channel_args_copy(r->resolved_result);
    gpr_log(GPR_DEBUG, "dns_ares_maybe_finish_next_locked");
    GRPC_CLOSURE_SCHED(exec_ctx, r->next_completion, GRPC_ERROR_NONE);
    r->next_completion = NULL;
    r->published_version = r->resolved_version;
  }
}

static void dns_ares_destroy(grpc_exec_ctx *exec_ctx, grpc_resolver *gr) {
  gpr_log(GPR_DEBUG, "dns_ares_destroy");
  ares_dns_resolver *r = (ares_dns_resolver *)gr;
  if (r->resolved_result != NULL) {
    grpc_channel_args_destroy(exec_ctx, r->resolved_result);
  }
  grpc_pollset_set_destroy(exec_ctx, r->interested_parties);
  gpr_free(r->dns_server);
  gpr_free(r->name_to_resolve);
  gpr_free(r->default_port);
  grpc_channel_args_destroy(exec_ctx, r->channel_args);
  gpr_free(r);
}

static grpc_resolver *dns_ares_create(grpc_exec_ctx *exec_ctx,
                                      grpc_resolver_args *args,
                                      const char *default_port) {
  /* Get name from args. */
  const char *path = args->uri->path;
  if (path[0] == '/') ++path;
  /* Create resolver. */
  ares_dns_resolver *r = gpr_zalloc(sizeof(ares_dns_resolver));
  grpc_resolver_init(&r->base, &dns_ares_resolver_vtable, args->combiner);
  if (0 != strcmp(args->uri->authority, "")) {
    r->dns_server = gpr_strdup(args->uri->authority);
  }
  r->name_to_resolve = gpr_strdup(path);
  r->default_port = gpr_strdup(default_port);
  r->channel_args = grpc_channel_args_copy(args->args);
  const grpc_arg *arg = grpc_channel_args_find(
      r->channel_args, GRPC_ARG_SERVICE_CONFIG_DISABLE_RESOLUTION);
  r->request_service_config = !grpc_channel_arg_get_integer(
      arg, (grpc_integer_options){false, false, true});
  r->interested_parties = grpc_pollset_set_create();
  if (args->pollset_set != NULL) {
    grpc_pollset_set_add_pollset_set(exec_ctx, r->interested_parties,
                                     args->pollset_set);
  }
  gpr_backoff_init(&r->backoff_state, GRPC_DNS_INITIAL_CONNECT_BACKOFF_SECONDS,
                   GRPC_DNS_RECONNECT_BACKOFF_MULTIPLIER,
                   GRPC_DNS_RECONNECT_JITTER,
                   GRPC_DNS_MIN_CONNECT_TIMEOUT_SECONDS * 1000,
                   GRPC_DNS_RECONNECT_MAX_BACKOFF_SECONDS * 1000);
  GRPC_CLOSURE_INIT(&r->dns_ares_on_retry_timer_locked,
                    dns_ares_on_retry_timer_locked, r,
                    grpc_combiner_scheduler(r->base.combiner));
  GRPC_CLOSURE_INIT(&r->dns_ares_on_resolved_locked,
                    dns_ares_on_resolved_locked, r,
                    grpc_combiner_scheduler(r->base.combiner));
  return &r->base;
}

/*
 * FACTORY
 */

static void dns_ares_factory_ref(grpc_resolver_factory *factory) {}

static void dns_ares_factory_unref(grpc_resolver_factory *factory) {}

static grpc_resolver *dns_factory_create_resolver(
    grpc_exec_ctx *exec_ctx, grpc_resolver_factory *factory,
    grpc_resolver_args *args) {
  return dns_ares_create(exec_ctx, args, "https");
}

static char *dns_ares_factory_get_default_host_name(
    grpc_resolver_factory *factory, grpc_uri *uri) {
  const char *path = uri->path;
  if (path[0] == '/') ++path;
  return gpr_strdup(path);
}

static const grpc_resolver_factory_vtable dns_ares_factory_vtable = {
    dns_ares_factory_ref, dns_ares_factory_unref, dns_factory_create_resolver,
    dns_ares_factory_get_default_host_name, "dns"};
static grpc_resolver_factory dns_resolver_factory = {&dns_ares_factory_vtable};

static grpc_resolver_factory *dns_ares_resolver_factory_create() {
  return &dns_resolver_factory;
}

void grpc_resolver_dns_ares_init(void) {
  char *resolver = gpr_getenv("GRPC_DNS_RESOLVER");
  /* TODO(zyc): Turn on c-ares based resolver by default after the address
     sorter and the CNAME support are added. */
  if (resolver != NULL && gpr_stricmp(resolver, "ares") == 0) {
    grpc_error *error = grpc_ares_init();
    if (error != GRPC_ERROR_NONE) {
      GRPC_LOG_IF_ERROR("ares_library_init() failed", error);
      return;
    }
    grpc_resolve_address = grpc_resolve_address_ares;
    grpc_register_resolver_type(dns_ares_resolver_factory_create());
  }
  gpr_free(resolver);
}

void grpc_resolver_dns_ares_shutdown(void) {
  char *resolver = gpr_getenv("GRPC_DNS_RESOLVER");
  if (resolver != NULL && gpr_stricmp(resolver, "ares") == 0) {
    grpc_ares_cleanup();
  }
  gpr_free(resolver);
}

#else /* GRPC_ARES == 1 && !defined(GRPC_UV) */

void grpc_resolver_dns_ares_init(void) {}

void grpc_resolver_dns_ares_shutdown(void) {}

#endif /* GRPC_ARES == 1 && !defined(GRPC_UV) */

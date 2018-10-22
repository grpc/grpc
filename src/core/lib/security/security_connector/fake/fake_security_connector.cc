/*
 *
 * Copyright 2018 gRPC authors.
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

#include "src/core/lib/security/security_connector/fake/fake_security_connector.h"

#include <stdbool.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/transport/chttp2/alpn/alpn.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/handshaker.h"
#include "src/core/lib/gpr/host_port.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/security/context/security_context.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/credentials/fake/fake_credentials.h"
#include "src/core/lib/security/transport/security_handshaker.h"
#include "src/core/lib/security/transport/target_authority_table.h"
#include "src/core/tsi/fake_transport_security.h"

typedef struct {
  grpc_channel_security_connector base;
  char* target;
  char* expected_targets;
  bool is_lb_channel;
  char* target_name_override;
} grpc_fake_channel_security_connector;

static void fake_channel_destroy(grpc_security_connector* sc) {
  grpc_fake_channel_security_connector* c =
      reinterpret_cast<grpc_fake_channel_security_connector*>(sc);
  grpc_call_credentials_unref(c->base.request_metadata_creds);
  gpr_free(c->target);
  gpr_free(c->expected_targets);
  gpr_free(c->target_name_override);
  gpr_free(c);
}

static void fake_server_destroy(grpc_security_connector* sc) { gpr_free(sc); }

static bool fake_check_target(const char* target_type, const char* target,
                              const char* set_str) {
  GPR_ASSERT(target_type != nullptr);
  GPR_ASSERT(target != nullptr);
  char** set = nullptr;
  size_t set_size = 0;
  gpr_string_split(set_str, ",", &set, &set_size);
  bool found = false;
  for (size_t i = 0; i < set_size; ++i) {
    if (set[i] != nullptr && strcmp(target, set[i]) == 0) found = true;
  }
  for (size_t i = 0; i < set_size; ++i) {
    gpr_free(set[i]);
  }
  gpr_free(set);
  return found;
}

static void fake_secure_name_check(const char* target,
                                   const char* expected_targets,
                                   bool is_lb_channel) {
  if (expected_targets == nullptr) return;
  char** lbs_and_backends = nullptr;
  size_t lbs_and_backends_size = 0;
  bool success = false;
  gpr_string_split(expected_targets, ";", &lbs_and_backends,
                   &lbs_and_backends_size);
  if (lbs_and_backends_size > 2 || lbs_and_backends_size == 0) {
    gpr_log(GPR_ERROR, "Invalid expected targets arg value: '%s'",
            expected_targets);
    goto done;
  }
  if (is_lb_channel) {
    if (lbs_and_backends_size != 2) {
      gpr_log(GPR_ERROR,
              "Invalid expected targets arg value: '%s'. Expectations for LB "
              "channels must be of the form 'be1,be2,be3,...;lb1,lb2,...",
              expected_targets);
      goto done;
    }
    if (!fake_check_target("LB", target, lbs_and_backends[1])) {
      gpr_log(GPR_ERROR, "LB target '%s' not found in expected set '%s'",
              target, lbs_and_backends[1]);
      goto done;
    }
    success = true;
  } else {
    if (!fake_check_target("Backend", target, lbs_and_backends[0])) {
      gpr_log(GPR_ERROR, "Backend target '%s' not found in expected set '%s'",
              target, lbs_and_backends[0]);
      goto done;
    }
    success = true;
  }
done:
  for (size_t i = 0; i < lbs_and_backends_size; ++i) {
    gpr_free(lbs_and_backends[i]);
  }
  gpr_free(lbs_and_backends);
  if (!success) abort();
}

static void fake_check_peer(grpc_security_connector* sc, tsi_peer peer,
                            grpc_auth_context** auth_context,
                            grpc_closure* on_peer_checked) {
  const char* prop_name;
  grpc_error* error = GRPC_ERROR_NONE;
  *auth_context = nullptr;
  if (peer.property_count != 1) {
    error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Fake peers should only have 1 property.");
    goto end;
  }
  prop_name = peer.properties[0].name;
  if (prop_name == nullptr ||
      strcmp(prop_name, TSI_CERTIFICATE_TYPE_PEER_PROPERTY)) {
    char* msg;
    gpr_asprintf(&msg, "Unexpected property in fake peer: %s.",
                 prop_name == nullptr ? "<EMPTY>" : prop_name);
    error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg);
    gpr_free(msg);
    goto end;
  }
  if (strncmp(peer.properties[0].value.data, TSI_FAKE_CERTIFICATE_TYPE,
              peer.properties[0].value.length)) {
    error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Invalid value for cert type property.");
    goto end;
  }
  *auth_context = grpc_auth_context_create(nullptr);
  grpc_auth_context_add_cstring_property(
      *auth_context, GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME,
      GRPC_FAKE_TRANSPORT_SECURITY_TYPE);
end:
  GRPC_CLOSURE_SCHED(on_peer_checked, error);
  tsi_peer_destruct(&peer);
}

static void fake_channel_check_peer(grpc_security_connector* sc, tsi_peer peer,
                                    grpc_auth_context** auth_context,
                                    grpc_closure* on_peer_checked) {
  fake_check_peer(sc, peer, auth_context, on_peer_checked);
  grpc_fake_channel_security_connector* c =
      reinterpret_cast<grpc_fake_channel_security_connector*>(sc);
  fake_secure_name_check(c->target, c->expected_targets, c->is_lb_channel);
}

static void fake_server_check_peer(grpc_security_connector* sc, tsi_peer peer,
                                   grpc_auth_context** auth_context,
                                   grpc_closure* on_peer_checked) {
  fake_check_peer(sc, peer, auth_context, on_peer_checked);
}

static int fake_channel_cmp(grpc_security_connector* sc1,
                            grpc_security_connector* sc2) {
  grpc_fake_channel_security_connector* c1 =
      reinterpret_cast<grpc_fake_channel_security_connector*>(sc1);
  grpc_fake_channel_security_connector* c2 =
      reinterpret_cast<grpc_fake_channel_security_connector*>(sc2);
  int c = grpc_channel_security_connector_cmp(&c1->base, &c2->base);
  if (c != 0) return c;
  c = strcmp(c1->target, c2->target);
  if (c != 0) return c;
  if (c1->expected_targets == nullptr || c2->expected_targets == nullptr) {
    c = GPR_ICMP(c1->expected_targets, c2->expected_targets);
  } else {
    c = strcmp(c1->expected_targets, c2->expected_targets);
  }
  if (c != 0) return c;
  return GPR_ICMP(c1->is_lb_channel, c2->is_lb_channel);
}

static int fake_server_cmp(grpc_security_connector* sc1,
                           grpc_security_connector* sc2) {
  return grpc_server_security_connector_cmp(
      reinterpret_cast<grpc_server_security_connector*>(sc1),
      reinterpret_cast<grpc_server_security_connector*>(sc2));
}

static bool fake_channel_check_call_host(grpc_channel_security_connector* sc,
                                         const char* host,
                                         grpc_auth_context* auth_context,
                                         grpc_closure* on_call_host_checked,
                                         grpc_error** error) {
  grpc_fake_channel_security_connector* c =
      reinterpret_cast<grpc_fake_channel_security_connector*>(sc);
  char* authority_hostname = nullptr;
  char* authority_ignored_port = nullptr;
  char* target_hostname = nullptr;
  char* target_ignored_port = nullptr;
  gpr_split_host_port(host, &authority_hostname, &authority_ignored_port);
  gpr_split_host_port(c->target, &target_hostname, &target_ignored_port);
  if (c->target_name_override != nullptr) {
    char* fake_security_target_name_override_hostname = nullptr;
    char* fake_security_target_name_override_ignored_port = nullptr;
    gpr_split_host_port(c->target_name_override,
                        &fake_security_target_name_override_hostname,
                        &fake_security_target_name_override_ignored_port);
    if (strcmp(authority_hostname,
               fake_security_target_name_override_hostname) != 0) {
      gpr_log(GPR_ERROR,
              "Authority (host) '%s' != Fake Security Target override '%s'",
              host, fake_security_target_name_override_hostname);
      abort();
    }
    gpr_free(fake_security_target_name_override_hostname);
    gpr_free(fake_security_target_name_override_ignored_port);
  } else if (strcmp(authority_hostname, target_hostname) != 0) {
    gpr_log(GPR_ERROR, "Authority (host) '%s' != Target '%s'",
            authority_hostname, target_hostname);
    abort();
  }
  gpr_free(authority_hostname);
  gpr_free(authority_ignored_port);
  gpr_free(target_hostname);
  gpr_free(target_ignored_port);
  return true;
}

static void fake_channel_cancel_check_call_host(
    grpc_channel_security_connector* sc, grpc_closure* on_call_host_checked,
    grpc_error* error) {
  GRPC_ERROR_UNREF(error);
}

static void fake_channel_add_handshakers(
    grpc_channel_security_connector* sc, grpc_pollset_set* interested_parties,
    grpc_handshake_manager* handshake_mgr) {
  grpc_handshake_manager_add(
      handshake_mgr,
      grpc_security_handshaker_create(
          tsi_create_fake_handshaker(true /* is_client */), &sc->base));
}

static void fake_server_add_handshakers(grpc_server_security_connector* sc,
                                        grpc_pollset_set* interested_parties,
                                        grpc_handshake_manager* handshake_mgr) {
  grpc_handshake_manager_add(
      handshake_mgr,
      grpc_security_handshaker_create(
          tsi_create_fake_handshaker(false /* is_client */), &sc->base));
}

static grpc_security_connector_vtable fake_channel_vtable = {
    fake_channel_destroy, fake_channel_check_peer, fake_channel_cmp};

static grpc_security_connector_vtable fake_server_vtable = {
    fake_server_destroy, fake_server_check_peer, fake_server_cmp};

grpc_channel_security_connector* grpc_fake_channel_security_connector_create(
    grpc_channel_credentials* channel_creds,
    grpc_call_credentials* request_metadata_creds, const char* target,
    const grpc_channel_args* args) {
  grpc_fake_channel_security_connector* c =
      static_cast<grpc_fake_channel_security_connector*>(
          gpr_zalloc(sizeof(*c)));
  gpr_ref_init(&c->base.base.refcount, 1);
  c->base.base.url_scheme = GRPC_FAKE_SECURITY_URL_SCHEME;
  c->base.base.vtable = &fake_channel_vtable;
  c->base.channel_creds = channel_creds;
  c->base.request_metadata_creds =
      grpc_call_credentials_ref(request_metadata_creds);
  c->base.check_call_host = fake_channel_check_call_host;
  c->base.cancel_check_call_host = fake_channel_cancel_check_call_host;
  c->base.add_handshakers = fake_channel_add_handshakers;
  c->target = gpr_strdup(target);
  const char* expected_targets = grpc_fake_transport_get_expected_targets(args);
  c->expected_targets = gpr_strdup(expected_targets);
  c->is_lb_channel = grpc_core::FindTargetAuthorityTableInArgs(args) != nullptr;
  const grpc_arg* target_name_override_arg =
      grpc_channel_args_find(args, GRPC_SSL_TARGET_NAME_OVERRIDE_ARG);
  if (target_name_override_arg != nullptr) {
    c->target_name_override =
        gpr_strdup(grpc_channel_arg_get_string(target_name_override_arg));
  }
  return &c->base;
}

grpc_server_security_connector* grpc_fake_server_security_connector_create(
    grpc_server_credentials* server_creds) {
  grpc_server_security_connector* c =
      static_cast<grpc_server_security_connector*>(
          gpr_zalloc(sizeof(grpc_server_security_connector)));
  gpr_ref_init(&c->base.refcount, 1);
  c->base.vtable = &fake_server_vtable;
  c->base.url_scheme = GRPC_FAKE_SECURITY_URL_SCHEME;
  c->server_creds = server_creds;
  c->add_handshakers = fake_server_add_handshakers;
  return c;
}

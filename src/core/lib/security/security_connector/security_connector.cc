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

#include "src/core/lib/security/security_connector/security_connector.h"

#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/transport/chttp2/alpn/alpn.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/handshaker.h"
#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gpr/host_port.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/iomgr/load_file.h"
#include "src/core/lib/security/context/security_context.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/security_connector/load_system_roots.h"
#include "src/core/lib/security/transport/security_handshaker.h"

grpc_core::DebugOnlyTraceFlag grpc_trace_security_connector_refcount(
    false, "security_connector_refcount");

void grpc_channel_security_connector_add_handshakers(
    grpc_channel_security_connector* connector,
    grpc_pollset_set* interested_parties,
    grpc_handshake_manager* handshake_mgr) {
  if (connector != nullptr) {
    connector->add_handshakers(connector, interested_parties, handshake_mgr);
  }
}

void grpc_server_security_connector_add_handshakers(
    grpc_server_security_connector* connector,
    grpc_pollset_set* interested_parties,
    grpc_handshake_manager* handshake_mgr) {
  if (connector != nullptr) {
    connector->add_handshakers(connector, interested_parties, handshake_mgr);
  }
}

void grpc_security_connector_check_peer(grpc_security_connector* sc,
                                        tsi_peer peer,
                                        grpc_auth_context** auth_context,
                                        grpc_closure* on_peer_checked) {
  if (sc == nullptr) {
    GRPC_CLOSURE_SCHED(on_peer_checked,
                       GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                           "cannot check peer -- no security connector"));
    tsi_peer_destruct(&peer);
  } else {
    sc->vtable->check_peer(sc, peer, auth_context, on_peer_checked);
  }
}

int grpc_security_connector_cmp(grpc_security_connector* sc,
                                grpc_security_connector* other) {
  if (sc == nullptr || other == nullptr) return GPR_ICMP(sc, other);
  int c = GPR_ICMP(sc->vtable, other->vtable);
  if (c != 0) return c;
  return sc->vtable->cmp(sc, other);
}

int grpc_channel_security_connector_cmp(grpc_channel_security_connector* sc1,
                                        grpc_channel_security_connector* sc2) {
  GPR_ASSERT(sc1->channel_creds != nullptr);
  GPR_ASSERT(sc2->channel_creds != nullptr);
  int c = GPR_ICMP(sc1->channel_creds, sc2->channel_creds);
  if (c != 0) return c;
  c = GPR_ICMP(sc1->request_metadata_creds, sc2->request_metadata_creds);
  if (c != 0) return c;
  c = GPR_ICMP((void*)sc1->check_call_host, (void*)sc2->check_call_host);
  if (c != 0) return c;
  c = GPR_ICMP((void*)sc1->cancel_check_call_host,
               (void*)sc2->cancel_check_call_host);
  if (c != 0) return c;
  return GPR_ICMP((void*)sc1->add_handshakers, (void*)sc2->add_handshakers);
}

int grpc_server_security_connector_cmp(grpc_server_security_connector* sc1,
                                       grpc_server_security_connector* sc2) {
  GPR_ASSERT(sc1->server_creds != nullptr);
  GPR_ASSERT(sc2->server_creds != nullptr);
  int c = GPR_ICMP(sc1->server_creds, sc2->server_creds);
  if (c != 0) return c;
  return GPR_ICMP((void*)sc1->add_handshakers, (void*)sc2->add_handshakers);
}

bool grpc_channel_security_connector_check_call_host(
    grpc_channel_security_connector* sc, const char* host,
    grpc_auth_context* auth_context, grpc_closure* on_call_host_checked,
    grpc_error** error) {
  if (sc == nullptr || sc->check_call_host == nullptr) {
    *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "cannot check call host -- no security connector");
    return true;
  }
  return sc->check_call_host(sc, host, auth_context, on_call_host_checked,
                             error);
}

void grpc_channel_security_connector_cancel_check_call_host(
    grpc_channel_security_connector* sc, grpc_closure* on_call_host_checked,
    grpc_error* error) {
  if (sc == nullptr || sc->cancel_check_call_host == nullptr) {
    GRPC_ERROR_UNREF(error);
    return;
  }
  sc->cancel_check_call_host(sc, on_call_host_checked, error);
}

#ifndef NDEBUG
grpc_security_connector* grpc_security_connector_ref(
    grpc_security_connector* sc, const char* file, int line,
    const char* reason) {
  if (sc == nullptr) return nullptr;
  if (grpc_trace_security_connector_refcount.enabled()) {
    gpr_atm val = gpr_atm_no_barrier_load(&sc->refcount.count);
    gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG,
            "SECURITY_CONNECTOR:%p   ref %" PRIdPTR " -> %" PRIdPTR " %s", sc,
            val, val + 1, reason);
  }
#else
grpc_security_connector* grpc_security_connector_ref(
    grpc_security_connector* sc) {
  if (sc == nullptr) return nullptr;
#endif
  gpr_ref(&sc->refcount);
  return sc;
}

#ifndef NDEBUG
void grpc_security_connector_unref(grpc_security_connector* sc,
                                   const char* file, int line,
                                   const char* reason) {
  if (sc == nullptr) return;
  if (grpc_trace_security_connector_refcount.enabled()) {
    gpr_atm val = gpr_atm_no_barrier_load(&sc->refcount.count);
    gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG,
            "SECURITY_CONNECTOR:%p unref %" PRIdPTR " -> %" PRIdPTR " %s", sc,
            val, val - 1, reason);
  }
#else
void grpc_security_connector_unref(grpc_security_connector* sc) {
  if (sc == nullptr) return;
#endif
  if (gpr_unref(&sc->refcount)) sc->vtable->destroy(sc);
}

static void connector_arg_destroy(void* p) {
  GRPC_SECURITY_CONNECTOR_UNREF((grpc_security_connector*)p,
                                "connector_arg_destroy");
}

static void* connector_arg_copy(void* p) {
  return GRPC_SECURITY_CONNECTOR_REF((grpc_security_connector*)p,
                                     "connector_arg_copy");
}

static int connector_cmp(void* a, void* b) {
  return grpc_security_connector_cmp(static_cast<grpc_security_connector*>(a),
                                     static_cast<grpc_security_connector*>(b));
}

static const grpc_arg_pointer_vtable connector_arg_vtable = {
    connector_arg_copy, connector_arg_destroy, connector_cmp};

grpc_arg grpc_security_connector_to_arg(grpc_security_connector* sc) {
  return grpc_channel_arg_pointer_create((char*)GRPC_ARG_SECURITY_CONNECTOR, sc,
                                         &connector_arg_vtable);
}

grpc_security_connector* grpc_security_connector_from_arg(const grpc_arg* arg) {
  if (strcmp(arg->key, GRPC_ARG_SECURITY_CONNECTOR)) return nullptr;
  if (arg->type != GRPC_ARG_POINTER) {
    gpr_log(GPR_ERROR, "Invalid type %d for arg %s", arg->type,
            GRPC_ARG_SECURITY_CONNECTOR);
    return nullptr;
  }
  return static_cast<grpc_security_connector*>(arg->value.pointer.p);
}

grpc_security_connector* grpc_security_connector_find_in_args(
    const grpc_channel_args* args) {
  size_t i;
  if (args == nullptr) return nullptr;
  for (i = 0; i < args->num_args; i++) {
    grpc_security_connector* sc =
        grpc_security_connector_from_arg(&args->args[i]);
    if (sc != nullptr) return sc;
  }
  return nullptr;
}

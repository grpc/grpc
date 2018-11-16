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

#ifndef GRPC_CORE_LIB_SECURITY_SECURITY_CONNECTOR_SECURITY_CONNECTOR_H
#define GRPC_CORE_LIB_SECURITY_SECURITY_CONNECTOR_SECURITY_CONNECTOR_H

#include <grpc/support/port_platform.h>

#include <stdbool.h>

#include <grpc/grpc_security.h>

#include "src/core/lib/channel/handshaker.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/pollset.h"
#include "src/core/lib/iomgr/tcp_server.h"
#include "src/core/tsi/ssl_transport_security.h"
#include "src/core/tsi/transport_security_interface.h"

extern grpc_core::DebugOnlyTraceFlag grpc_trace_security_connector_refcount;

/* --- status enum. --- */

typedef enum { GRPC_SECURITY_OK = 0, GRPC_SECURITY_ERROR } grpc_security_status;

/* --- security_connector object. ---

    A security connector object represents away to configure the underlying
    transport security mechanism and check the resulting trusted peer.  */

typedef struct grpc_security_connector grpc_security_connector;

#define GRPC_ARG_SECURITY_CONNECTOR "grpc.security_connector"

typedef struct {
  void (*destroy)(grpc_security_connector* sc);
  void (*check_peer)(grpc_security_connector* sc, tsi_peer peer,
                     grpc_auth_context** auth_context,
                     grpc_closure* on_peer_checked);
  int (*cmp)(grpc_security_connector* sc, grpc_security_connector* other);
} grpc_security_connector_vtable;

struct grpc_security_connector {
  const grpc_security_connector_vtable* vtable;
  gpr_refcount refcount;
  const char* url_scheme;
};

/* Refcounting. */
#ifndef NDEBUG
#define GRPC_SECURITY_CONNECTOR_REF(p, r) \
  grpc_security_connector_ref((p), __FILE__, __LINE__, (r))
#define GRPC_SECURITY_CONNECTOR_UNREF(p, r) \
  grpc_security_connector_unref((p), __FILE__, __LINE__, (r))
grpc_security_connector* grpc_security_connector_ref(
    grpc_security_connector* policy, const char* file, int line,
    const char* reason);
void grpc_security_connector_unref(grpc_security_connector* policy,
                                   const char* file, int line,
                                   const char* reason);
#else
#define GRPC_SECURITY_CONNECTOR_REF(p, r) grpc_security_connector_ref((p))
#define GRPC_SECURITY_CONNECTOR_UNREF(p, r) grpc_security_connector_unref((p))
grpc_security_connector* grpc_security_connector_ref(
    grpc_security_connector* policy);
void grpc_security_connector_unref(grpc_security_connector* policy);
#endif

/* Check the peer. Callee takes ownership of the peer object.
   When done, sets *auth_context and invokes on_peer_checked. */
void grpc_security_connector_check_peer(grpc_security_connector* sc,
                                        tsi_peer peer,
                                        grpc_auth_context** auth_context,
                                        grpc_closure* on_peer_checked);

/* Compares two security connectors. */
int grpc_security_connector_cmp(grpc_security_connector* sc,
                                grpc_security_connector* other);

/* Util to encapsulate the connector in a channel arg. */
grpc_arg grpc_security_connector_to_arg(grpc_security_connector* sc);

/* Util to get the connector from a channel arg. */
grpc_security_connector* grpc_security_connector_from_arg(const grpc_arg* arg);

/* Util to find the connector from channel args. */
grpc_security_connector* grpc_security_connector_find_in_args(
    const grpc_channel_args* args);

/* --- channel_security_connector object. ---

    A channel security connector object represents a way to configure the
    underlying transport security mechanism on the client side.  */

typedef struct grpc_channel_security_connector grpc_channel_security_connector;

struct grpc_channel_security_connector {
  grpc_security_connector base;
  grpc_channel_credentials* channel_creds;
  grpc_call_credentials* request_metadata_creds;
  bool (*check_call_host)(grpc_channel_security_connector* sc, const char* host,
                          grpc_auth_context* auth_context,
                          grpc_closure* on_call_host_checked,
                          grpc_error** error);
  void (*cancel_check_call_host)(grpc_channel_security_connector* sc,
                                 grpc_closure* on_call_host_checked,
                                 grpc_error* error);
  void (*add_handshakers)(grpc_channel_security_connector* sc,
                          grpc_pollset_set* interested_parties,
                          grpc_handshake_manager* handshake_mgr);
};

/// A helper function for use in grpc_security_connector_cmp() implementations.
int grpc_channel_security_connector_cmp(grpc_channel_security_connector* sc1,
                                        grpc_channel_security_connector* sc2);

/// Checks that the host that will be set for a call is acceptable.
/// Returns true if completed synchronously, in which case \a error will
/// be set to indicate the result.  Otherwise, \a on_call_host_checked
/// will be invoked when complete.
bool grpc_channel_security_connector_check_call_host(
    grpc_channel_security_connector* sc, const char* host,
    grpc_auth_context* auth_context, grpc_closure* on_call_host_checked,
    grpc_error** error);

/// Cancels a pending asychronous call to
/// grpc_channel_security_connector_check_call_host() with
/// \a on_call_host_checked as its callback.
void grpc_channel_security_connector_cancel_check_call_host(
    grpc_channel_security_connector* sc, grpc_closure* on_call_host_checked,
    grpc_error* error);

/* Registers handshakers with \a handshake_mgr. */
void grpc_channel_security_connector_add_handshakers(
    grpc_channel_security_connector* connector,
    grpc_pollset_set* interested_parties,
    grpc_handshake_manager* handshake_mgr);

/* --- server_security_connector object. ---

    A server security connector object represents a way to configure the
    underlying transport security mechanism on the server side.  */

typedef struct grpc_server_security_connector grpc_server_security_connector;

struct grpc_server_security_connector {
  grpc_security_connector base;
  grpc_server_credentials* server_creds;
  void (*add_handshakers)(grpc_server_security_connector* sc,
                          grpc_pollset_set* interested_parties,
                          grpc_handshake_manager* handshake_mgr);
};

/// A helper function for use in grpc_security_connector_cmp() implementations.
int grpc_server_security_connector_cmp(grpc_server_security_connector* sc1,
                                       grpc_server_security_connector* sc2);

void grpc_server_security_connector_add_handshakers(
    grpc_server_security_connector* sc, grpc_pollset_set* interested_parties,
    grpc_handshake_manager* handshake_mgr);

#endif /* GRPC_CORE_LIB_SECURITY_SECURITY_CONNECTOR_SECURITY_CONNECTOR_H */

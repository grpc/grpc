/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef GRPC_CORE_LIB_SECURITY_TRANSPORT_SECURITY_CONNECTOR_H
#define GRPC_CORE_LIB_SECURITY_TRANSPORT_SECURITY_CONNECTOR_H

#include <grpc/grpc_security.h>
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/tcp_server.h"
#include "src/core/lib/tsi/transport_security_interface.h"

/* --- status enum. --- */

typedef enum { GRPC_SECURITY_OK = 0, GRPC_SECURITY_ERROR } grpc_security_status;

/* --- URL schemes. --- */

#define GRPC_SSL_URL_SCHEME "https"
#define GRPC_FAKE_SECURITY_URL_SCHEME "http+fake_security"

/* --- security_connector object. ---

    A security connector object represents away to configure the underlying
    transport security mechanism and check the resulting trusted peer.  */

typedef struct grpc_security_connector grpc_security_connector;

#define GRPC_SECURITY_CONNECTOR_ARG "grpc.security_connector"

typedef void (*grpc_security_peer_check_cb)(grpc_exec_ctx *exec_ctx,
                                            void *user_data,
                                            grpc_security_status status,
                                            grpc_auth_context *auth_context);

/* Ownership of the secure_endpoint is transfered. */
typedef void (*grpc_security_handshake_done_cb)(
    grpc_exec_ctx *exec_ctx, void *user_data, grpc_security_status status,
    grpc_endpoint *secure_endpoint, grpc_auth_context *auth_context);

typedef struct {
  void (*destroy)(grpc_security_connector *sc);
  void (*check_peer)(grpc_exec_ctx *exec_ctx, grpc_security_connector *sc,
                     tsi_peer peer, grpc_security_peer_check_cb cb,
                     void *user_data);
} grpc_security_connector_vtable;

typedef struct grpc_security_connector_handshake_list {
  void *handshake;
  struct grpc_security_connector_handshake_list *next;
} grpc_security_connector_handshake_list;

struct grpc_security_connector {
  const grpc_security_connector_vtable *vtable;
  gpr_refcount refcount;
  const char *url_scheme;
};

/* Refcounting. */
#ifdef GRPC_SECURITY_CONNECTOR_REFCOUNT_DEBUG
#define GRPC_SECURITY_CONNECTOR_REF(p, r) \
  grpc_security_connector_ref((p), __FILE__, __LINE__, (r))
#define GRPC_SECURITY_CONNECTOR_UNREF(p, r) \
  grpc_security_connector_unref((p), __FILE__, __LINE__, (r))
grpc_security_connector *grpc_security_connector_ref(
    grpc_security_connector *policy, const char *file, int line,
    const char *reason);
void grpc_security_connector_unref(grpc_security_connector *policy,
                                   const char *file, int line,
                                   const char *reason);
#else
#define GRPC_SECURITY_CONNECTOR_REF(p, r) grpc_security_connector_ref((p))
#define GRPC_SECURITY_CONNECTOR_UNREF(p, r) grpc_security_connector_unref((p))
grpc_security_connector *grpc_security_connector_ref(
    grpc_security_connector *policy);
void grpc_security_connector_unref(grpc_security_connector *policy);
#endif

/* Check the peer. Callee takes ownership of the peer object.
   The callback will include the resulting auth_context. */
void grpc_security_connector_check_peer(grpc_exec_ctx *exec_ctx,
                                        grpc_security_connector *sc,
                                        tsi_peer peer,
                                        grpc_security_peer_check_cb cb,
                                        void *user_data);

/* Util to encapsulate the connector in a channel arg. */
grpc_arg grpc_security_connector_to_arg(grpc_security_connector *sc);

/* Util to get the connector from a channel arg. */
grpc_security_connector *grpc_security_connector_from_arg(const grpc_arg *arg);

/* Util to find the connector from channel args. */
grpc_security_connector *grpc_find_security_connector_in_args(
    const grpc_channel_args *args);

/* --- channel_security_connector object. ---

    A channel security connector object represents away to configure the
    underlying transport security mechanism on the client side.  */

typedef struct grpc_channel_security_connector grpc_channel_security_connector;

typedef void (*grpc_security_call_host_check_cb)(grpc_exec_ctx *exec_ctx,
                                                 void *user_data,
                                                 grpc_security_status status);

struct grpc_channel_security_connector {
  grpc_security_connector base;
  grpc_call_credentials *request_metadata_creds;
  void (*check_call_host)(grpc_exec_ctx *exec_ctx,
                          grpc_channel_security_connector *sc, const char *host,
                          grpc_auth_context *auth_context,
                          grpc_security_call_host_check_cb cb, void *user_data);
  void (*do_handshake)(grpc_exec_ctx *exec_ctx,
                       grpc_channel_security_connector *sc,
                       grpc_endpoint *nonsecure_endpoint,
                       grpc_security_handshake_done_cb cb, void *user_data);
};

/* Checks that the host that will be set for a call is acceptable. */
void grpc_channel_security_connector_check_call_host(
    grpc_exec_ctx *exec_ctx, grpc_channel_security_connector *sc,
    const char *host, grpc_auth_context *auth_context,
    grpc_security_call_host_check_cb cb, void *user_data);

/* Handshake. */
void grpc_channel_security_connector_do_handshake(
    grpc_exec_ctx *exec_ctx, grpc_channel_security_connector *connector,
    grpc_endpoint *nonsecure_endpoint, grpc_security_handshake_done_cb cb,
    void *user_data);

/* --- server_security_connector object. ---

    A server security connector object represents away to configure the
    underlying transport security mechanism on the server side.  */

typedef struct grpc_server_security_connector grpc_server_security_connector;

struct grpc_server_security_connector {
  grpc_security_connector base;
  gpr_mu mu;
  grpc_security_connector_handshake_list *handshaking_handshakes;
  const grpc_channel_args *channel_args;
  void (*do_handshake)(grpc_exec_ctx *exec_ctx,
                       grpc_server_security_connector *sc,
                       grpc_tcp_server_acceptor *acceptor,
                       grpc_endpoint *nonsecure_endpoint,
                       grpc_security_handshake_done_cb cb, void *user_data);
};

void grpc_server_security_connector_do_handshake(
    grpc_exec_ctx *exec_ctx, grpc_server_security_connector *sc,
    grpc_tcp_server_acceptor *acceptor, grpc_endpoint *nonsecure_endpoint,
    grpc_security_handshake_done_cb cb, void *user_data);

void grpc_server_security_connector_shutdown(
    grpc_exec_ctx *exec_ctx, grpc_server_security_connector *connector);

/* --- Creation security connectors. --- */

/* For TESTING ONLY!
   Creates a fake connector that emulates real channel security.  */
grpc_channel_security_connector *grpc_fake_channel_security_connector_create(
    grpc_call_credentials *request_metadata_creds);

/* For TESTING ONLY!
   Creates a fake connector that emulates real server security.  */
grpc_server_security_connector *grpc_fake_server_security_connector_create(
    void);

/* Config for ssl clients. */
typedef struct {
  unsigned char *pem_private_key;
  size_t pem_private_key_size;
  unsigned char *pem_cert_chain;
  size_t pem_cert_chain_size;
  unsigned char *pem_root_certs;
  size_t pem_root_certs_size;
} grpc_ssl_config;

/* Creates an SSL channel_security_connector.
   - request_metadata_creds is the credentials object which metadata
     will be sent with each request. This parameter can be NULL.
   - config is the SSL config to be used for the SSL channel establishment.
   - is_client should be 0 for a server or a non-0 value for a client.
   - secure_peer_name is the secure peer name that should be checked in
     grpc_channel_security_connector_check_peer. This parameter may be NULL in
     which case the peer name will not be checked. Note that if this parameter
     is not NULL, then, pem_root_certs should not be NULL either.
   - sc is a pointer on the connector to be created.
  This function returns GRPC_SECURITY_OK in case of success or a
  specific error code otherwise.
*/
grpc_security_status grpc_ssl_channel_security_connector_create(
    grpc_call_credentials *request_metadata_creds,
    const grpc_ssl_config *config, const char *target_name,
    const char *overridden_target_name, grpc_channel_security_connector **sc);

/* Gets the default ssl roots. */
size_t grpc_get_default_ssl_roots(const unsigned char **pem_root_certs);

/* Exposed for TESTING ONLY!. */
gpr_slice grpc_get_default_ssl_roots_for_testing(void);

/* Config for ssl servers. */
typedef struct {
  unsigned char **pem_private_keys;
  size_t *pem_private_keys_sizes;
  unsigned char **pem_cert_chains;
  size_t *pem_cert_chains_sizes;
  size_t num_key_cert_pairs;
  unsigned char *pem_root_certs;
  size_t pem_root_certs_size;
  grpc_ssl_client_certificate_request_type client_certificate_request;
} grpc_ssl_server_config;

/* Creates an SSL server_security_connector.
   - config is the SSL config to be used for the SSL channel establishment.
   - sc is a pointer on the connector to be created.
  This function returns GRPC_SECURITY_OK in case of success or a
  specific error code otherwise.
*/
grpc_security_status grpc_ssl_server_security_connector_create(
    const grpc_ssl_server_config *config, grpc_server_security_connector **sc);

/* Util. */
const tsi_peer_property *tsi_peer_get_property_by_name(const tsi_peer *peer,
                                                       const char *name);

/* Exposed for testing only. */
grpc_auth_context *tsi_ssl_peer_to_auth_context(const tsi_peer *peer);
tsi_peer tsi_shallow_peer_from_ssl_auth_context(
    const grpc_auth_context *auth_context);
void tsi_shallow_peer_destruct(tsi_peer *peer);

#endif /* GRPC_CORE_LIB_SECURITY_TRANSPORT_SECURITY_CONNECTOR_H */

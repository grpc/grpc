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

#ifndef GRPC_INTERNAL_CORE_SECURITY_SECURITY_CONTEXT_H
#define GRPC_INTERNAL_CORE_SECURITY_SECURITY_CONTEXT_H

#include <grpc/grpc_security.h>
#include "src/core/iomgr/endpoint.h"
#include "src/core/security/credentials.h"
#include "src/core/tsi/transport_security_interface.h"

/* --- status enum. --- */

typedef enum {
  GRPC_SECURITY_OK = 0,
  GRPC_SECURITY_PENDING,
  GRPC_SECURITY_ERROR
} grpc_security_status;

/* --- URL schemes. --- */

#define GRPC_SSL_URL_SCHEME "https"
#define GRPC_FAKE_SECURITY_URL_SCHEME "http+fake_security"

/* --- security_context object. ---

    A security context object represents away to configure the underlying
    transport security mechanism and check the resulting trusted peer.  */

typedef struct grpc_security_context grpc_security_context;

#define GRPC_SECURITY_CONTEXT_ARG "grpc.security_context"

typedef void (*grpc_security_check_cb)(void *user_data,
                                       grpc_security_status status);

typedef struct {
  void (*destroy)(grpc_security_context *ctx);
  grpc_security_status (*create_handshaker)(grpc_security_context *ctx,
                                            tsi_handshaker **handshaker);
  grpc_security_status (*check_peer)(grpc_security_context *ctx, tsi_peer peer,
                                     grpc_security_check_cb cb,
                                     void *user_data);
} grpc_security_context_vtable;

struct grpc_security_context {
  const grpc_security_context_vtable *vtable;
  gpr_refcount refcount;
  int is_client_side;
  const char *url_scheme;
};

/* Increments the refcount. */
grpc_security_context *grpc_security_context_ref(grpc_security_context *ctx);

/* Decrements the refcount and destroys the object if it reaches 0. */
void grpc_security_context_unref(grpc_security_context *ctx);

/* Handshake creation. */
grpc_security_status grpc_security_context_create_handshaker(
    grpc_security_context *ctx, tsi_handshaker **handshaker);

/* Check the peer.
   Implementations can choose to check the peer either synchronously or
   asynchronously. In the first case, a successful call will return
   GRPC_SECURITY_OK. In the asynchronous case, the call will return
   GRPC_SECURITY_PENDING unless an error is detected early on.
   Ownership of the peer is transfered.
*/
grpc_security_status grpc_security_context_check_peer(
    grpc_security_context *ctx, tsi_peer peer,
    grpc_security_check_cb cb, void *user_data);

/* Util to encapsulate the context in a channel arg. */
grpc_arg grpc_security_context_to_arg(grpc_security_context *ctx);

/* Util to get the context from a channel arg. */
grpc_security_context *grpc_security_context_from_arg(const grpc_arg *arg);

/* Util to find the context from channel args. */
grpc_security_context *grpc_find_security_context_in_args(
    const grpc_channel_args *args);

/* --- channel_security_context object. ---

    A channel security context object represents away to configure the
    underlying transport security mechanism on the client side.  */

typedef struct grpc_channel_security_context grpc_channel_security_context;

struct grpc_channel_security_context {
  grpc_security_context base; /* requires is_client_side to be non 0. */
  grpc_credentials *request_metadata_creds;
  grpc_security_status (*check_call_host)(
      grpc_channel_security_context *ctx, const char *host,
      grpc_security_check_cb cb, void *user_data);
};

/* Checks that the host that will be set for a call is acceptable.
   Implementations can choose do the check either synchronously or
   asynchronously. In the first case, a successful call will return
   GRPC_SECURITY_OK. In the asynchronous case, the call will return
   GRPC_SECURITY_PENDING unless an error is detected early on. */
grpc_security_status grpc_channel_security_context_check_call_host(
    grpc_channel_security_context *ctx, const char *host,
    grpc_security_check_cb cb, void *user_data);

/* --- Creation security contexts. --- */

/* For TESTING ONLY!
   Creates a fake context that emulates real channel security.  */
grpc_channel_security_context *grpc_fake_channel_security_context_create(
    grpc_credentials *request_metadata_creds, int call_host_check_is_async);

/* For TESTING ONLY!
   Creates a fake context that emulates real server security.  */
grpc_security_context *grpc_fake_server_security_context_create(void);

/* Creates an SSL channel_security_context.
   - request_metadata_creds is the credentials object which metadata
     will be sent with each request. This parameter can be NULL.
   - config is the SSL config to be used for the SSL channel establishment.
   - is_client should be 0 for a server or a non-0 value for a client.
   - secure_peer_name is the secure peer name that should be checked in
     grpc_channel_security_context_check_peer. This parameter may be NULL in
     which case the peer name will not be checked. Note that if this parameter
     is not NULL, then, pem_root_certs should not be NULL either.
   - ctx is a pointer on the context to be created.
  This function returns GRPC_SECURITY_OK in case of success or a
  specific error code otherwise.
*/
grpc_security_status grpc_ssl_channel_security_context_create(
    grpc_credentials *request_metadata_creds, const grpc_ssl_config *config,
    const char *target_name, const char *overridden_target_name,
    grpc_channel_security_context **ctx);

/* Creates an SSL server_security_context.
   - config is the SSL config to be used for the SSL channel establishment.
   - ctx is a pointer on the context to be created.
  This function returns GRPC_SECURITY_OK in case of success or a
  specific error code otherwise.
*/
grpc_security_status grpc_ssl_server_security_context_create(
    const grpc_ssl_server_config *config, grpc_security_context **ctx);

/* --- Creation of high level objects. --- */

/* Secure client channel creation. */

size_t grpc_get_default_ssl_roots(const unsigned char **pem_root_certs);

grpc_channel *grpc_ssl_channel_create(grpc_credentials *ssl_creds,
                                      grpc_credentials *request_metadata_creds,
                                      const char *target,
                                      const grpc_channel_args *args);

grpc_channel *grpc_fake_transport_security_channel_create(
    grpc_credentials *fake_creds, grpc_credentials *request_metadata_creds,
    const char *target, const grpc_channel_args *args);

grpc_channel *grpc_secure_channel_create_internal(
    const char *target, const grpc_channel_args *args,
    grpc_channel_security_context *ctx);

typedef grpc_channel *(*grpc_secure_channel_factory_func)(
    grpc_credentials *transport_security_creds,
    grpc_credentials *request_metadata_creds, const char *target,
    const grpc_channel_args *args);

typedef struct {
  const char *creds_type;
  grpc_secure_channel_factory_func factory;
} grpc_secure_channel_factory;

grpc_channel *grpc_secure_channel_create_with_factories(
    const grpc_secure_channel_factory *factories, size_t num_factories,
    grpc_credentials *creds, const char *target, const grpc_channel_args *args);

/* Secure server creation. */

grpc_server *grpc_secure_server_create_internal(grpc_completion_queue *cq,
                                                const grpc_channel_args *args,
                                                grpc_security_context *ctx);

#endif  /* GRPC_INTERNAL_CORE_SECURITY_SECURITY_CONTEXT_H */

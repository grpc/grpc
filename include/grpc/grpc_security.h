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

#ifndef GRPC_GRPC_SECURITY_H
#define GRPC_GRPC_SECURITY_H

#include "grpc.h"
#include "status.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- grpc_credentials object. ---

   A credentials object represents a way to authenticate a client.  */

typedef struct grpc_credentials grpc_credentials;

/* Releases a credentials object.
   The creator of the credentials object is responsible for its release. */
void grpc_credentials_release(grpc_credentials *creds);

/* Creates default credentials to connect to a google gRPC service.
   WARNING: Do NOT use this credentials to connect to a non-google service as
   this could result in an oauth2 token leak. */
grpc_credentials *grpc_google_default_credentials_create(void);

/* Environment variable that points to the default SSL roots file. This file
   must be a PEM encoded file with all the roots such as the one that can be
   downloaded from https://pki.google.com/roots.pem.  */
#define GRPC_DEFAULT_SSL_ROOTS_FILE_PATH_ENV_VAR \
  "GRPC_DEFAULT_SSL_ROOTS_FILE_PATH"

/* Object that holds a private key / certificate chain pair in PEM format. */
typedef struct {
  /* private_key is the NULL-terminated string containing the PEM encoding of
     the client's private key. */
  const char *private_key;

  /* cert_chain is the NULL-terminated string containing the PEM encoding of
     the client's certificate chain. */
  const char *cert_chain;
} grpc_ssl_pem_key_cert_pair;

/* Creates an SSL credentials object.
   - pem_roots_cert is the NULL-terminated string containing the PEM encoding
     of the server root certificates. If this parameter is NULL, the
     implementation will first try to dereference the file pointed by the
     GRPC_DEFAULT_SSL_ROOTS_FILE_PATH environment variable, and if that fails,
     get the roots from a well-known place on disk (in the grpc install
     directory).
   - pem_key_cert_pair is a pointer on the object containing client's private
     key and certificate chain. This parameter can be NULL if the client does
     not have such a key/cert pair.  */
grpc_credentials *grpc_ssl_credentials_create(
    const char *pem_root_certs, grpc_ssl_pem_key_cert_pair *pem_key_cert_pair);

/* Creates a composite credentials object. */
grpc_credentials *grpc_composite_credentials_create(grpc_credentials *creds1,
                                                    grpc_credentials *creds2);

/* Creates a compute engine credentials object.
   WARNING: Do NOT use this credentials to connect to a non-google service as
   this could result in an oauth2 token leak. */
grpc_credentials *grpc_compute_engine_credentials_create(void);

extern const gpr_timespec grpc_max_auth_token_lifetime;

/* Creates a service account credentials object. May return NULL if the input is
   invalid.
   WARNING: Do NOT use this credentials to connect to a non-google service as
   this could result in an oauth2 token leak.
   - json_key is the JSON key string containing the client's private key.
   - scope is a space-delimited list of the requested permissions.
   - token_lifetime is the lifetime of each token acquired through this service
     account credentials.  It should not exceed grpc_max_auth_token_lifetime
     or will be cropped to this value.  */
grpc_credentials *grpc_service_account_credentials_create(
    const char *json_key, const char *scope, gpr_timespec token_lifetime);

/* Creates a JWT credentials object. May return NULL if the input is invalid.
   - json_key is the JSON key string containing the client's private key.
   - token_lifetime is the lifetime of each Json Web Token (JWT) created with
     this credentials.  It should not exceed grpc_max_auth_token_lifetime or
     will be cropped to this value.  */
grpc_credentials *grpc_jwt_credentials_create(const char *json_key,
                                              gpr_timespec token_lifetime);

/* Creates an Oauth2 Refresh Token crednetials object. May return NULL if the
   input is invalid.
   WARNING: Do NOT use this credentials to connect to a non-google service as
   this could result in an oauth2 token leak.
   - json_refresh_token is the JSON string containing the refresh token itself
     along with a client_id and client_secret. */
grpc_credentials *grpc_refresh_token_credentials_create(
    const char *json_refresh_token);

/* Creates a fake transport security credentials object for testing. */
grpc_credentials *grpc_fake_transport_security_credentials_create(void);

/* Creates an IAM credentials object. */
grpc_credentials *grpc_iam_credentials_create(const char *authorization_token,
                                              const char *authority_selector);

/* --- Secure channel creation. --- */

/* The caller of the secure_channel_create functions may override the target
   name used for SSL host name checking using this channel argument which is of
   type GRPC_ARG_STRING. This *should* be used for testing only.
   If this argument is not specified, the name used for SSL host name checking
   will be the target parameter (assuming that the secure channel is an SSL
   channel). If this parameter is specified and the underlying is not an SSL
   channel, it will just be ignored. */
#define GRPC_SSL_TARGET_NAME_OVERRIDE_ARG "grpc.ssl_target_name_override"

/* Creates a secure channel using the passed-in credentials. */
grpc_channel *grpc_secure_channel_create(grpc_credentials *creds,
                                         const char *target,
                                         const grpc_channel_args *args);

/* --- grpc_server_credentials object. ---

   A server credentials object represents a way to authenticate a server.  */

typedef struct grpc_server_credentials grpc_server_credentials;

/* Releases a server_credentials object.
   The creator of the server_credentials object is responsible for its release.
   */
void grpc_server_credentials_release(grpc_server_credentials *creds);

/* Creates an SSL server_credentials object.
   - pem_roots_cert is the NULL-terminated string containing the PEM encoding of
     the client root certificates. This parameter may be NULL if the server does
     not want the client to be authenticated with SSL.
   - pem_key_cert_pairs is an array private key / certificate chains of the
     server. This parameter cannot be NULL.
   - num_key_cert_pairs indicates the number of items in the private_key_files
     and cert_chain_files parameters. It should be at least 1. */
grpc_server_credentials *grpc_ssl_server_credentials_create(
    const char *pem_root_certs, grpc_ssl_pem_key_cert_pair *pem_key_cert_pairs,
    size_t num_key_cert_pairs);

/* Creates a fake server transport security credentials object for testing. */
grpc_server_credentials *grpc_fake_transport_security_server_credentials_create(
    void);

/* --- Server-side secure ports. --- */

/* Add a HTTP2 over an encrypted link over tcp listener.
   Returns bound port number on success, 0 on failure.
   REQUIRES: server not started */
int grpc_server_add_secure_http2_port(grpc_server *server, const char *addr,
                                      grpc_server_credentials *creds);

/* --- Call specific credentials. --- */

/* Sets a credentials to a call. Can only be called on the client side before
   grpc_call_start_batch. */
grpc_call_error grpc_call_set_credentials(grpc_call *call,
                                          grpc_credentials *creds);

#ifdef __cplusplus
}
#endif

#endif  /* GRPC_GRPC_SECURITY_H */

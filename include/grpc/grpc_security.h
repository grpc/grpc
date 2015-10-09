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

#include <grpc/grpc.h>
#include <grpc/status.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- grpc_channel_credentials object. ---

   A channel credentials object represents a way to authenticate a client on a
   channel.  */

typedef struct grpc_channel_credentials grpc_channel_credentials;

/* Releases a channel credentials object.
   The creator of the credentials object is responsible for its release. */
void grpc_credentials_release(grpc_channel_credentials *creds);

/* Environment variable that points to the google default application
   credentials json key or refresh token. Used in the
   grpc_google_default_credentials_create function. */
#define GRPC_GOOGLE_CREDENTIALS_ENV_VAR "GOOGLE_APPLICATION_CREDENTIALS"

/* Creates default credentials to connect to a google gRPC service.
   WARNING: Do NOT use this credentials to connect to a non-google service as
   this could result in an oauth2 token leak. */
grpc_channel_credentials *grpc_google_default_credentials_create(void);

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
     not have such a key/cert pair. */
grpc_channel_credentials *grpc_ssl_credentials_create(
    const char *pem_root_certs, grpc_ssl_pem_key_cert_pair *pem_key_cert_pair,
    void *reserved);

/* --- grpc_call_credentials object.

   A call credentials object represents a way to authenticate on a particular
   call. These credentials can be composed with a channel credentials object
   so that they are sent with every call on this channel.  */

typedef struct grpc_call_credentials grpc_call_credentials;

/* Creates a composite channel credentials object. */
grpc_channel_credentials *grpc_composite_channel_credentials_create(
    grpc_channel_credentials *channel_creds, grpc_call_credentials *call_creds,
    void *reserved);

/* Creates a composite call credentials object. */
grpc_call_credentials *grpc_composite_call_credentials_create(
    grpc_call_credentials *creds1, grpc_call_credentials *creds2,
    void *reserved);

/* Creates a compute engine credentials object for connecting to Google.
   WARNING: Do NOT use this credentials to connect to a non-google service as
   this could result in an oauth2 token leak. */
grpc_call_credentials *grpc_google_compute_engine_credentials_create(
    void *reserved);

extern const gpr_timespec grpc_max_auth_token_lifetime;

/* Creates a JWT credentials object. May return NULL if the input is invalid.
   - json_key is the JSON key string containing the client's private key.
   - token_lifetime is the lifetime of each Json Web Token (JWT) created with
     this credentials.  It should not exceed grpc_max_auth_token_lifetime or
     will be cropped to this value.  */
grpc_call_credentials *grpc_service_account_jwt_access_credentials_create(
    const char *json_key, gpr_timespec token_lifetime, void *reserved);

/* Creates an Oauth2 Refresh Token credentials object for connecting to Google.
   May return NULL if the input is invalid.
   WARNING: Do NOT use this credentials to connect to a non-google service as
   this could result in an oauth2 token leak.
   - json_refresh_token is the JSON string containing the refresh token itself
     along with a client_id and client_secret. */
grpc_call_credentials *grpc_google_refresh_token_credentials_create(
    const char *json_refresh_token, void *reserved);

/* Creates an Oauth2 Access Token credentials with an access token that was
   aquired by an out of band mechanism. */
grpc_call_credentials *grpc_access_token_credentials_create(
    const char *access_token, void *reserved);

/* Creates an IAM credentials object for connecting to Google. */
grpc_call_credentials *grpc_google_iam_credentials_create(
    const char *authorization_token, const char *authority_selector,
    void *reserved);

/* Callback function to be called by the metadata credentials plugin
   implementation when the metadata is ready.
   - user_data is the opaque pointer that was passed in the get_metadata method
     of the grpc_metadata_credentials_plugin (see below).
   - creds_md is an array of credentials metadata produced by the plugin. It
     may be set to NULL in case of an error.
   - num_creds_md is the number of items in the creds_md array.
   - status must be GRPC_STATUS_OK in case of success or another specific error
     code otherwise.
   - error_details contains details about the error if any. In case of success
     it should be NULL and will be otherwise ignored. */
typedef void (*grpc_credentials_plugin_metadata_cb)(
    void *user_data, const grpc_metadata *creds_md, size_t num_creds_md,
    grpc_status_code status, const char *error_details);

/* grpc_metadata_credentials plugin is an API user provided structure used to
   create grpc_credentials objects that can be set on a channel (composed) or
   a call. See grpc_credentials_metadata_create_from_plugin below.
   The grpc client stack will call the get_metadata method of the plugin for
   every call in scope for the credentials created from it. */
typedef struct {
  /* The implementation of this method has to be non-blocking.
     - service_url is the fully qualified URL that the client stack is
       connecting to.
     - cb is the callback that needs to be called when the metadata is ready.
     - user_data needs to be passed as the first parameter of the callback. */
  void (*get_metadata)(void *state, const char *service_url,
                       grpc_credentials_plugin_metadata_cb cb, void *user_data);

  /* Destroys the plugin state. */
  void (*destroy)(void *state);

  /* State that will be set as the first parameter of the methods above. */
  void *state;
} grpc_metadata_credentials_plugin;

/* Creates a credentials object from a plugin. */
grpc_call_credentials *grpc_metadata_credentials_create_from_plugin(
    grpc_metadata_credentials_plugin plugin, void *reserved);

/* --- Secure channel creation. --- */

/* Creates a secure channel using the passed-in credentials. */
grpc_channel *grpc_secure_channel_create(grpc_channel_credentials *creds,
                                         const char *target,
                                         const grpc_channel_args *args,
                                         void *reserved);

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
     and cert_chain_files parameters. It should be at least 1.
   - force_client_auth, if set to non-zero will force the client to authenticate
     with an SSL cert. Note that this option is ignored if pem_root_certs is
     NULL. */
grpc_server_credentials *grpc_ssl_server_credentials_create(
    const char *pem_root_certs, grpc_ssl_pem_key_cert_pair *pem_key_cert_pairs,
    size_t num_key_cert_pairs, int force_client_auth, void *reserved);

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
                                          grpc_call_credentials *creds);

/* --- Authentication Context. --- */

#define GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME "transport_security_type"
#define GRPC_SSL_TRANSPORT_SECURITY_TYPE "ssl"

#define GRPC_X509_CN_PROPERTY_NAME "x509_common_name"
#define GRPC_X509_SAN_PROPERTY_NAME "x509_subject_alternative_name"

typedef struct grpc_auth_context grpc_auth_context;

typedef struct grpc_auth_property_iterator {
  const grpc_auth_context *ctx;
  size_t index;
  const char *name;
} grpc_auth_property_iterator;

/* value, if not NULL, is guaranteed to be NULL terminated. */
typedef struct grpc_auth_property {
  char *name;
  char *value;
  size_t value_length;
} grpc_auth_property;

/* Returns NULL when the iterator is at the end. */
const grpc_auth_property *grpc_auth_property_iterator_next(
    grpc_auth_property_iterator *it);

/* Iterates over the auth context. */
grpc_auth_property_iterator grpc_auth_context_property_iterator(
    const grpc_auth_context *ctx);

/* Gets the peer identity. Returns an empty iterator (first _next will return
   NULL) if the peer is not authenticated. */
grpc_auth_property_iterator grpc_auth_context_peer_identity(
    const grpc_auth_context *ctx);

/* Finds a property in the context. May return an empty iterator (first _next
   will return NULL) if no property with this name was found in the context. */
grpc_auth_property_iterator grpc_auth_context_find_properties_by_name(
    const grpc_auth_context *ctx, const char *name);

/* Gets the name of the property that indicates the peer identity. Will return
   NULL if the peer is not authenticated. */
const char *grpc_auth_context_peer_identity_property_name(
    const grpc_auth_context *ctx);

/* Returns 1 if the peer is authenticated, 0 otherwise. */
int grpc_auth_context_peer_is_authenticated(const grpc_auth_context *ctx);

/* Gets the auth context from the call. Caller needs to call
   grpc_auth_context_release on the returned context. */
grpc_auth_context *grpc_call_auth_context(grpc_call *call);

/* Releases the auth context returned from grpc_call_auth_context. */
void grpc_auth_context_release(grpc_auth_context *context);

/* --
   The following auth context methods should only be called by a server metadata
   processor to set properties extracted from auth metadata.
   -- */

/* Add a property. */
void grpc_auth_context_add_property(grpc_auth_context *ctx, const char *name,
                                    const char *value, size_t value_length);

/* Add a C string property. */
void grpc_auth_context_add_cstring_property(grpc_auth_context *ctx,
                                            const char *name,
                                            const char *value);

/* Sets the property name. Returns 1 if successful or 0 in case of failure
   (which means that no property with this name exists). */
int grpc_auth_context_set_peer_identity_property_name(grpc_auth_context *ctx,
                                                      const char *name);

/* --- Auth Metadata Processing --- */

/* Callback function that is called when the metadata processing is done.
   - Consumed metadata will be removed from the set of metadata available on the
     call. consumed_md may be NULL if no metadata has been consumed.
   - Response metadata will be set on the response. response_md may be NULL.
   - status is GRPC_STATUS_OK for success or a specific status for an error.
     Common error status for auth metadata processing is either
     GRPC_STATUS_UNAUTHENTICATED in case of an authentication failure or
     GRPC_STATUS PERMISSION_DENIED in case of an authorization failure.
   - error_details gives details about the error. May be NULL. */
typedef void (*grpc_process_auth_metadata_done_cb)(
    void *user_data, const grpc_metadata *consumed_md, size_t num_consumed_md,
    const grpc_metadata *response_md, size_t num_response_md,
    grpc_status_code status, const char *error_details);

/* Pluggable server-side metadata processor object. */
typedef struct {
  /* The context object is read/write: it contains the properties of the
     channel peer and it is the job of the process function to augment it with
     properties derived from the passed-in metadata.
     The lifetime of these objects is guaranteed until cb is invoked. */
  void (*process)(void *state, grpc_auth_context *context,
                  const grpc_metadata *md, size_t num_md,
                  grpc_process_auth_metadata_done_cb cb, void *user_data);
  void (*destroy)(void *state);
  void *state;
} grpc_auth_metadata_processor;

void grpc_server_credentials_set_auth_metadata_processor(
    grpc_server_credentials *creds, grpc_auth_metadata_processor processor);

#ifdef __cplusplus
}
#endif

#endif /* GRPC_GRPC_SECURITY_H */

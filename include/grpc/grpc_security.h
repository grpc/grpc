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

#ifndef GRPC_GRPC_SECURITY_H
#define GRPC_GRPC_SECURITY_H

#include <grpc/grpc.h>
#include <grpc/grpc_security_constants.h>
#include <grpc/status.h>

#ifdef __cplusplus
extern "C" {
#endif

/** --- Authentication Context. --- */

typedef struct grpc_auth_context grpc_auth_context;

typedef struct grpc_auth_property_iterator {
  const grpc_auth_context *ctx;
  size_t index;
  const char *name;
} grpc_auth_property_iterator;

/** value, if not NULL, is guaranteed to be NULL terminated. */
typedef struct grpc_auth_property {
  char *name;
  char *value;
  size_t value_length;
} grpc_auth_property;

/** Returns NULL when the iterator is at the end. */
GRPCAPI const grpc_auth_property *grpc_auth_property_iterator_next(
    grpc_auth_property_iterator *it);

/** Iterates over the auth context. */
GRPCAPI grpc_auth_property_iterator
grpc_auth_context_property_iterator(const grpc_auth_context *ctx);

/** Gets the peer identity. Returns an empty iterator (first _next will return
   NULL) if the peer is not authenticated. */
GRPCAPI grpc_auth_property_iterator
grpc_auth_context_peer_identity(const grpc_auth_context *ctx);

/** Finds a property in the context. May return an empty iterator (first _next
   will return NULL) if no property with this name was found in the context. */
GRPCAPI grpc_auth_property_iterator grpc_auth_context_find_properties_by_name(
    const grpc_auth_context *ctx, const char *name);

/** Gets the name of the property that indicates the peer identity. Will return
   NULL if the peer is not authenticated. */
GRPCAPI const char *grpc_auth_context_peer_identity_property_name(
    const grpc_auth_context *ctx);

/** Returns 1 if the peer is authenticated, 0 otherwise. */
GRPCAPI int grpc_auth_context_peer_is_authenticated(
    const grpc_auth_context *ctx);

/** Gets the auth context from the call. Caller needs to call
   grpc_auth_context_release on the returned context. */
GRPCAPI grpc_auth_context *grpc_call_auth_context(grpc_call *call);

/** Releases the auth context returned from grpc_call_auth_context. */
GRPCAPI void grpc_auth_context_release(grpc_auth_context *context);

/** --
   The following auth context methods should only be called by a server metadata
   processor to set properties extracted from auth metadata.
   -- */

/** Add a property. */
GRPCAPI void grpc_auth_context_add_property(grpc_auth_context *ctx,
                                            const char *name, const char *value,
                                            size_t value_length);

/** Add a C string property. */
GRPCAPI void grpc_auth_context_add_cstring_property(grpc_auth_context *ctx,
                                                    const char *name,
                                                    const char *value);

/** Sets the property name. Returns 1 if successful or 0 in case of failure
   (which means that no property with this name exists). */
GRPCAPI int grpc_auth_context_set_peer_identity_property_name(
    grpc_auth_context *ctx, const char *name);

/** --- grpc_channel_credentials object. ---

   A channel credentials object represents a way to authenticate a client on a
   channel.  */

typedef struct grpc_channel_credentials grpc_channel_credentials;

/** Releases a channel credentials object.
   The creator of the credentials object is responsible for its release. */
GRPCAPI void grpc_channel_credentials_release(grpc_channel_credentials *creds);

/** Creates default credentials to connect to a google gRPC service.
   WARNING: Do NOT use this credentials to connect to a non-google service as
   this could result in an oauth2 token leak. */
GRPCAPI grpc_channel_credentials *grpc_google_default_credentials_create(void);

/** Callback for getting the SSL roots override from the application.
   In case of success, *pem_roots_certs must be set to a NULL terminated string
   containing the list of PEM encoded root certificates. The ownership is passed
   to the core and freed (laster by the core) with gpr_free.
   If this function fails and GRPC_DEFAULT_SSL_ROOTS_FILE_PATH environment is
   set to a valid path, it will override the roots specified this func */
typedef grpc_ssl_roots_override_result (*grpc_ssl_roots_override_callback)(
    char **pem_root_certs);

/** Setup a callback to override the default TLS/SSL roots.
   This function is not thread-safe and must be called at initialization time
   before any ssl credentials are created to have the desired side effect.
   If GRPC_DEFAULT_SSL_ROOTS_FILE_PATH environment is set to a valid path, the
   callback will not be called. */
GRPCAPI void grpc_set_ssl_roots_override_callback(
    grpc_ssl_roots_override_callback cb);

/** Object that holds a private key / certificate chain pair in PEM format. */
typedef struct {
  /** private_key is the NULL-terminated string containing the PEM encoding of
     the client's private key. */
  const char *private_key;

  /** cert_chain is the NULL-terminated string containing the PEM encoding of
     the client's certificate chain. */
  const char *cert_chain;
} grpc_ssl_pem_key_cert_pair;

/** Creates an SSL credentials object.
   - pem_root_certs is the NULL-terminated string containing the PEM encoding
     of the server root certificates. If this parameter is NULL, the
     implementation will first try to dereference the file pointed by the
     GRPC_DEFAULT_SSL_ROOTS_FILE_PATH environment variable, and if that fails,
     try to get the roots set by grpc_override_ssl_default_roots. Eventually,
     if all these fail, it will try to get the roots from a well-known place on
     disk (in the grpc install directory).
   - pem_key_cert_pair is a pointer on the object containing client's private
     key and certificate chain. This parameter can be NULL if the client does
     not have such a key/cert pair. */
GRPCAPI grpc_channel_credentials *grpc_ssl_credentials_create(
    const char *pem_root_certs, grpc_ssl_pem_key_cert_pair *pem_key_cert_pair,
    void *reserved);

/** --- grpc_call_credentials object.

   A call credentials object represents a way to authenticate on a particular
   call. These credentials can be composed with a channel credentials object
   so that they are sent with every call on this channel.  */

typedef struct grpc_call_credentials grpc_call_credentials;

/** Releases a call credentials object.
   The creator of the credentials object is responsible for its release. */
GRPCAPI void grpc_call_credentials_release(grpc_call_credentials *creds);

/** Creates a composite channel credentials object. */
GRPCAPI grpc_channel_credentials *grpc_composite_channel_credentials_create(
    grpc_channel_credentials *channel_creds, grpc_call_credentials *call_creds,
    void *reserved);

/** Creates a composite call credentials object. */
GRPCAPI grpc_call_credentials *grpc_composite_call_credentials_create(
    grpc_call_credentials *creds1, grpc_call_credentials *creds2,
    void *reserved);

/** Creates a compute engine credentials object for connecting to Google.
   WARNING: Do NOT use this credentials to connect to a non-google service as
   this could result in an oauth2 token leak. */
GRPCAPI grpc_call_credentials *grpc_google_compute_engine_credentials_create(
    void *reserved);

GRPCAPI gpr_timespec grpc_max_auth_token_lifetime();

/** Creates a JWT credentials object. May return NULL if the input is invalid.
   - json_key is the JSON key string containing the client's private key.
   - token_lifetime is the lifetime of each Json Web Token (JWT) created with
     this credentials.  It should not exceed grpc_max_auth_token_lifetime or
     will be cropped to this value.  */
GRPCAPI grpc_call_credentials *
grpc_service_account_jwt_access_credentials_create(const char *json_key,
                                                   gpr_timespec token_lifetime,
                                                   void *reserved);

/** Creates an Oauth2 Refresh Token credentials object for connecting to Google.
   May return NULL if the input is invalid.
   WARNING: Do NOT use this credentials to connect to a non-google service as
   this could result in an oauth2 token leak.
   - json_refresh_token is the JSON string containing the refresh token itself
     along with a client_id and client_secret. */
GRPCAPI grpc_call_credentials *grpc_google_refresh_token_credentials_create(
    const char *json_refresh_token, void *reserved);

/** Creates an Oauth2 Access Token credentials with an access token that was
   aquired by an out of band mechanism. */
GRPCAPI grpc_call_credentials *grpc_access_token_credentials_create(
    const char *access_token, void *reserved);

/** Creates an IAM credentials object for connecting to Google. */
GRPCAPI grpc_call_credentials *grpc_google_iam_credentials_create(
    const char *authorization_token, const char *authority_selector,
    void *reserved);

/** Callback function to be called by the metadata credentials plugin
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

/** Context that can be used by metadata credentials plugin in order to create
   auth related metadata. */
typedef struct {
  /** The fully qualifed service url. */
  const char *service_url;

  /** The method name of the RPC being called (not fully qualified).
     The fully qualified method name can be built from the service_url:
     full_qualified_method_name = ctx->service_url + '/' + ctx->method_name. */
  const char *method_name;

  /** The auth_context of the channel which gives the server's identity. */
  const grpc_auth_context *channel_auth_context;

  /** Reserved for future use. */
  void *reserved;
} grpc_auth_metadata_context;

/** Maximum number of metadata entries returnable by a credentials plugin via
    a synchronous return. */
#define GRPC_METADATA_CREDENTIALS_PLUGIN_SYNC_MAX 4

/** grpc_metadata_credentials plugin is an API user provided structure used to
   create grpc_credentials objects that can be set on a channel (composed) or
   a call. See grpc_credentials_metadata_create_from_plugin below.
   The grpc client stack will call the get_metadata method of the plugin for
   every call in scope for the credentials created from it. */
typedef struct {
  /** The implementation of this method has to be non-blocking, but can
     be performed synchronously or asynchronously.

     If processing occurs synchronously, returns non-zero and populates
     creds_md, num_creds_md, status, and error_details.  In this case,
     the caller takes ownership of the entries in creds_md and of
     error_details.  Note that if the plugin needs to return more than
     GRPC_METADATA_CREDENTIALS_PLUGIN_SYNC_MAX entries in creds_md, it must
     return asynchronously.

     If processing occurs asynchronously, returns zero and invokes \a cb
     when processing is completed.  \a user_data will be passed as the
     first parameter of the callback.  NOTE: \a cb MUST be invoked in a
     different thread, not from the thread in which \a get_metadata() is
     invoked.

     \a context is the information that can be used by the plugin to create
     auth metadata. */
  int (*get_metadata)(
      void *state, grpc_auth_metadata_context context,
      grpc_credentials_plugin_metadata_cb cb, void *user_data,
      grpc_metadata creds_md[GRPC_METADATA_CREDENTIALS_PLUGIN_SYNC_MAX],
      size_t *num_creds_md, grpc_status_code *status,
      const char **error_details);

  /** Destroys the plugin state. */
  void (*destroy)(void *state);

  /** State that will be set as the first parameter of the methods above. */
  void *state;

  /** Type of credentials that this plugin is implementing. */
  const char *type;
} grpc_metadata_credentials_plugin;

/** Creates a credentials object from a plugin. */
GRPCAPI grpc_call_credentials *grpc_metadata_credentials_create_from_plugin(
    grpc_metadata_credentials_plugin plugin, void *reserved);

/** --- Secure channel creation. --- */

/** Creates a secure channel using the passed-in credentials. */
GRPCAPI grpc_channel *grpc_secure_channel_create(
    grpc_channel_credentials *creds, const char *target,
    const grpc_channel_args *args, void *reserved);

/** --- grpc_server_credentials object. ---

   A server credentials object represents a way to authenticate a server.  */

typedef struct grpc_server_credentials grpc_server_credentials;

/** Releases a server_credentials object.
   The creator of the server_credentials object is responsible for its release.
   */
GRPCAPI void grpc_server_credentials_release(grpc_server_credentials *creds);

/** Deprecated in favor of grpc_ssl_server_credentials_create_ex.
   Creates an SSL server_credentials object.
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
GRPCAPI grpc_server_credentials *grpc_ssl_server_credentials_create(
    const char *pem_root_certs, grpc_ssl_pem_key_cert_pair *pem_key_cert_pairs,
    size_t num_key_cert_pairs, int force_client_auth, void *reserved);

/** Same as grpc_ssl_server_credentials_create method except uses
   grpc_ssl_client_certificate_request_type enum to support more ways to
   authenticate client cerificates.*/
GRPCAPI grpc_server_credentials *grpc_ssl_server_credentials_create_ex(
    const char *pem_root_certs, grpc_ssl_pem_key_cert_pair *pem_key_cert_pairs,
    size_t num_key_cert_pairs,
    grpc_ssl_client_certificate_request_type client_certificate_request,
    void *reserved);

/** --- Server-side secure ports. --- */

/** Add a HTTP2 over an encrypted link over tcp listener.
   Returns bound port number on success, 0 on failure.
   REQUIRES: server not started */
GRPCAPI int grpc_server_add_secure_http2_port(grpc_server *server,
                                              const char *addr,
                                              grpc_server_credentials *creds);

/** --- Call specific credentials. --- */

/** Sets a credentials to a call. Can only be called on the client side before
   grpc_call_start_batch. */
GRPCAPI grpc_call_error grpc_call_set_credentials(grpc_call *call,
                                                  grpc_call_credentials *creds);

/** --- Auth Metadata Processing --- */

/** Callback function that is called when the metadata processing is done.
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

/** Pluggable server-side metadata processor object. */
typedef struct {
  /** The context object is read/write: it contains the properties of the
     channel peer and it is the job of the process function to augment it with
     properties derived from the passed-in metadata.
     The lifetime of these objects is guaranteed until cb is invoked. */
  void (*process)(void *state, grpc_auth_context *context,
                  const grpc_metadata *md, size_t num_md,
                  grpc_process_auth_metadata_done_cb cb, void *user_data);
  void (*destroy)(void *state);
  void *state;
} grpc_auth_metadata_processor;

GRPCAPI void grpc_server_credentials_set_auth_metadata_processor(
    grpc_server_credentials *creds, grpc_auth_metadata_processor processor);

#ifdef __cplusplus
}
#endif

#endif /* GRPC_GRPC_SECURITY_H */

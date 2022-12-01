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

#include <grpc/support/port_platform.h>

#include <grpc/grpc.h>
#include <grpc/grpc_security_constants.h>
#include <grpc/status.h>

#ifdef __cplusplus
extern "C" {
#endif

/** --- Authentication Context. --- */

typedef struct grpc_auth_context grpc_auth_context;

typedef struct grpc_auth_property_iterator {
  const grpc_auth_context* ctx;
  size_t index;
  const char* name;
} grpc_auth_property_iterator;

/** value, if not NULL, is guaranteed to be NULL terminated. */
typedef struct grpc_auth_property {
  char* name;
  char* value;
  size_t value_length;
} grpc_auth_property;

/** Returns NULL when the iterator is at the end. */
GRPCAPI const grpc_auth_property* grpc_auth_property_iterator_next(
    grpc_auth_property_iterator* it);

/** Iterates over the auth context. */
GRPCAPI grpc_auth_property_iterator
grpc_auth_context_property_iterator(const grpc_auth_context* ctx);

/** Gets the peer identity. Returns an empty iterator (first _next will return
   NULL) if the peer is not authenticated. */
GRPCAPI grpc_auth_property_iterator
grpc_auth_context_peer_identity(const grpc_auth_context* ctx);

/** Finds a property in the context. May return an empty iterator (first _next
   will return NULL) if no property with this name was found in the context. */
GRPCAPI grpc_auth_property_iterator grpc_auth_context_find_properties_by_name(
    const grpc_auth_context* ctx, const char* name);

/** Gets the name of the property that indicates the peer identity. Will return
   NULL if the peer is not authenticated. */
GRPCAPI const char* grpc_auth_context_peer_identity_property_name(
    const grpc_auth_context* ctx);

/** Returns 1 if the peer is authenticated, 0 otherwise. */
GRPCAPI int grpc_auth_context_peer_is_authenticated(
    const grpc_auth_context* ctx);

/** Gets the auth context from the call. Caller needs to call
   grpc_auth_context_release on the returned context. */
GRPCAPI grpc_auth_context* grpc_call_auth_context(grpc_call* call);

/** Releases the auth context returned from grpc_call_auth_context. */
GRPCAPI void grpc_auth_context_release(grpc_auth_context* context);

/** --
   The following auth context methods should only be called by a server metadata
   processor to set properties extracted from auth metadata.
   -- */

/** Add a property. */
GRPCAPI void grpc_auth_context_add_property(grpc_auth_context* ctx,
                                            const char* name, const char* value,
                                            size_t value_length);

/** Add a C string property. */
GRPCAPI void grpc_auth_context_add_cstring_property(grpc_auth_context* ctx,
                                                    const char* name,
                                                    const char* value);

/** Sets the property name. Returns 1 if successful or 0 in case of failure
   (which means that no property with this name exists). */
GRPCAPI int grpc_auth_context_set_peer_identity_property_name(
    grpc_auth_context* ctx, const char* name);

/** --- SSL Session Cache. ---

    A SSL session cache object represents a way to cache client sessions
    between connections. Only ticket-based resumption is supported. */

typedef struct grpc_ssl_session_cache grpc_ssl_session_cache;

/** Create LRU cache for client-side SSL sessions with the given capacity.
    If capacity is < 1, a default capacity is used instead. */
GRPCAPI grpc_ssl_session_cache* grpc_ssl_session_cache_create_lru(
    size_t capacity);

/** Destroy SSL session cache. */
GRPCAPI void grpc_ssl_session_cache_destroy(grpc_ssl_session_cache* cache);

/** Create a channel arg with the given cache object. */
GRPCAPI grpc_arg
grpc_ssl_session_cache_create_channel_arg(grpc_ssl_session_cache* cache);

/** --- grpc_call_credentials object.

   A call credentials object represents a way to authenticate on a particular
   call. These credentials can be composed with a channel credentials object
   so that they are sent with every call on this channel.  */

typedef struct grpc_call_credentials grpc_call_credentials;

/** Releases a call credentials object.
   The creator of the credentials object is responsible for its release. */
GRPCAPI void grpc_call_credentials_release(grpc_call_credentials* creds);

/** Callback for getting the SSL roots override from the application.
   In case of success, *pem_roots_certs must be set to a NULL terminated string
   containing the list of PEM encoded root certificates. The ownership is passed
   to the core and freed (laster by the core) with gpr_free.
   If this function fails and GRPC_DEFAULT_SSL_ROOTS_FILE_PATH environment is
   set to a valid path, it will override the roots specified this func */
typedef grpc_ssl_roots_override_result (*grpc_ssl_roots_override_callback)(
    char** pem_root_certs);

/** Setup a callback to override the default TLS/SSL roots.
   This function is not thread-safe and must be called at initialization time
   before any ssl credentials are created to have the desired side effect.
   If GRPC_DEFAULT_SSL_ROOTS_FILE_PATH environment is set to a valid path, the
   callback will not be called. */
GRPCAPI void grpc_set_ssl_roots_override_callback(
    grpc_ssl_roots_override_callback cb);

/** Creates a composite channel credentials object. The security level of
 * resulting connection is determined by channel_creds. */
GRPCAPI grpc_channel_credentials* grpc_composite_channel_credentials_create(
    grpc_channel_credentials* channel_creds, grpc_call_credentials* call_creds,
    void* reserved);

/** --- composite credentials. */

/** Creates a composite call credentials object. */
GRPCAPI grpc_call_credentials* grpc_composite_call_credentials_create(
    grpc_call_credentials* creds1, grpc_call_credentials* creds2,
    void* reserved);

/** Creates a compute engine credentials object for connecting to Google.
   WARNING: Do NOT use this credentials to connect to a non-google service as
   this could result in an oauth2 token leak. */
GRPCAPI grpc_call_credentials* grpc_google_compute_engine_credentials_create(
    void* reserved);

GRPCAPI gpr_timespec grpc_max_auth_token_lifetime(void);

/** Creates a JWT credentials object. May return NULL if the input is invalid.
   - json_key is the JSON key string containing the client's private key.
   - token_lifetime is the lifetime of each Json Web Token (JWT) created with
     this credentials.  It should not exceed grpc_max_auth_token_lifetime or
     will be cropped to this value.  */
GRPCAPI grpc_call_credentials*
grpc_service_account_jwt_access_credentials_create(const char* json_key,
                                                   gpr_timespec token_lifetime,
                                                   void* reserved);

/** Builds External Account credentials.
 - json_string is the JSON string containing the credentials options.
 - scopes_string contains the scopes to be binded with the credentials.
   This API is used for experimental purposes for now and may change in the
 future. */
GRPCAPI grpc_call_credentials* grpc_external_account_credentials_create(
    const char* json_string, const char* scopes_string);

/** Creates an Oauth2 Refresh Token credentials object for connecting to Google.
   May return NULL if the input is invalid.
   WARNING: Do NOT use this credentials to connect to a non-google service as
   this could result in an oauth2 token leak.
   - json_refresh_token is the JSON string containing the refresh token itself
     along with a client_id and client_secret. */
GRPCAPI grpc_call_credentials* grpc_google_refresh_token_credentials_create(
    const char* json_refresh_token, void* reserved);

/** Creates an Oauth2 Access Token credentials with an access token that was
   acquired by an out of band mechanism. */
GRPCAPI grpc_call_credentials* grpc_access_token_credentials_create(
    const char* access_token, void* reserved);

/** Creates an IAM credentials object for connecting to Google. */
GRPCAPI grpc_call_credentials* grpc_google_iam_credentials_create(
    const char* authorization_token, const char* authority_selector,
    void* reserved);

/** Options for creating STS Oauth Token Exchange credentials following the IETF
   draft https://tools.ietf.org/html/draft-ietf-oauth-token-exchange-16.
   Optional fields may be set to NULL or empty string. It is the responsibility
   of the caller to ensure that the subject and actor tokens are refreshed on
   disk at the specified paths. This API is used for experimental purposes for
   now and may change in the future. */
typedef struct {
  const char* token_exchange_service_uri; /* Required. */
  const char* resource;                   /* Optional. */
  const char* audience;                   /* Optional. */
  const char* scope;                      /* Optional. */
  const char* requested_token_type;       /* Optional. */
  const char* subject_token_path;         /* Required. */
  const char* subject_token_type;         /* Required. */
  const char* actor_token_path;           /* Optional. */
  const char* actor_token_type;           /* Optional. */
} grpc_sts_credentials_options;

/** Creates an STS credentials following the STS Token Exchanged specifed in the
   IETF draft https://tools.ietf.org/html/draft-ietf-oauth-token-exchange-16.
   This API is used for experimental purposes for now and may change in the
   future. */
GRPCAPI grpc_call_credentials* grpc_sts_credentials_create(
    const grpc_sts_credentials_options* options, void* reserved);

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
    void* user_data, const grpc_metadata* creds_md, size_t num_creds_md,
    grpc_status_code status, const char* error_details);

/** Context that can be used by metadata credentials plugin in order to create
   auth related metadata. */
typedef struct {
  /** The fully qualified service url. */
  const char* service_url;

  /** The method name of the RPC being called (not fully qualified).
     The fully qualified method name can be built from the service_url:
     full_qualified_method_name = ctx->service_url + '/' + ctx->method_name. */
  const char* method_name;

  /** The auth_context of the channel which gives the server's identity. */
  const grpc_auth_context* channel_auth_context;

  /** Reserved for future use. */
  void* reserved;
} grpc_auth_metadata_context;

/** Performs a deep copy from \a from to \a to. **/
GRPCAPI void grpc_auth_metadata_context_copy(grpc_auth_metadata_context* from,
                                             grpc_auth_metadata_context* to);

/** Releases internal resources held by \a context. **/
GRPCAPI void grpc_auth_metadata_context_reset(
    grpc_auth_metadata_context* context);

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
      void* state, grpc_auth_metadata_context context,
      grpc_credentials_plugin_metadata_cb cb, void* user_data,
      grpc_metadata creds_md[GRPC_METADATA_CREDENTIALS_PLUGIN_SYNC_MAX],
      size_t* num_creds_md, grpc_status_code* status,
      const char** error_details);

  /** Implements debug string of the given plugin. This method returns an
   * allocated string that the caller needs to free using gpr_free() */
  char* (*debug_string)(void* state);

  /** Destroys the plugin state. */
  void (*destroy)(void* state);

  /** State that will be set as the first parameter of the methods above. */
  void* state;

  /** Type of credentials that this plugin is implementing. */
  const char* type;
} grpc_metadata_credentials_plugin;

/** Creates a credentials object from a plugin with a specified minimum security
 * level. */
GRPCAPI grpc_call_credentials* grpc_metadata_credentials_create_from_plugin(
    grpc_metadata_credentials_plugin plugin,
    grpc_security_level min_security_level, void* reserved);

/** --- Call specific credentials. --- */

/** Sets a credentials to a call. Can only be called on the client side before
   grpc_call_start_batch. */
GRPCAPI grpc_call_error grpc_call_set_credentials(grpc_call* call,
                                                  grpc_call_credentials* creds);

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
    void* user_data, const grpc_metadata* consumed_md, size_t num_consumed_md,
    const grpc_metadata* response_md, size_t num_response_md,
    grpc_status_code status, const char* error_details);

/** Pluggable server-side metadata processor object. */
typedef struct {
  /** The context object is read/write: it contains the properties of the
     channel peer and it is the job of the process function to augment it with
     properties derived from the passed-in metadata.
     The lifetime of these objects is guaranteed until cb is invoked. */
  void (*process)(void* state, grpc_auth_context* context,
                  const grpc_metadata* md, size_t num_md,
                  grpc_process_auth_metadata_done_cb cb, void* user_data);
  void (*destroy)(void* state);
  void* state;
} grpc_auth_metadata_processor;

GRPCAPI void grpc_server_credentials_set_auth_metadata_processor(
    grpc_server_credentials* creds, grpc_auth_metadata_processor processor);

/**
 * EXPERIMENTAL API - Subject to change
 *
 * This method creates an insecure channel credentials object.
 */
GRPCAPI grpc_channel_credentials* grpc_insecure_credentials_create();

/**
 * EXPERIMENTAL API - Subject to change
 *
 * This method creates an insecure server credentials object.
 */
GRPCAPI grpc_server_credentials* grpc_insecure_server_credentials_create();

/**
 * EXPERIMENTAL API - Subject to change
 *
 * This method creates an xDS channel credentials object.
 *
 * Creating a channel with credentials of this type indicates that the channel
 * should get credentials configuration from the xDS control plane.
 *
 * \a fallback_credentials are used if the channel target does not have the
 * 'xds:///' scheme or if the xDS control plane does not provide information on
 * how to fetch credentials dynamically. Does NOT take ownership of the \a
 * fallback_credentials. (Internally takes a ref to the object.)
 */
GRPCAPI grpc_channel_credentials* grpc_xds_credentials_create(
    grpc_channel_credentials* fallback_credentials);

/**
 * EXPERIMENTAL API - Subject to change
 *
 * This method creates an xDS server credentials object.
 *
 * \a fallback_credentials are used if the xDS control plane does not provide
 * information on how to fetch credentials dynamically.
 *
 * Does NOT take ownership of the \a fallback_credentials. (Internally takes
 * a ref to the object.)
 */
GRPCAPI grpc_server_credentials* grpc_xds_server_credentials_create(
    grpc_server_credentials* fallback_credentials);

/**
 * EXPERIMENTAL - Subject to change.
 * An opaque type that is responsible for providing authorization policies to
 * gRPC.
 */
typedef struct grpc_authorization_policy_provider
    grpc_authorization_policy_provider;

/**
 * EXPERIMENTAL - Subject to change.
 * Creates a grpc_authorization_policy_provider using gRPC authorization policy
 * from static string.
 * - authz_policy is the input gRPC authorization policy.
 * - code is the error status code on failure. On success, it equals
 *   GRPC_STATUS_OK.
 * - error_details contains details about the error if any. If the
 *   initialization is successful, it will be null. Caller must use gpr_free to
 *   destroy this string.
 */
GRPCAPI grpc_authorization_policy_provider*
grpc_authorization_policy_provider_static_data_create(
    const char* authz_policy, grpc_status_code* code,
    const char** error_details);

/**
 * EXPERIMENTAL - Subject to change.
 * Creates a grpc_authorization_policy_provider by watching for gRPC
 * authorization policy changes in filesystem.
 * - authz_policy is the file path of gRPC authorization policy.
 * - refresh_interval_sec is the amount of time the internal thread would wait
 *   before checking for file updates.
 * - code is the error status code on failure. On success, it equals
 *   GRPC_STATUS_OK.
 * - error_details contains details about the error if any. If the
 *   initialization is successful, it will be null. Caller must use gpr_free to
 *   destroy this string.
 */
GRPCAPI grpc_authorization_policy_provider*
grpc_authorization_policy_provider_file_watcher_create(
    const char* authz_policy_path, unsigned int refresh_interval_sec,
    grpc_status_code* code, const char** error_details);

/**
 * EXPERIMENTAL - Subject to change.
 * Releases grpc_authorization_policy_provider object. The creator of
 * grpc_authorization_policy_provider is responsible for its release.
 */
GRPCAPI void grpc_authorization_policy_provider_release(
    grpc_authorization_policy_provider* provider);

#ifdef __cplusplus
}
#endif

#endif /* GRPC_GRPC_SECURITY_H */

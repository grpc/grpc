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

#include <stdbool.h>

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

typedef struct grpc_call_credentials grpc_call_credentials;

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

GRPCAPI gpr_timespec grpc_max_auth_token_lifetime(void);

/** --- Call specific credentials. --- */

/** Sets a credentials to a call. Can only be called on the client side before
   grpc_call_start_batch. */
GRPCAPI grpc_call_error grpc_call_set_credentials(grpc_call* call,
                                                  grpc_call_credentials* creds);

/** --- TLS channel/server credentials ---
 * It is used for experimental purpose for now and subject to change. */

/**
 * EXPERIMENTAL API - Subject to change
 *
 * A struct provides ways to gain credential data that will be used in the TLS
 * handshake.
 */
typedef struct grpc_tls_certificate_provider grpc_tls_certificate_provider;

/**
 * EXPERIMENTAL API - Subject to change
 *
 * A struct that stores the credential data presented to the peer in handshake
 * to show local identity.
 */
typedef struct grpc_tls_identity_pairs grpc_tls_identity_pairs;

/**
 * EXPERIMENTAL API - Subject to change
 *
 * Creates a grpc_tls_identity_pairs that stores a list of identity credential
 * data, including identity private key and identity certificate chain.
 */
GRPCAPI grpc_tls_identity_pairs* grpc_tls_identity_pairs_create();

/**
 * EXPERIMENTAL API - Subject to change
 *
 * Adds a identity private key and a identity certificate chain to
 * grpc_tls_identity_pairs. This function will make an internal copy of
 * |private_key| and |cert_chain|.
 */
GRPCAPI void grpc_tls_identity_pairs_add_pair(grpc_tls_identity_pairs* pairs,
                                              const char* private_key,
                                              const char* cert_chain);

/**
 * EXPERIMENTAL API - Subject to change
 *
 * Destroys a grpc_tls_identity_pairs object. If this object is passed to a
 * provider initiation function, the ownership is transferred so this function
 * doesn't need to be called. Otherwise the creator of the
 * grpc_tls_identity_pairs object is responsible for its destruction.
 */
GRPCAPI void grpc_tls_identity_pairs_destroy(grpc_tls_identity_pairs* pairs);

/**
 * EXPERIMENTAL API - Subject to change
 *
 * Creates a grpc_tls_certificate_provider that will load credential data from
 * static string during initialization. This provider will always return the
 * same cert data for all cert names.
 * root_certificate and pem_key_cert_pairs can be nullptr, indicating the
 * corresponding credential data is not needed.
 * This function will make a copy of |root_certificate|.
 * The ownership of |pem_key_cert_pairs| is transferred.
 */
GRPCAPI grpc_tls_certificate_provider*
grpc_tls_certificate_provider_static_data_create(
    const char* root_certificate, grpc_tls_identity_pairs* pem_key_cert_pairs);

/**
 * EXPERIMENTAL API - Subject to change
 *
 * Creates a grpc_tls_certificate_provider that will watch the credential
 * changes on the file system. This provider will always return the up-to-date
 * cert data for all the cert names callers set through
 * |grpc_tls_credentials_options|. Note that this API only supports one key-cert
 * file and hence one set of identity key-cert pair, so SNI(Server Name
 * Indication) is not supported.
 * - private_key_path is the file path of the private key. This must be set if
 *   |identity_certificate_path| is set. Otherwise, it could be null if no
 *   identity credentials are needed.
 * - identity_certificate_path is the file path of the identity certificate
 *   chain. This must be set if |private_key_path| is set. Otherwise, it could
 *   be null if no identity credentials are needed.
 * - root_cert_path is the file path to the root certificate bundle. This
 *   may be null if no root certs are needed.
 * - refresh_interval_sec is the refreshing interval that we will check the
 *   files for updates.
 * It does not take ownership of parameters.
 */
GRPCAPI grpc_tls_certificate_provider*
grpc_tls_certificate_provider_file_watcher_create(
    const char* private_key_path, const char* identity_certificate_path,
    const char* root_cert_path, unsigned int refresh_interval_sec);

/**
 * EXPERIMENTAL API - Subject to change
 *
 * Releases a grpc_tls_certificate_provider object. The creator of the
 * grpc_tls_certificate_provider object is responsible for its release.
 */
GRPCAPI void grpc_tls_certificate_provider_release(
    grpc_tls_certificate_provider* provider);

/**
 * EXPERIMENTAL API - Subject to change
 *
 * The read-only request information exposed in a verification call.
 * Callers should not directly manage the ownership of it. We will make sure it
 * is always available inside verify() or cancel() call, and will destroy the
 * object at the end of custom verification.
 */
typedef struct grpc_tls_custom_verification_check_request {
  /* The target name of the server when the client initiates the connection. */
  /* This field will be nullptr if on the server side. */
  const char* target_name;
  /* The information contained in the certificate chain sent from the peer. */
  struct peer_info {
    /* The Common Name field on the peer leaf certificate. */
    const char* common_name;
    /* The list of Subject Alternative Names on the peer leaf certificate. */
    struct san_names {
      char** uri_names;
      size_t uri_names_size;
      char** dns_names;
      size_t dns_names_size;
      char** email_names;
      size_t email_names_size;
      char** ip_names;
      size_t ip_names_size;
    } san_names;
    /* The raw peer leaf certificate. */
    const char* peer_cert;
    /* The raw peer certificate chain. Note that it is not always guaranteed to
     * get the peer full chain. For more, please refer to
     * GRPC_X509_PEM_CERT_CHAIN_PROPERTY_NAME defined in file
     * grpc_security_constants.h.
     * TODO(ZhenLian): Consider fixing this in the future. */
    const char* peer_cert_full_chain;
    /* The verified root cert subject.
     * This value will only be filled if the cryptographic peer certificate
     * verification was successful */
    const char* verified_root_cert_subject;
  } peer_info;
} grpc_tls_custom_verification_check_request;

/**
 * EXPERIMENTAL API - Subject to change
 *
 * A callback function provided by gRPC as a parameter of the |verify| function
 * in grpc_tls_certificate_verifier_external. If |verify| is expected to be run
 * asynchronously, the implementer of |verify| will need to invoke this callback
 * with |callback_arg| and proper verification status at the end to bring the
 * control back to gRPC C core.
 */
typedef void (*grpc_tls_on_custom_verification_check_done_cb)(
    grpc_tls_custom_verification_check_request* request, void* callback_arg,
    grpc_status_code status, const char* error_details);

/**
 * EXPERIMENTAL API - Subject to change
 *
 * The internal verifier type that will be used inside core.
 */
typedef struct grpc_tls_certificate_verifier grpc_tls_certificate_verifier;

/**
 * EXPERIMENTAL API - Subject to change
 *
 * A struct containing all the necessary functions a custom external verifier
 * needs to implement to be able to be converted to an internal verifier.
 */
typedef struct grpc_tls_certificate_verifier_external {
  void* user_data;
  /**
   * A function pointer containing the verification logic that will be
   * performed after the TLS handshake is done. It could be processed
   * synchronously or asynchronously.
   * - If expected to be processed synchronously, the implementer should
   *   populate the verification result through |sync_status| and
   *   |sync_error_details|, and then return true.
   * - If expected to be processed asynchronously, the implementer should return
   *   false immediately, and then in the asynchronous thread invoke |callback|
   *   with the verification result. The implementer MUST NOT invoke the async
   *   |callback| in the same thread before |verify| returns, otherwise it can
   *   lead to deadlocks.
   *
   * user_data: any argument that is passed in the user_data of
   *            grpc_tls_certificate_verifier_external during construction time
   *            can be retrieved later here.
   * request: request information exposed to the function implementer.
   * callback: the callback that the function implementer needs to invoke, if
   *           return a non-zero value. It is usually invoked when the
   *           asynchronous verification is done, and serves to bring the
   *           control back to gRPC.
   * callback_arg: A pointer to the internal ExternalVerifier instance. This is
   *               mainly used as an argument in |callback|, if want to invoke
   *               |callback| in async mode.
   * sync_status: indicates if a connection should be allowed. This should only
   *              be used if the verification check is done synchronously.
   * sync_error_details: the error generated while verifying a connection. This
   *                     should only be used if the verification check is done
   *                     synchronously. the implementation must allocate the
   *                     error string via gpr_malloc() or gpr_strdup().
   * return: return 0 if |verify| is expected to be executed asynchronously,
   *         otherwise return a non-zero value.
   */
  int (*verify)(void* user_data,
                grpc_tls_custom_verification_check_request* request,
                grpc_tls_on_custom_verification_check_done_cb callback,
                void* callback_arg, grpc_status_code* sync_status,
                char** sync_error_details);
  /**
   * A function pointer that cleans up the caller-specified resources when the
   * verifier is still running but the whole connection got cancelled. This
   * could happen when the verifier is doing some async operations, and the
   * whole handshaker object got destroyed because of connection time limit is
   * reached, or any other reasons. In such cases, function implementers might
   * want to be notified, and properly clean up some resources.
   *
   * user_data: any argument that is passed in the user_data of
   *            grpc_tls_certificate_verifier_external during construction time
   *            can be retrieved later here.
   * request: request information exposed to the function implementer. It will
   *          be the same request object that was passed to verify(), and it
   *          tells the cancel() which request to cancel.
   */
  void (*cancel)(void* user_data,
                 grpc_tls_custom_verification_check_request* request);
  /**
   * A function pointer that does some additional destruction work when the
   * verifier is destroyed. This is used when the caller wants to associate some
   * objects to the lifetime of external_verifier, and destroy them when
   * external_verifier got destructed. For example, in C++, the class containing
   * user-specified callback functions should not be destroyed before
   * external_verifier, since external_verifier will invoke them while being
   * used.
   * Note that the caller MUST delete the grpc_tls_certificate_verifier_external
   * object itself in this function, otherwise it will cause memory leaks. That
   * also means the user_data has to carries at least a self pointer, for the
   * callers to later delete it in destruct().
   *
   * user_data: any argument that is passed in the user_data of
   *            grpc_tls_certificate_verifier_external during construction time
   *            can be retrieved later here.
   */
  void (*destruct)(void* user_data);
} grpc_tls_certificate_verifier_external;

/**
 * EXPERIMENTAL API - Subject to change
 *
 * Converts an external verifier to an internal verifier.
 * Note that we will not take the ownership of the external_verifier. Callers
 * will need to delete external_verifier in its own destruct function.
 */
grpc_tls_certificate_verifier* grpc_tls_certificate_verifier_external_create(
    grpc_tls_certificate_verifier_external* external_verifier);

/**
 * EXPERIMENTAL API - Subject to change
 *
 * Factory function for an internal verifier that won't perform any
 * post-handshake verification. Note: using this solely without any other
 * authentication mechanisms on the peer identity will leave your applications
 * to the MITM(Man-In-The-Middle) attacks. Users should avoid doing so in
 * production environments.
 */
grpc_tls_certificate_verifier* grpc_tls_certificate_verifier_no_op_create();

/**
 * EXPERIMENTAL API - Subject to change
 *
 * Factory function for an internal verifier that will do the default hostname
 * check.
 */
grpc_tls_certificate_verifier* grpc_tls_certificate_verifier_host_name_create();

/**
 * EXPERIMENTAL API - Subject to change
 *
 * Releases a grpc_tls_certificate_verifier object. The creator of the
 * grpc_tls_certificate_verifier object is responsible for its release.
 */
void grpc_tls_certificate_verifier_release(
    grpc_tls_certificate_verifier* verifier);

/**
 * EXPERIMENTAL API - Subject to change
 *
 * Performs the verification logic of an internal verifier.
 * This is typically used when composing the internal verifiers as part of the
 * custom verification.
 * If |grpc_tls_certificate_verifier_verify| returns true, inspect the
 * verification result through request->status and request->error_details.
 * Otherwise, inspect through the parameter of |callback|.
 */
int grpc_tls_certificate_verifier_verify(
    grpc_tls_certificate_verifier* verifier,
    grpc_tls_custom_verification_check_request* request,
    grpc_tls_on_custom_verification_check_done_cb callback, void* callback_arg,
    grpc_status_code* sync_status, char** sync_error_details);

/**
 * EXPERIMENTAL API - Subject to change
 *
 * Performs the cancellation logic of an internal verifier.
 * This is typically used when composing the internal verifiers as part of the
 * custom verification.
 */
void grpc_tls_certificate_verifier_cancel(
    grpc_tls_certificate_verifier* verifier,
    grpc_tls_custom_verification_check_request* request);

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

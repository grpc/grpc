// Copyright 2022 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef GRPC_CHANNEL_CREDENTIALS_TLS_H
#define GRPC_CHANNEL_CREDENTIALS_TLS_H

#include <grpc/support/port_platform.h>

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>

#ifdef __cplusplus
extern "C" {
#endif

/** --- TLS channel/server credentials ---
 * It is used for experimental purpose for now and subject to change. */

/**
 * EXPERIMENTAL API - Subject to change
 *
 * A struct that can be specified by callers to configure underlying TLS
 * behaviors.
 */
typedef struct grpc_tls_credentials_options grpc_tls_credentials_options;

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
 * Creates an grpc_tls_credentials_options.
 */
GRPCAPI grpc_tls_credentials_options* grpc_tls_credentials_options_create(void);

/**
 * EXPERIMENTAL API - Subject to change
 *
 * Sets the credential provider in the options.
 * The |options| will implicitly take a new ref to the |provider|.
 */
GRPCAPI void grpc_tls_credentials_options_set_certificate_provider(
    grpc_tls_credentials_options* options,
    grpc_tls_certificate_provider* provider);

/**
 * EXPERIMENTAL API - Subject to change
 *
 * If set, gRPC stack will keep watching the root certificates with
 * name |root_cert_name|.
 * If this is not set on the client side, we will use the root certificates
 * stored in the default system location, since client side must provide root
 * certificates in TLS.
 * If this is not set on the server side, we will not watch any root certificate
 * updates, and assume no root certificates needed for the server(single-side
 * TLS). Default root certs on the server side is not supported.
 */
GRPCAPI void grpc_tls_credentials_options_watch_root_certs(
    grpc_tls_credentials_options* options);

/**
 * EXPERIMENTAL API - Subject to change
 *
 * Sets the name of the root certificates being watched.
 * If not set, We will use a default empty string as the root certificate name.
 */
GRPCAPI void grpc_tls_credentials_options_set_root_cert_name(
    grpc_tls_credentials_options* options, const char* root_cert_name);

/**
 * EXPERIMENTAL API - Subject to change
 *
 * If set, gRPC stack will keep watching the identity key-cert pairs
 * with name |identity_cert_name|.
 * This is required on the server side, and optional on the client side.
 */
GRPCAPI void grpc_tls_credentials_options_watch_identity_key_cert_pairs(
    grpc_tls_credentials_options* options);

/**
 * EXPERIMENTAL API - Subject to change
 *
 * Sets the name of the identity certificates being watched.
 * If not set, We will use a default empty string as the identity certificate
 * name.
 */
GRPCAPI void grpc_tls_credentials_options_set_identity_cert_name(
    grpc_tls_credentials_options* options, const char* identity_cert_name);

/**
 * EXPERIMENTAL API - Subject to change
 *
 * Sets the options of whether to request and/or verify client certs. This shall
 * only be called on the server side.
 */
GRPCAPI void grpc_tls_credentials_options_set_cert_request_type(
    grpc_tls_credentials_options* options,
    grpc_ssl_client_certificate_request_type type);
/**
 * EXPERIMENTAL API - Subject to change
 *
 * If set, gRPC will read all hashed x.509 CRL files in the directory and
 * enforce the CRL files on all TLS handshakes. Only supported for OpenSSL
 * version > 1.1.
 * It is used for experimental purpose for now and subject to change.
 */
GRPCAPI void grpc_tls_credentials_options_set_crl_directory(
    grpc_tls_credentials_options* options, const char* crl_directory);

/**
 * EXPERIMENTAL API - Subject to change
 *
 * Sets the options of whether to verify server certs on the client side.
 * Passing in a non-zero value indicates verifying the certs.
 */
GRPCAPI void grpc_tls_credentials_options_set_verify_server_cert(
    grpc_tls_credentials_options* options, int verify_server_cert);

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
 * Sets the verifier in options. The |options| will implicitly take a new ref to
 * the |verifier|. If not set on the client side, we will verify server's
 * certificates, and check the default hostname. If not set on the server side,
 * we will verify client's certificates.
 */
void grpc_tls_credentials_options_set_certificate_verifier(
    grpc_tls_credentials_options* options,
    grpc_tls_certificate_verifier* verifier);

/**
 * EXPERIMENTAL API - Subject to change
 *
 * Sets the options of whether to check the hostname of the peer on a per-call
 * basis. This is usually used in a combination with virtual hosting at the
 * client side, where each individual call on a channel can have a different
 * host associated with it.
 * This check is intended to verify that the host specified for the individual
 * call is covered by the cert that the peer presented.
 * The default is a non-zero value, which indicates performing such checks.
 */
GRPCAPI void grpc_tls_credentials_options_set_check_call_host(
    grpc_tls_credentials_options* options, int check_call_host);

/** --- TLS session key logging. ---
 * Experimental API to control tls session key logging. Tls session key logging
 * is expected to be used only for debugging purposes and never in production.
 * Tls session key logging is only enabled when:
 *  At least one grpc_tls_credentials_options object is assigned a tls session
 *  key logging file path using the API specified below.
 */

/**
 * EXPERIMENTAL API - Subject to change.
 * Configures a grpc_tls_credentials_options object with tls session key
 * logging capability. TLS channels using these credentials have tls session
 * key logging enabled.
 * - options is the grpc_tls_credentials_options object
 * - path is a string pointing to the location where TLS session keys would be
 *   stored.
 */
GRPCAPI void grpc_tls_credentials_options_set_tls_session_key_log_file_path(
    grpc_tls_credentials_options* options, const char* path);

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
 * EXPERIMENTAL API - Subject to change
 *
 * Creates a TLS channel credential object based on the
 * grpc_tls_credentials_options specified by callers. The
 * grpc_channel_credentials will take the ownership of the |options|. The
 * security level of the resulting connection is GRPC_PRIVACY_AND_INTEGRITY.
 */
grpc_channel_credentials* grpc_tls_credentials_create(
    grpc_tls_credentials_options* options);

/**
 * EXPERIMENTAL API - Subject to change
 *
 * Creates a TLS server credential object based on the
 * grpc_tls_credentials_options specified by callers. The
 * grpc_server_credentials will take the ownership of the |options|.
 */
grpc_server_credentials* grpc_tls_server_credentials_create(
    grpc_tls_credentials_options* options);

#ifdef __cplusplus
}
#endif

#endif /* GRPC_CHANNEL_CREDENTIALS_TLS_H */

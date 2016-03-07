/*
 *
 * Copyright 2015-2016, Google Inc.
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

#ifndef GRPC_INTERNAL_CORE_SECURITY_CREDENTIALS_H
#define GRPC_INTERNAL_CORE_SECURITY_CREDENTIALS_H

#include "src/core/transport/metadata_batch.h"
#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/sync.h>

#include "src/core/httpcli/httpcli.h"
#include "src/core/security/json_token.h"
#include "src/core/security/security_connector.h"

struct grpc_httpcli_response;

/* --- Constants. --- */

typedef enum {
  GRPC_CREDENTIALS_OK = 0,
  GRPC_CREDENTIALS_ERROR
} grpc_credentials_status;

#define GRPC_FAKE_TRANSPORT_SECURITY_TYPE "fake"

#define GRPC_CHANNEL_CREDENTIALS_TYPE_SSL "Ssl"
#define GRPC_CHANNEL_CREDENTIALS_TYPE_FAKE_TRANSPORT_SECURITY \
  "FakeTransportSecurity"

#define GRPC_CALL_CREDENTIALS_TYPE_OAUTH2 "Oauth2"
#define GRPC_CALL_CREDENTIALS_TYPE_JWT "Jwt"
#define GRPC_CALL_CREDENTIALS_TYPE_IAM "Iam"
#define GRPC_CALL_CREDENTIALS_TYPE_COMPOSITE "Composite"

#define GRPC_AUTHORIZATION_METADATA_KEY "authorization"
#define GRPC_IAM_AUTHORIZATION_TOKEN_METADATA_KEY \
  "x-goog-iam-authorization-token"
#define GRPC_IAM_AUTHORITY_SELECTOR_METADATA_KEY "x-goog-iam-authority-selector"

#define GRPC_GOOGLE_CLOUD_SDK_CONFIG_DIRECTORY "gcloud"
#define GRPC_GOOGLE_WELL_KNOWN_CREDENTIALS_FILE \
  "application_default_credentials.json"

#define GRPC_SECURE_TOKEN_REFRESH_THRESHOLD_SECS 60

#define GRPC_COMPUTE_ENGINE_METADATA_HOST "metadata"
#define GRPC_COMPUTE_ENGINE_METADATA_TOKEN_PATH \
  "/computeMetadata/v1/instance/service-accounts/default/token"

#define GRPC_GOOGLE_OAUTH2_SERVICE_HOST "www.googleapis.com"
#define GRPC_GOOGLE_OAUTH2_SERVICE_TOKEN_PATH "/oauth2/v3/token"

#define GRPC_SERVICE_ACCOUNT_POST_BODY_PREFIX                         \
  "grant_type=urn%3Aietf%3Aparams%3Aoauth%3Agrant-type%3Ajwt-bearer&" \
  "assertion="

#define GRPC_REFRESH_TOKEN_POST_BODY_FORMAT_STRING \
  "client_id=%s&client_secret=%s&refresh_token=%s&grant_type=refresh_token"

/* --- Google utils --- */

/* It is the caller's responsibility to gpr_free the result if not NULL. */
char *grpc_get_well_known_google_credentials_file_path(void);

/* Implementation function for the different platforms. */
char *grpc_get_well_known_google_credentials_file_path_impl(void);

/* Override for testing only. Not thread-safe */
typedef char *(*grpc_well_known_credentials_path_getter)(void);
void grpc_override_well_known_credentials_path_getter(
    grpc_well_known_credentials_path_getter getter);

/* --- grpc_channel_credentials. --- */

typedef struct {
  void (*destruct)(grpc_channel_credentials *c);

  grpc_security_status (*create_security_connector)(
      grpc_channel_credentials *c, grpc_call_credentials *call_creds,
      const char *target, const grpc_channel_args *args,
      grpc_channel_security_connector **sc, grpc_channel_args **new_args);
} grpc_channel_credentials_vtable;

struct grpc_channel_credentials {
  const grpc_channel_credentials_vtable *vtable;
  const char *type;
  gpr_refcount refcount;
};

grpc_channel_credentials *grpc_channel_credentials_ref(
    grpc_channel_credentials *creds);
void grpc_channel_credentials_unref(grpc_channel_credentials *creds);

/* Creates a security connector for the channel. May also create new channel
   args for the channel to be used in place of the passed in const args if
   returned non NULL. In that case the caller is responsible for destroying
   new_args after channel creation. */
grpc_security_status grpc_channel_credentials_create_security_connector(
    grpc_channel_credentials *creds, const char *target,
    const grpc_channel_args *args, grpc_channel_security_connector **sc,
    grpc_channel_args **new_args);

/* --- grpc_credentials_md. --- */

typedef struct {
  gpr_slice key;
  gpr_slice value;
} grpc_credentials_md;

typedef struct {
  grpc_credentials_md *entries;
  size_t num_entries;
  size_t allocated;
  gpr_refcount refcount;
} grpc_credentials_md_store;

grpc_credentials_md_store *grpc_credentials_md_store_create(
    size_t initial_capacity);

/* Will ref key and value. */
void grpc_credentials_md_store_add(grpc_credentials_md_store *store,
                                   gpr_slice key, gpr_slice value);
void grpc_credentials_md_store_add_cstrings(grpc_credentials_md_store *store,
                                            const char *key, const char *value);
grpc_credentials_md_store *grpc_credentials_md_store_ref(
    grpc_credentials_md_store *store);
void grpc_credentials_md_store_unref(grpc_credentials_md_store *store);

/* --- grpc_call_credentials. --- */

typedef void (*grpc_credentials_metadata_cb)(grpc_exec_ctx *exec_ctx,
                                             void *user_data,
                                             grpc_credentials_md *md_elems,
                                             size_t num_md,
                                             grpc_credentials_status status);

typedef struct {
  void (*destruct)(grpc_call_credentials *c);
  void (*get_request_metadata)(grpc_exec_ctx *exec_ctx,
                               grpc_call_credentials *c, grpc_pollset *pollset,
                               grpc_auth_metadata_context context,
                               grpc_credentials_metadata_cb cb,
                               void *user_data);
} grpc_call_credentials_vtable;

struct grpc_call_credentials {
  const grpc_call_credentials_vtable *vtable;
  const char *type;
  gpr_refcount refcount;
};

grpc_call_credentials *grpc_call_credentials_ref(grpc_call_credentials *creds);
void grpc_call_credentials_unref(grpc_call_credentials *creds);
void grpc_call_credentials_get_request_metadata(
    grpc_exec_ctx *exec_ctx, grpc_call_credentials *creds,
    grpc_pollset *pollset, grpc_auth_metadata_context context,
    grpc_credentials_metadata_cb cb, void *user_data);

typedef struct {
  grpc_call_credentials **creds_array;
  size_t num_creds;
} grpc_call_credentials_array;

const grpc_call_credentials_array *
grpc_composite_call_credentials_get_credentials(
    grpc_call_credentials *composite_creds);

/* Returns creds if creds is of the specified type or the inner creds of the
   specified type (if found), if the creds is of type COMPOSITE.
   If composite_creds is not NULL, *composite_creds will point to creds if of
   type COMPOSITE in case of success. */
grpc_call_credentials *grpc_credentials_contains_type(
    grpc_call_credentials *creds, const char *type,
    grpc_call_credentials **composite_creds);

/* Exposed for testing only. */
grpc_credentials_status
grpc_oauth2_token_fetcher_credentials_parse_server_response(
    const struct grpc_httpcli_response *response,
    grpc_credentials_md_store **token_md, gpr_timespec *token_lifetime);

void grpc_flush_cached_google_default_credentials(void);

/* Metadata-only credentials with the specified key and value where
   asynchronicity can be simulated for testing. */
grpc_call_credentials *grpc_md_only_test_credentials_create(
    const char *md_key, const char *md_value, int is_async);

/* Private constructor for jwt credentials from an already parsed json key.
   Takes ownership of the key. */
grpc_call_credentials *
grpc_service_account_jwt_access_credentials_create_from_auth_json_key(
    grpc_auth_json_key key, gpr_timespec token_lifetime);

/* Private constructor for refresh token credentials from an already parsed
   refresh token. Takes ownership of the refresh token. */
grpc_call_credentials *
grpc_refresh_token_credentials_create_from_auth_refresh_token(
    grpc_auth_refresh_token token);

/* --- grpc_server_credentials. --- */

typedef struct {
  void (*destruct)(grpc_server_credentials *c);
  grpc_security_status (*create_security_connector)(
      grpc_server_credentials *c, grpc_server_security_connector **sc);
} grpc_server_credentials_vtable;

struct grpc_server_credentials {
  const grpc_server_credentials_vtable *vtable;
  const char *type;
  gpr_refcount refcount;
  grpc_auth_metadata_processor processor;
};

grpc_security_status grpc_server_credentials_create_security_connector(
    grpc_server_credentials *creds, grpc_server_security_connector **sc);

grpc_server_credentials *grpc_server_credentials_ref(
    grpc_server_credentials *creds);

void grpc_server_credentials_unref(grpc_server_credentials *creds);

#define GRPC_SERVER_CREDENTIALS_ARG "grpc.server_credentials"

grpc_arg grpc_server_credentials_to_arg(grpc_server_credentials *c);
grpc_server_credentials *grpc_server_credentials_from_arg(const grpc_arg *arg);
grpc_server_credentials *grpc_find_server_credentials_in_args(
    const grpc_channel_args *args);

/* -- Fake transport security credentials. -- */

/* Creates a fake transport security credentials object for testing. */
grpc_channel_credentials *grpc_fake_transport_security_credentials_create(void);
/* Creates a fake server transport security credentials object for testing. */
grpc_server_credentials *grpc_fake_transport_security_server_credentials_create(
    void);

/* -- Ssl credentials. -- */

typedef struct {
  grpc_channel_credentials base;
  grpc_ssl_config config;
} grpc_ssl_credentials;

typedef struct {
  grpc_server_credentials base;
  grpc_ssl_server_config config;
} grpc_ssl_server_credentials;

/* -- Channel composite credentials. -- */

typedef struct {
  grpc_channel_credentials base;
  grpc_channel_credentials *inner_creds;
  grpc_call_credentials *call_creds;
} grpc_composite_channel_credentials;

/* -- Jwt credentials -- */

typedef struct {
  grpc_call_credentials base;

  /* Have a simple cache for now with just 1 entry. We could have a map based on
     the service_url for a more sophisticated one. */
  gpr_mu cache_mu;
  struct {
    grpc_credentials_md_store *jwt_md;
    char *service_url;
    gpr_timespec jwt_expiration;
  } cached;

  grpc_auth_json_key key;
  gpr_timespec jwt_lifetime;
} grpc_service_account_jwt_access_credentials;

/* -- Oauth2TokenFetcher credentials --

   This object is a base for credentials that need to acquire an oauth2 token
   from an http service. */

typedef struct grpc_credentials_metadata_request
    grpc_credentials_metadata_request;

typedef void (*grpc_fetch_oauth2_func)(grpc_exec_ctx *exec_ctx,
                                       grpc_credentials_metadata_request *req,
                                       grpc_httpcli_context *http_context,
                                       grpc_pollset *pollset,
                                       grpc_httpcli_response_cb response_cb,
                                       gpr_timespec deadline);

typedef struct {
  grpc_call_credentials base;
  gpr_mu mu;
  grpc_credentials_md_store *access_token_md;
  gpr_timespec token_expiration;
  grpc_httpcli_context httpcli_context;
  grpc_fetch_oauth2_func fetch_func;
} grpc_oauth2_token_fetcher_credentials;

/* -- GoogleRefreshToken credentials. -- */

typedef struct {
  grpc_oauth2_token_fetcher_credentials base;
  grpc_auth_refresh_token refresh_token;
} grpc_google_refresh_token_credentials;

/* -- Oauth2 Access Token credentials. -- */

typedef struct {
  grpc_call_credentials base;
  grpc_credentials_md_store *access_token_md;
} grpc_access_token_credentials;

/* --  Metadata-only Test credentials. -- */

typedef struct {
  grpc_call_credentials base;
  grpc_credentials_md_store *md_store;
  int is_async;
} grpc_md_only_test_credentials;

/* -- GoogleIAM credentials. -- */

typedef struct {
  grpc_call_credentials base;
  grpc_credentials_md_store *iam_md;
} grpc_google_iam_credentials;

/* -- Composite credentials. -- */

typedef struct {
  grpc_call_credentials base;
  grpc_call_credentials_array inner;
} grpc_composite_call_credentials;

/* -- Plugin credentials. -- */

typedef struct {
  grpc_call_credentials base;
  grpc_metadata_credentials_plugin plugin;
  grpc_credentials_md_store *plugin_md;
} grpc_plugin_credentials;

#endif /* GRPC_INTERNAL_CORE_SECURITY_CREDENTIALS_H */

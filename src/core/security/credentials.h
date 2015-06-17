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

#ifndef GRPC_INTERNAL_CORE_SECURITY_CREDENTIALS_H
#define GRPC_INTERNAL_CORE_SECURITY_CREDENTIALS_H

#include "src/core/transport/stream_op.h"
#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/sync.h>

#include "src/core/security/security_connector.h"

struct grpc_httpcli_response;

/* --- Constants. --- */

typedef enum {
  GRPC_CREDENTIALS_OK = 0,
  GRPC_CREDENTIALS_ERROR
} grpc_credentials_status;

#define GRPC_CREDENTIALS_TYPE_SSL "Ssl"
#define GRPC_CREDENTIALS_TYPE_OAUTH2 "Oauth2"
#define GRPC_CREDENTIALS_TYPE_JWT "Jwt"
#define GRPC_CREDENTIALS_TYPE_IAM "Iam"
#define GRPC_CREDENTIALS_TYPE_COMPOSITE "Composite"
#define GRPC_CREDENTIALS_TYPE_FAKE_TRANSPORT_SECURITY "FakeTransportSecurity"

#define GRPC_AUTHORIZATION_METADATA_KEY "Authorization"
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

/* --- grpc_credentials. --- */

/* It is the caller's responsibility to gpr_free the result if not NULL. */
char *grpc_get_well_known_google_credentials_file_path(void);

typedef void (*grpc_credentials_metadata_cb)(void *user_data,
                                             grpc_credentials_md *md_elems,
                                             size_t num_md,
                                             grpc_credentials_status status);

typedef struct {
  void (*destroy)(grpc_credentials *c);
  int (*has_request_metadata)(const grpc_credentials *c);
  int (*has_request_metadata_only)(const grpc_credentials *c);
  void (*get_request_metadata)(grpc_credentials *c, grpc_pollset *pollset,
                               const char *service_url,
                               grpc_credentials_metadata_cb cb,
                               void *user_data);
  grpc_security_status (*create_security_connector)(
      grpc_credentials *c, const char *target, const grpc_channel_args *args,
      grpc_credentials *request_metadata_creds,
      grpc_channel_security_connector **sc, grpc_channel_args **new_args);
} grpc_credentials_vtable;

struct grpc_credentials {
  const grpc_credentials_vtable *vtable;
  const char *type;
  gpr_refcount refcount;
};

grpc_credentials *grpc_credentials_ref(grpc_credentials *creds);
void grpc_credentials_unref(grpc_credentials *creds);
int grpc_credentials_has_request_metadata(grpc_credentials *creds);
int grpc_credentials_has_request_metadata_only(grpc_credentials *creds);
void grpc_credentials_get_request_metadata(grpc_credentials *creds,
                                           grpc_pollset *pollset,
                                           const char *service_url,
                                           grpc_credentials_metadata_cb cb,
                                           void *user_data);

/* Creates a security connector for the channel. May also create new channel
   args for the channel to be used in place of the passed in const args if
   returned non NULL. In that case the caller is responsible for destroying
   new_args after channel creation. */
grpc_security_status grpc_credentials_create_security_connector(
    grpc_credentials *creds, const char *target, const grpc_channel_args *args,
    grpc_credentials *request_metadata_creds,
    grpc_channel_security_connector **sc, grpc_channel_args **new_args);

typedef struct {
  grpc_credentials **creds_array;
  size_t num_creds;
} grpc_credentials_array;

const grpc_credentials_array *grpc_composite_credentials_get_credentials(
    grpc_credentials *composite_creds);

/* Returns creds if creds is of the specified type or the inner creds of the
   specified type (if found), if the creds is of type COMPOSITE.
   If composite_creds is not NULL, *composite_creds will point to creds if of
   type COMPOSITE in case of success. */
grpc_credentials *grpc_credentials_contains_type(
    grpc_credentials *creds, const char *type,
    grpc_credentials **composite_creds);

/* Exposed for testing only. */
grpc_credentials_status
grpc_oauth2_token_fetcher_credentials_parse_server_response(
    const struct grpc_httpcli_response *response,
    grpc_credentials_md_store **token_md, gpr_timespec *token_lifetime);

/* Simulates an oauth2 token fetch with the specified value for testing. */
grpc_credentials *grpc_fake_oauth2_credentials_create(
    const char *token_md_value, int is_async);

/* --- grpc_server_credentials. --- */

typedef struct {
  void (*destroy)(grpc_server_credentials *c);
  grpc_security_status (*create_security_connector)(
      grpc_server_credentials *c, grpc_security_connector **sc);
} grpc_server_credentials_vtable;

struct grpc_server_credentials {
  const grpc_server_credentials_vtable *vtable;
  const char *type;
};

grpc_security_status grpc_server_credentials_create_security_connector(
    grpc_server_credentials *creds, grpc_security_connector **sc);

#endif /* GRPC_INTERNAL_CORE_SECURITY_CREDENTIALS_H */

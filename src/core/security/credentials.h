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

/* --- grpc_credentials. --- */

/* It is the caller's responsibility to gpr_free the result if not NULL. */
char *grpc_get_well_known_google_credentials_file_path(void);

typedef void (*grpc_credentials_metadata_cb)(void *user_data,
                                             grpc_mdelem **md_elems,
                                             size_t num_md,
                                             grpc_credentials_status status);

typedef struct {
  void (*destroy)(grpc_credentials *c);
  int (*has_request_metadata)(const grpc_credentials *c);
  int (*has_request_metadata_only)(const grpc_credentials *c);
  void (*get_request_metadata)(grpc_credentials *c,
                               const char *service_url,
                               grpc_credentials_metadata_cb cb,
                               void *user_data);
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
                                           const char *service_url,
                                           grpc_credentials_metadata_cb cb,
                                           void *user_data);
typedef struct {
  unsigned char *pem_private_key;
  size_t pem_private_key_size;
  unsigned char *pem_cert_chain;
  size_t pem_cert_chain_size;
  unsigned char *pem_root_certs;
  size_t pem_root_certs_size;
} grpc_ssl_config;

const grpc_ssl_config *grpc_ssl_credentials_get_config(
    const grpc_credentials *ssl_creds);

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
        const struct grpc_httpcli_response *response, grpc_mdctx *ctx,
        grpc_mdelem **token_elem, gpr_timespec *token_lifetime);

/* Simulates an oauth2 token fetch with the specified value for testing. */
grpc_credentials *grpc_fake_oauth2_credentials_create(
    const char *token_md_value, int is_async);

/* --- grpc_server_credentials. --- */

typedef struct {
  void (*destroy)(grpc_server_credentials *c);
} grpc_server_credentials_vtable;

struct grpc_server_credentials {
  const grpc_server_credentials_vtable *vtable;
  const char *type;
};

typedef struct {
  unsigned char **pem_private_keys;
  size_t *pem_private_keys_sizes;
  unsigned char **pem_cert_chains;
  size_t *pem_cert_chains_sizes;
  size_t num_key_cert_pairs;
  unsigned char *pem_root_certs;
  size_t pem_root_certs_size;
} grpc_ssl_server_config;

const grpc_ssl_server_config *grpc_ssl_server_credentials_get_config(
    const grpc_server_credentials *ssl_creds);

#endif  /* GRPC_INTERNAL_CORE_SECURITY_CREDENTIALS_H */

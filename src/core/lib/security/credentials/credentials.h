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

#ifndef GRPC_CORE_LIB_SECURITY_CREDENTIALS_CREDENTIALS_H
#define GRPC_CORE_LIB_SECURITY_CREDENTIALS_CREDENTIALS_H

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/sync.h>
#include "src/core/lib/transport/metadata_batch.h"

#include "src/core/lib/http/httpcli.h"
#include "src/core/lib/http/parser.h"
#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/security/transport/security_connector.h"

struct grpc_http_response;

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

#define GRPC_SECURE_TOKEN_REFRESH_THRESHOLD_SECS 60

#define GRPC_COMPUTE_ENGINE_METADATA_HOST "metadata.google.internal"
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

#define GRPC_ARG_CHANNEL_CREDENTIALS "grpc.channel_credentials"

typedef struct {
  void (*destruct)(grpc_exec_ctx *exec_ctx, grpc_channel_credentials *c);

  grpc_security_status (*create_security_connector)(
      grpc_exec_ctx *exec_ctx, grpc_channel_credentials *c,
      grpc_call_credentials *call_creds, const char *target,
      const grpc_channel_args *args, grpc_channel_security_connector **sc,
      grpc_channel_args **new_args);

  grpc_channel_credentials *(*duplicate_without_call_credentials)(
      grpc_channel_credentials *c);
} grpc_channel_credentials_vtable;

struct grpc_channel_credentials {
  const grpc_channel_credentials_vtable *vtable;
  const char *type;
  gpr_refcount refcount;
};

grpc_channel_credentials *grpc_channel_credentials_ref(
    grpc_channel_credentials *creds);
void grpc_channel_credentials_unref(grpc_exec_ctx *exec_ctx,
                                    grpc_channel_credentials *creds);

/* Creates a security connector for the channel. May also create new channel
   args for the channel to be used in place of the passed in const args if
   returned non NULL. In that case the caller is responsible for destroying
   new_args after channel creation. */
grpc_security_status grpc_channel_credentials_create_security_connector(
    grpc_exec_ctx *exec_ctx, grpc_channel_credentials *creds,
    const char *target, const grpc_channel_args *args,
    grpc_channel_security_connector **sc, grpc_channel_args **new_args);

/* Creates a version of the channel credentials without any attached call
   credentials. This can be used in order to open a channel to a non-trusted
   gRPC load balancer. */
grpc_channel_credentials *
grpc_channel_credentials_duplicate_without_call_credentials(
    grpc_channel_credentials *creds);

/* Util to encapsulate the channel credentials in a channel arg. */
grpc_arg grpc_channel_credentials_to_arg(grpc_channel_credentials *credentials);

/* Util to get the channel credentials from a channel arg. */
grpc_channel_credentials *grpc_channel_credentials_from_arg(
    const grpc_arg *arg);

/* Util to find the channel credentials from channel args. */
grpc_channel_credentials *grpc_channel_credentials_find_in_args(
    const grpc_channel_args *args);

/* --- grpc_credentials_mdelem_array. --- */

typedef struct {
  grpc_mdelem *md;
  size_t size;
} grpc_credentials_mdelem_array;

/// Takes a new ref to \a md.
void grpc_credentials_mdelem_array_add(grpc_credentials_mdelem_array *list,
                                       grpc_mdelem md);

/// Appends all elements from \a src to \a dst, taking a new ref to each one.
void grpc_credentials_mdelem_array_append(grpc_credentials_mdelem_array *dst,
                                          grpc_credentials_mdelem_array *src);

void grpc_credentials_mdelem_array_destroy(grpc_exec_ctx *exec_ctx,
                                           grpc_credentials_mdelem_array *list);

/* --- grpc_call_credentials. --- */

typedef struct {
  void (*destruct)(grpc_exec_ctx *exec_ctx, grpc_call_credentials *c);
  bool (*get_request_metadata)(grpc_exec_ctx *exec_ctx,
                               grpc_call_credentials *c,
                               grpc_polling_entity *pollent,
                               grpc_auth_metadata_context context,
                               grpc_credentials_mdelem_array *md_array,
                               grpc_closure *on_request_metadata,
                               grpc_error **error);
  void (*cancel_get_request_metadata)(grpc_exec_ctx *exec_ctx,
                                      grpc_call_credentials *c,
                                      grpc_credentials_mdelem_array *md_array,
                                      grpc_error *error);
} grpc_call_credentials_vtable;

struct grpc_call_credentials {
  const grpc_call_credentials_vtable *vtable;
  const char *type;
  gpr_refcount refcount;
};

grpc_call_credentials *grpc_call_credentials_ref(grpc_call_credentials *creds);
void grpc_call_credentials_unref(grpc_exec_ctx *exec_ctx,
                                 grpc_call_credentials *creds);

/// Returns true if completed synchronously, in which case \a error will
/// be set to indicate the result.  Otherwise, \a on_request_metadata will
/// be invoked asynchronously when complete.  \a md_array will be populated
/// with the resulting metadata once complete.
bool grpc_call_credentials_get_request_metadata(
    grpc_exec_ctx *exec_ctx, grpc_call_credentials *creds,
    grpc_polling_entity *pollent, grpc_auth_metadata_context context,
    grpc_credentials_mdelem_array *md_array, grpc_closure *on_request_metadata,
    grpc_error **error);

/// Cancels a pending asynchronous operation started by
/// grpc_call_credentials_get_request_metadata() with the corresponding
/// value of \a md_array.
void grpc_call_credentials_cancel_get_request_metadata(
    grpc_exec_ctx *exec_ctx, grpc_call_credentials *c,
    grpc_credentials_mdelem_array *md_array, grpc_error *error);

/* Metadata-only credentials with the specified key and value where
   asynchronicity can be simulated for testing. */
grpc_call_credentials *grpc_md_only_test_credentials_create(
    grpc_exec_ctx *exec_ctx, const char *md_key, const char *md_value,
    bool is_async);

/* --- grpc_server_credentials. --- */

typedef struct {
  void (*destruct)(grpc_exec_ctx *exec_ctx, grpc_server_credentials *c);
  grpc_security_status (*create_security_connector)(
      grpc_exec_ctx *exec_ctx, grpc_server_credentials *c,
      grpc_server_security_connector **sc);
} grpc_server_credentials_vtable;

struct grpc_server_credentials {
  const grpc_server_credentials_vtable *vtable;
  const char *type;
  gpr_refcount refcount;
  grpc_auth_metadata_processor processor;
};

grpc_security_status grpc_server_credentials_create_security_connector(
    grpc_exec_ctx *exec_ctx, grpc_server_credentials *creds,
    grpc_server_security_connector **sc);

grpc_server_credentials *grpc_server_credentials_ref(
    grpc_server_credentials *creds);

void grpc_server_credentials_unref(grpc_exec_ctx *exec_ctx,
                                   grpc_server_credentials *creds);

#define GRPC_SERVER_CREDENTIALS_ARG "grpc.server_credentials"

grpc_arg grpc_server_credentials_to_arg(grpc_server_credentials *c);
grpc_server_credentials *grpc_server_credentials_from_arg(const grpc_arg *arg);
grpc_server_credentials *grpc_find_server_credentials_in_args(
    const grpc_channel_args *args);

/* -- Credentials Metadata Request. -- */

typedef struct {
  grpc_call_credentials *creds;
  grpc_http_response response;
} grpc_credentials_metadata_request;

grpc_credentials_metadata_request *grpc_credentials_metadata_request_create(
    grpc_call_credentials *creds);

void grpc_credentials_metadata_request_destroy(
    grpc_exec_ctx *exec_ctx, grpc_credentials_metadata_request *r);

#endif /* GRPC_CORE_LIB_SECURITY_CREDENTIALS_CREDENTIALS_H */

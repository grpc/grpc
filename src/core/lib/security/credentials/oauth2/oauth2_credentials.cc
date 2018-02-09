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

#include "src/core/lib/security/credentials/oauth2/oauth2_credentials.h"

#include <string.h>

#include "src/core/lib/security/util/json_util.h"
#include "src/core/lib/surface/api_trace.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

//
// Auth Refresh Token.
//

int grpc_auth_refresh_token_is_valid(
    const grpc_auth_refresh_token* refresh_token) {
  return (refresh_token != nullptr) &&
         strcmp(refresh_token->type, GRPC_AUTH_JSON_TYPE_INVALID);
}

grpc_auth_refresh_token grpc_auth_refresh_token_create_from_json(
    const grpc_json* json) {
  grpc_auth_refresh_token result;
  const char* prop_value;
  int success = 0;

  memset(&result, 0, sizeof(grpc_auth_refresh_token));
  result.type = GRPC_AUTH_JSON_TYPE_INVALID;
  if (json == nullptr) {
    gpr_log(GPR_ERROR, "Invalid json.");
    goto end;
  }

  prop_value = grpc_json_get_string_property(json, "type");
  if (prop_value == nullptr ||
      strcmp(prop_value, GRPC_AUTH_JSON_TYPE_AUTHORIZED_USER)) {
    goto end;
  }
  result.type = GRPC_AUTH_JSON_TYPE_AUTHORIZED_USER;

  if (!grpc_copy_json_string_property(json, "client_secret",
                                      &result.client_secret) ||
      !grpc_copy_json_string_property(json, "client_id", &result.client_id) ||
      !grpc_copy_json_string_property(json, "refresh_token",
                                      &result.refresh_token)) {
    goto end;
  }
  success = 1;

end:
  if (!success) grpc_auth_refresh_token_destruct(&result);
  return result;
}

grpc_auth_refresh_token grpc_auth_refresh_token_create_from_string(
    const char* json_string) {
  char* scratchpad = gpr_strdup(json_string);
  grpc_json* json = grpc_json_parse_string(scratchpad);
  grpc_auth_refresh_token result =
      grpc_auth_refresh_token_create_from_json(json);
  if (json != nullptr) grpc_json_destroy(json);
  gpr_free(scratchpad);
  return result;
}

void grpc_auth_refresh_token_destruct(grpc_auth_refresh_token* refresh_token) {
  if (refresh_token == nullptr) return;
  refresh_token->type = GRPC_AUTH_JSON_TYPE_INVALID;
  if (refresh_token->client_id != nullptr) {
    gpr_free(refresh_token->client_id);
    refresh_token->client_id = nullptr;
  }
  if (refresh_token->client_secret != nullptr) {
    gpr_free(refresh_token->client_secret);
    refresh_token->client_secret = nullptr;
  }
  if (refresh_token->refresh_token != nullptr) {
    gpr_free(refresh_token->refresh_token);
    refresh_token->refresh_token = nullptr;
  }
}

//
// Oauth2 Token Fetcher credentials.
//

static void oauth2_token_fetcher_destruct(grpc_call_credentials* creds) {
  grpc_oauth2_token_fetcher_credentials* c =
      reinterpret_cast<grpc_oauth2_token_fetcher_credentials*>(creds);
  GRPC_MDELEM_UNREF(c->access_token_md);
  gpr_mu_destroy(&c->mu);
  grpc_pollset_set_destroy(grpc_polling_entity_pollset_set(&c->pollent));
  grpc_httpcli_context_destroy(&c->httpcli_context);
}

grpc_credentials_status
grpc_oauth2_token_fetcher_credentials_parse_server_response(
    const grpc_http_response* response, grpc_mdelem* token_md,
    grpc_millis* token_lifetime) {
  char* null_terminated_body = nullptr;
  char* new_access_token = nullptr;
  grpc_credentials_status status = GRPC_CREDENTIALS_OK;
  grpc_json* json = nullptr;

  if (response == nullptr) {
    gpr_log(GPR_ERROR, "Received NULL response.");
    status = GRPC_CREDENTIALS_ERROR;
    goto end;
  }

  if (response->body_length > 0) {
    null_terminated_body =
        static_cast<char*>(gpr_malloc(response->body_length + 1));
    null_terminated_body[response->body_length] = '\0';
    memcpy(null_terminated_body, response->body, response->body_length);
  }

  if (response->status != 200) {
    gpr_log(GPR_ERROR, "Call to http server ended with error %d [%s].",
            response->status,
            null_terminated_body != nullptr ? null_terminated_body : "");
    status = GRPC_CREDENTIALS_ERROR;
    goto end;
  } else {
    grpc_json* access_token = nullptr;
    grpc_json* token_type = nullptr;
    grpc_json* expires_in = nullptr;
    grpc_json* ptr;
    json = grpc_json_parse_string(null_terminated_body);
    if (json == nullptr) {
      gpr_log(GPR_ERROR, "Could not parse JSON from %s", null_terminated_body);
      status = GRPC_CREDENTIALS_ERROR;
      goto end;
    }
    if (json->type != GRPC_JSON_OBJECT) {
      gpr_log(GPR_ERROR, "Response should be a JSON object");
      status = GRPC_CREDENTIALS_ERROR;
      goto end;
    }
    for (ptr = json->child; ptr; ptr = ptr->next) {
      if (strcmp(ptr->key, "access_token") == 0) {
        access_token = ptr;
      } else if (strcmp(ptr->key, "token_type") == 0) {
        token_type = ptr;
      } else if (strcmp(ptr->key, "expires_in") == 0) {
        expires_in = ptr;
      }
    }
    if (access_token == nullptr || access_token->type != GRPC_JSON_STRING) {
      gpr_log(GPR_ERROR, "Missing or invalid access_token in JSON.");
      status = GRPC_CREDENTIALS_ERROR;
      goto end;
    }
    if (token_type == nullptr || token_type->type != GRPC_JSON_STRING) {
      gpr_log(GPR_ERROR, "Missing or invalid token_type in JSON.");
      status = GRPC_CREDENTIALS_ERROR;
      goto end;
    }
    if (expires_in == nullptr || expires_in->type != GRPC_JSON_NUMBER) {
      gpr_log(GPR_ERROR, "Missing or invalid expires_in in JSON.");
      status = GRPC_CREDENTIALS_ERROR;
      goto end;
    }
    gpr_asprintf(&new_access_token, "%s %s", token_type->value,
                 access_token->value);
    *token_lifetime = strtol(expires_in->value, nullptr, 10) * GPR_MS_PER_SEC;
    if (!GRPC_MDISNULL(*token_md)) GRPC_MDELEM_UNREF(*token_md);
    *token_md = grpc_mdelem_from_slices(
        grpc_slice_from_static_string(GRPC_AUTHORIZATION_METADATA_KEY),
        grpc_slice_from_copied_string(new_access_token));
    status = GRPC_CREDENTIALS_OK;
  }

end:
  if (status != GRPC_CREDENTIALS_OK && !GRPC_MDISNULL(*token_md)) {
    GRPC_MDELEM_UNREF(*token_md);
    *token_md = GRPC_MDNULL;
  }
  if (null_terminated_body != nullptr) gpr_free(null_terminated_body);
  if (new_access_token != nullptr) gpr_free(new_access_token);
  if (json != nullptr) grpc_json_destroy(json);
  return status;
}

static void on_oauth2_token_fetcher_http_response(void* user_data,
                                                  grpc_error* error) {
  GRPC_LOG_IF_ERROR("oauth_fetch", GRPC_ERROR_REF(error));
  grpc_credentials_metadata_request* r =
      static_cast<grpc_credentials_metadata_request*>(user_data);
  grpc_oauth2_token_fetcher_credentials* c =
      reinterpret_cast<grpc_oauth2_token_fetcher_credentials*>(r->creds);
  grpc_mdelem access_token_md = GRPC_MDNULL;
  grpc_millis token_lifetime;
  grpc_credentials_status status =
      grpc_oauth2_token_fetcher_credentials_parse_server_response(
          &r->response, &access_token_md, &token_lifetime);
  // Update cache and grab list of pending requests.
  gpr_mu_lock(&c->mu);
  c->token_fetch_pending = false;
  c->access_token_md = GRPC_MDELEM_REF(access_token_md);
  c->token_expiration = status == GRPC_CREDENTIALS_OK
                            ? grpc_core::ExecCtx::Get()->Now() + token_lifetime
                            : 0;
  grpc_oauth2_pending_get_request_metadata* pending_request =
      c->pending_requests;
  c->pending_requests = nullptr;
  gpr_mu_unlock(&c->mu);
  // Invoke callbacks for all pending requests.
  while (pending_request != nullptr) {
    if (status == GRPC_CREDENTIALS_OK) {
      grpc_credentials_mdelem_array_add(pending_request->md_array,
                                        access_token_md);
    } else {
      error = GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
          "Error occured when fetching oauth2 token.", &error, 1);
    }
    GRPC_CLOSURE_SCHED(pending_request->on_request_metadata, error);
    grpc_polling_entity_del_from_pollset_set(
        pending_request->pollent, grpc_polling_entity_pollset_set(&c->pollent));
    grpc_oauth2_pending_get_request_metadata* prev = pending_request;
    pending_request = pending_request->next;
    gpr_free(prev);
  }
  GRPC_MDELEM_UNREF(access_token_md);
  grpc_call_credentials_unref(r->creds);
  grpc_credentials_metadata_request_destroy(r);
}

static bool oauth2_token_fetcher_get_request_metadata(
    grpc_call_credentials* creds, grpc_polling_entity* pollent,
    grpc_auth_metadata_context context, grpc_credentials_mdelem_array* md_array,
    grpc_closure* on_request_metadata, grpc_error** error) {
  grpc_oauth2_token_fetcher_credentials* c =
      reinterpret_cast<grpc_oauth2_token_fetcher_credentials*>(creds);
  // Check if we can use the cached token.
  grpc_millis refresh_threshold =
      GRPC_SECURE_TOKEN_REFRESH_THRESHOLD_SECS * GPR_MS_PER_SEC;
  grpc_mdelem cached_access_token_md = GRPC_MDNULL;
  gpr_mu_lock(&c->mu);
  if (!GRPC_MDISNULL(c->access_token_md) &&
      (c->token_expiration - grpc_core::ExecCtx::Get()->Now() >
       refresh_threshold)) {
    cached_access_token_md = GRPC_MDELEM_REF(c->access_token_md);
  }
  if (!GRPC_MDISNULL(cached_access_token_md)) {
    gpr_mu_unlock(&c->mu);
    grpc_credentials_mdelem_array_add(md_array, cached_access_token_md);
    GRPC_MDELEM_UNREF(cached_access_token_md);
    return true;
  }
  // Couldn't get the token from the cache.
  // Add request to c->pending_requests and start a new fetch if needed.
  grpc_oauth2_pending_get_request_metadata* pending_request =
      static_cast<grpc_oauth2_pending_get_request_metadata*>(
          gpr_malloc(sizeof(*pending_request)));
  pending_request->md_array = md_array;
  pending_request->on_request_metadata = on_request_metadata;
  pending_request->pollent = pollent;
  grpc_polling_entity_add_to_pollset_set(
      pollent, grpc_polling_entity_pollset_set(&c->pollent));
  pending_request->next = c->pending_requests;
  c->pending_requests = pending_request;
  bool start_fetch = false;
  if (!c->token_fetch_pending) {
    c->token_fetch_pending = true;
    start_fetch = true;
  }
  gpr_mu_unlock(&c->mu);
  if (start_fetch) {
    grpc_call_credentials_ref(creds);
    c->fetch_func(grpc_credentials_metadata_request_create(creds),
                  &c->httpcli_context, &c->pollent,
                  on_oauth2_token_fetcher_http_response,
                  grpc_core::ExecCtx::Get()->Now() + refresh_threshold);
  }
  return false;
}

static void oauth2_token_fetcher_cancel_get_request_metadata(
    grpc_call_credentials* creds, grpc_credentials_mdelem_array* md_array,
    grpc_error* error) {
  grpc_oauth2_token_fetcher_credentials* c =
      reinterpret_cast<grpc_oauth2_token_fetcher_credentials*>(creds);
  gpr_mu_lock(&c->mu);
  grpc_oauth2_pending_get_request_metadata* prev = nullptr;
  grpc_oauth2_pending_get_request_metadata* pending_request =
      c->pending_requests;
  while (pending_request != nullptr) {
    if (pending_request->md_array == md_array) {
      // Remove matching pending request from the list.
      if (prev != nullptr) {
        prev->next = pending_request->next;
      } else {
        c->pending_requests = pending_request->next;
      }
      // Invoke the callback immediately with an error.
      GRPC_CLOSURE_SCHED(pending_request->on_request_metadata,
                         GRPC_ERROR_REF(error));
      gpr_free(pending_request);
      break;
    }
    prev = pending_request;
    pending_request = pending_request->next;
  }
  gpr_mu_unlock(&c->mu);
  GRPC_ERROR_UNREF(error);
}

static void init_oauth2_token_fetcher(grpc_oauth2_token_fetcher_credentials* c,
                                      grpc_fetch_oauth2_func fetch_func) {
  memset(c, 0, sizeof(grpc_oauth2_token_fetcher_credentials));
  c->base.type = GRPC_CALL_CREDENTIALS_TYPE_OAUTH2;
  gpr_ref_init(&c->base.refcount, 1);
  gpr_mu_init(&c->mu);
  c->token_expiration = 0;
  c->fetch_func = fetch_func;
  c->pollent =
      grpc_polling_entity_create_from_pollset_set(grpc_pollset_set_create());
  grpc_httpcli_context_init(&c->httpcli_context);
}

//
//  Google Compute Engine credentials.
//

static grpc_call_credentials_vtable compute_engine_vtable = {
    oauth2_token_fetcher_destruct, oauth2_token_fetcher_get_request_metadata,
    oauth2_token_fetcher_cancel_get_request_metadata};

static void compute_engine_fetch_oauth2(
    grpc_credentials_metadata_request* metadata_req,
    grpc_httpcli_context* httpcli_context, grpc_polling_entity* pollent,
    grpc_iomgr_cb_func response_cb, grpc_millis deadline) {
  grpc_http_header header = {(char*)"Metadata-Flavor", (char*)"Google"};
  grpc_httpcli_request request;
  memset(&request, 0, sizeof(grpc_httpcli_request));
  request.host = (char*)GRPC_COMPUTE_ENGINE_METADATA_HOST;
  request.http.path = (char*)GRPC_COMPUTE_ENGINE_METADATA_TOKEN_PATH;
  request.http.hdr_count = 1;
  request.http.hdrs = &header;
  /* TODO(ctiller): Carry the resource_quota in ctx and share it with the host
     channel. This would allow us to cancel an authentication query when under
     extreme memory pressure. */
  grpc_resource_quota* resource_quota =
      grpc_resource_quota_create("oauth2_credentials");
  grpc_httpcli_get(
      httpcli_context, pollent, resource_quota, &request, deadline,
      GRPC_CLOSURE_CREATE(response_cb, metadata_req, grpc_schedule_on_exec_ctx),
      &metadata_req->response);
  grpc_resource_quota_unref_internal(resource_quota);
}

grpc_call_credentials* grpc_google_compute_engine_credentials_create(
    void* reserved) {
  grpc_oauth2_token_fetcher_credentials* c =
      static_cast<grpc_oauth2_token_fetcher_credentials*>(
          gpr_malloc(sizeof(grpc_oauth2_token_fetcher_credentials)));
  GRPC_API_TRACE("grpc_compute_engine_credentials_create(reserved=%p)", 1,
                 (reserved));
  GPR_ASSERT(reserved == nullptr);
  init_oauth2_token_fetcher(c, compute_engine_fetch_oauth2);
  c->base.vtable = &compute_engine_vtable;
  return &c->base;
}

//
// Google Refresh Token credentials.
//

static void refresh_token_destruct(grpc_call_credentials* creds) {
  grpc_google_refresh_token_credentials* c =
      reinterpret_cast<grpc_google_refresh_token_credentials*>(creds);
  grpc_auth_refresh_token_destruct(&c->refresh_token);
  oauth2_token_fetcher_destruct(&c->base.base);
}

static grpc_call_credentials_vtable refresh_token_vtable = {
    refresh_token_destruct, oauth2_token_fetcher_get_request_metadata,
    oauth2_token_fetcher_cancel_get_request_metadata};

static void refresh_token_fetch_oauth2(
    grpc_credentials_metadata_request* metadata_req,
    grpc_httpcli_context* httpcli_context, grpc_polling_entity* pollent,
    grpc_iomgr_cb_func response_cb, grpc_millis deadline) {
  grpc_google_refresh_token_credentials* c =
      reinterpret_cast<grpc_google_refresh_token_credentials*>(
          metadata_req->creds);
  grpc_http_header header = {(char*)"Content-Type",
                             (char*)"application/x-www-form-urlencoded"};
  grpc_httpcli_request request;
  char* body = nullptr;
  gpr_asprintf(&body, GRPC_REFRESH_TOKEN_POST_BODY_FORMAT_STRING,
               c->refresh_token.client_id, c->refresh_token.client_secret,
               c->refresh_token.refresh_token);
  memset(&request, 0, sizeof(grpc_httpcli_request));
  request.host = (char*)GRPC_GOOGLE_OAUTH2_SERVICE_HOST;
  request.http.path = (char*)GRPC_GOOGLE_OAUTH2_SERVICE_TOKEN_PATH;
  request.http.hdr_count = 1;
  request.http.hdrs = &header;
  request.handshaker = &grpc_httpcli_ssl;
  /* TODO(ctiller): Carry the resource_quota in ctx and share it with the host
     channel. This would allow us to cancel an authentication query when under
     extreme memory pressure. */
  grpc_resource_quota* resource_quota =
      grpc_resource_quota_create("oauth2_credentials_refresh");
  grpc_httpcli_post(
      httpcli_context, pollent, resource_quota, &request, body, strlen(body),
      deadline,
      GRPC_CLOSURE_CREATE(response_cb, metadata_req, grpc_schedule_on_exec_ctx),
      &metadata_req->response);
  grpc_resource_quota_unref_internal(resource_quota);
  gpr_free(body);
}

grpc_call_credentials*
grpc_refresh_token_credentials_create_from_auth_refresh_token(
    grpc_auth_refresh_token refresh_token) {
  grpc_google_refresh_token_credentials* c;
  if (!grpc_auth_refresh_token_is_valid(&refresh_token)) {
    gpr_log(GPR_ERROR, "Invalid input for refresh token credentials creation");
    return nullptr;
  }
  c = static_cast<grpc_google_refresh_token_credentials*>(
      gpr_zalloc(sizeof(grpc_google_refresh_token_credentials)));
  init_oauth2_token_fetcher(&c->base, refresh_token_fetch_oauth2);
  c->base.base.vtable = &refresh_token_vtable;
  c->refresh_token = refresh_token;
  return &c->base.base;
}

static char* create_loggable_refresh_token(grpc_auth_refresh_token* token) {
  if (strcmp(token->type, GRPC_AUTH_JSON_TYPE_INVALID) == 0) {
    return gpr_strdup("<Invalid json token>");
  }
  char* loggable_token = nullptr;
  gpr_asprintf(&loggable_token,
               "{\n type: %s\n client_id: %s\n client_secret: "
               "<redacted>\n refresh_token: <redacted>\n}",
               token->type, token->client_id);
  return loggable_token;
}

grpc_call_credentials* grpc_google_refresh_token_credentials_create(
    const char* json_refresh_token, void* reserved) {
  grpc_auth_refresh_token token =
      grpc_auth_refresh_token_create_from_string(json_refresh_token);
  if (grpc_api_trace.enabled()) {
    char* loggable_token = create_loggable_refresh_token(&token);
    gpr_log(GPR_INFO,
            "grpc_refresh_token_credentials_create(json_refresh_token=%s, "
            "reserved=%p)",
            loggable_token, reserved);
    gpr_free(loggable_token);
  }
  GPR_ASSERT(reserved == nullptr);
  return grpc_refresh_token_credentials_create_from_auth_refresh_token(token);
}

//
// Oauth2 Access Token credentials.
//

static void access_token_destruct(grpc_call_credentials* creds) {
  grpc_access_token_credentials* c =
      reinterpret_cast<grpc_access_token_credentials*>(creds);
  GRPC_MDELEM_UNREF(c->access_token_md);
}

static bool access_token_get_request_metadata(
    grpc_call_credentials* creds, grpc_polling_entity* pollent,
    grpc_auth_metadata_context context, grpc_credentials_mdelem_array* md_array,
    grpc_closure* on_request_metadata, grpc_error** error) {
  grpc_access_token_credentials* c =
      reinterpret_cast<grpc_access_token_credentials*>(creds);
  grpc_credentials_mdelem_array_add(md_array, c->access_token_md);
  return true;
}

static void access_token_cancel_get_request_metadata(
    grpc_call_credentials* c, grpc_credentials_mdelem_array* md_array,
    grpc_error* error) {
  GRPC_ERROR_UNREF(error);
}

static grpc_call_credentials_vtable access_token_vtable = {
    access_token_destruct, access_token_get_request_metadata,
    access_token_cancel_get_request_metadata};

grpc_call_credentials* grpc_access_token_credentials_create(
    const char* access_token, void* reserved) {
  grpc_access_token_credentials* c =
      static_cast<grpc_access_token_credentials*>(
          gpr_zalloc(sizeof(grpc_access_token_credentials)));
  GRPC_API_TRACE(
      "grpc_access_token_credentials_create(access_token=<redacted>, "
      "reserved=%p)",
      1, (reserved));
  GPR_ASSERT(reserved == nullptr);
  c->base.type = GRPC_CALL_CREDENTIALS_TYPE_OAUTH2;
  c->base.vtable = &access_token_vtable;
  gpr_ref_init(&c->base.refcount, 1);
  char* token_md_value;
  gpr_asprintf(&token_md_value, "Bearer %s", access_token);
  grpc_core::ExecCtx exec_ctx;
  c->access_token_md = grpc_mdelem_from_slices(
      grpc_slice_from_static_string(GRPC_AUTHORIZATION_METADATA_KEY),
      grpc_slice_from_copied_string(token_md_value));

  gpr_free(token_md_value);
  return &c->base;
}

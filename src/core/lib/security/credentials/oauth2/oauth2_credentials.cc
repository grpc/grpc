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

#include <grpc/support/port_platform.h>

#include "src/core/lib/security/credentials/oauth2/oauth2_credentials.h"

#include <string.h>

#include "absl/container/inlined_vector.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"

#include <grpc/grpc_security.h>
#include <grpc/impl/codegen/slice.h>
#include <grpc/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/load_file.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/security/util/json_util.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/uri/uri_parser.h"

using grpc_core::Json;

//
// Auth Refresh Token.
//

int grpc_auth_refresh_token_is_valid(
    const grpc_auth_refresh_token* refresh_token) {
  return (refresh_token != nullptr) &&
         strcmp(refresh_token->type, GRPC_AUTH_JSON_TYPE_INVALID) != 0;
}

grpc_auth_refresh_token grpc_auth_refresh_token_create_from_json(
    const Json& json) {
  grpc_auth_refresh_token result;
  const char* prop_value;
  int success = 0;
  grpc_error_handle error = GRPC_ERROR_NONE;

  memset(&result, 0, sizeof(grpc_auth_refresh_token));
  result.type = GRPC_AUTH_JSON_TYPE_INVALID;
  if (json.type() != Json::Type::OBJECT) {
    gpr_log(GPR_ERROR, "Invalid json.");
    goto end;
  }

  prop_value = grpc_json_get_string_property(json, "type", &error);
  GRPC_LOG_IF_ERROR("Parsing refresh token", error);
  if (prop_value == nullptr ||
      strcmp(prop_value, GRPC_AUTH_JSON_TYPE_AUTHORIZED_USER) != 0) {
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
  grpc_error_handle error = GRPC_ERROR_NONE;
  Json json = Json::Parse(json_string, &error);
  if (error != GRPC_ERROR_NONE) {
    gpr_log(GPR_ERROR, "JSON parsing failed: %s",
            grpc_error_std_string(error).c_str());
    GRPC_ERROR_UNREF(error);
  }
  return grpc_auth_refresh_token_create_from_json(json);
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

grpc_oauth2_token_fetcher_credentials::
    ~grpc_oauth2_token_fetcher_credentials() {
  gpr_mu_destroy(&mu_);
  grpc_pollset_set_destroy(grpc_polling_entity_pollset_set(&pollent_));
}

grpc_credentials_status
grpc_oauth2_token_fetcher_credentials_parse_server_response(
    const grpc_http_response* response,
    absl::optional<grpc_core::Slice>* token_value,
    grpc_millis* token_lifetime) {
  char* null_terminated_body = nullptr;
  grpc_credentials_status status = GRPC_CREDENTIALS_OK;
  Json json;

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
    const char* access_token = nullptr;
    const char* token_type = nullptr;
    const char* expires_in = nullptr;
    Json::Object::const_iterator it;
    grpc_error_handle error = GRPC_ERROR_NONE;
    json = Json::Parse(null_terminated_body, &error);
    if (error != GRPC_ERROR_NONE) {
      gpr_log(GPR_ERROR, "Could not parse JSON from %s: %s",
              null_terminated_body, grpc_error_std_string(error).c_str());
      GRPC_ERROR_UNREF(error);
      status = GRPC_CREDENTIALS_ERROR;
      goto end;
    }
    if (json.type() != Json::Type::OBJECT) {
      gpr_log(GPR_ERROR, "Response should be a JSON object");
      status = GRPC_CREDENTIALS_ERROR;
      goto end;
    }
    it = json.object_value().find("access_token");
    if (it == json.object_value().end() ||
        it->second.type() != Json::Type::STRING) {
      gpr_log(GPR_ERROR, "Missing or invalid access_token in JSON.");
      status = GRPC_CREDENTIALS_ERROR;
      goto end;
    }
    access_token = it->second.string_value().c_str();
    it = json.object_value().find("token_type");
    if (it == json.object_value().end() ||
        it->second.type() != Json::Type::STRING) {
      gpr_log(GPR_ERROR, "Missing or invalid token_type in JSON.");
      status = GRPC_CREDENTIALS_ERROR;
      goto end;
    }
    token_type = it->second.string_value().c_str();
    it = json.object_value().find("expires_in");
    if (it == json.object_value().end() ||
        it->second.type() != Json::Type::NUMBER) {
      gpr_log(GPR_ERROR, "Missing or invalid expires_in in JSON.");
      status = GRPC_CREDENTIALS_ERROR;
      goto end;
    }
    expires_in = it->second.string_value().c_str();
    *token_lifetime = strtol(expires_in, nullptr, 10) * GPR_MS_PER_SEC;
    *token_value = grpc_core::Slice::FromCopiedString(
        absl::StrCat(token_type, " ", access_token));
    status = GRPC_CREDENTIALS_OK;
  }

end:
  if (status != GRPC_CREDENTIALS_OK) *token_value = absl::nullopt;
  gpr_free(null_terminated_body);
  return status;
}

static void on_oauth2_token_fetcher_http_response(void* user_data,
                                                  grpc_error_handle error) {
  GRPC_LOG_IF_ERROR("oauth_fetch", GRPC_ERROR_REF(error));
  grpc_credentials_metadata_request* r =
      static_cast<grpc_credentials_metadata_request*>(user_data);
  grpc_oauth2_token_fetcher_credentials* c =
      reinterpret_cast<grpc_oauth2_token_fetcher_credentials*>(r->creds.get());
  c->on_http_response(r, error);
}

void grpc_oauth2_token_fetcher_credentials::on_http_response(
    grpc_credentials_metadata_request* r, grpc_error_handle error) {
  absl::optional<grpc_core::Slice> access_token_value;
  grpc_millis token_lifetime = 0;
  grpc_credentials_status status =
      error == GRPC_ERROR_NONE
          ? grpc_oauth2_token_fetcher_credentials_parse_server_response(
                &r->response, &access_token_value, &token_lifetime)
          : GRPC_CREDENTIALS_ERROR;
  // Update cache and grab list of pending requests.
  gpr_mu_lock(&mu_);
  token_fetch_pending_ = false;
  if (access_token_value.has_value()) {
    access_token_value_ = access_token_value->Ref();
  } else {
    access_token_value_ = absl::nullopt;
  }
  token_expiration_ =
      status == GRPC_CREDENTIALS_OK
          ? gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                         gpr_time_from_millis(token_lifetime, GPR_TIMESPAN))
          : gpr_inf_past(GPR_CLOCK_MONOTONIC);
  grpc_oauth2_pending_get_request_metadata* pending_request = pending_requests_;
  pending_requests_ = nullptr;
  gpr_mu_unlock(&mu_);
  // Invoke callbacks for all pending requests.
  while (pending_request != nullptr) {
    grpc_error_handle new_error = GRPC_ERROR_NONE;
    if (status == GRPC_CREDENTIALS_OK) {
      pending_request->md_array->emplace_back(
          grpc_core::Slice::FromStaticString(GRPC_AUTHORIZATION_METADATA_KEY),
          access_token_value->Ref());
    } else {
      new_error = GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
          "Error occurred when fetching oauth2 token.", &error, 1);
    }
    grpc_core::ExecCtx::Run(DEBUG_LOCATION,
                            pending_request->on_request_metadata, new_error);
    grpc_polling_entity_del_from_pollset_set(
        pending_request->pollent, grpc_polling_entity_pollset_set(&pollent_));
    grpc_oauth2_pending_get_request_metadata* prev = pending_request;
    pending_request = pending_request->next;
    gpr_free(prev);
  }
  Unref();
  grpc_credentials_metadata_request_destroy(r);
}

bool grpc_oauth2_token_fetcher_credentials::get_request_metadata(
    grpc_polling_entity* pollent, grpc_auth_metadata_context /*context*/,
    grpc_core::CredentialsMetadataArray* md_array,
    grpc_closure* on_request_metadata, grpc_error_handle* /*error*/) {
  // Check if we can use the cached token.
  grpc_millis refresh_threshold =
      GRPC_SECURE_TOKEN_REFRESH_THRESHOLD_SECS * GPR_MS_PER_SEC;
  absl::optional<grpc_core::Slice> cached_access_token_value;
  gpr_mu_lock(&mu_);
  if (access_token_value_.has_value() &&
      gpr_time_cmp(
          gpr_time_sub(token_expiration_, gpr_now(GPR_CLOCK_MONOTONIC)),
          gpr_time_from_seconds(GRPC_SECURE_TOKEN_REFRESH_THRESHOLD_SECS,
                                GPR_TIMESPAN)) > 0) {
    cached_access_token_value = access_token_value_->Ref();
  }
  if (cached_access_token_value.has_value()) {
    gpr_mu_unlock(&mu_);
    md_array->emplace_back(
        grpc_core::Slice::FromStaticString(GRPC_AUTHORIZATION_METADATA_KEY),
        std::move(*cached_access_token_value));
    return true;
  }
  // Couldn't get the token from the cache.
  // Add request to pending_requests_ and start a new fetch if needed.
  grpc_oauth2_pending_get_request_metadata* pending_request =
      static_cast<grpc_oauth2_pending_get_request_metadata*>(
          gpr_malloc(sizeof(*pending_request)));
  pending_request->md_array = md_array;
  pending_request->on_request_metadata = on_request_metadata;
  pending_request->pollent = pollent;
  grpc_polling_entity_add_to_pollset_set(
      pollent, grpc_polling_entity_pollset_set(&pollent_));
  pending_request->next = pending_requests_;
  pending_requests_ = pending_request;
  bool start_fetch = false;
  if (!token_fetch_pending_) {
    token_fetch_pending_ = true;
    start_fetch = true;
  }
  gpr_mu_unlock(&mu_);
  if (start_fetch) {
    Ref().release();
    fetch_oauth2(grpc_credentials_metadata_request_create(this->Ref()),
                 &pollent_, on_oauth2_token_fetcher_http_response,
                 grpc_core::ExecCtx::Get()->Now() + refresh_threshold);
  }
  return false;
}

void grpc_oauth2_token_fetcher_credentials::cancel_get_request_metadata(
    grpc_core::CredentialsMetadataArray* md_array, grpc_error_handle error) {
  gpr_mu_lock(&mu_);
  grpc_oauth2_pending_get_request_metadata* prev = nullptr;
  grpc_oauth2_pending_get_request_metadata* pending_request = pending_requests_;
  while (pending_request != nullptr) {
    if (pending_request->md_array == md_array) {
      // Remove matching pending request from the list.
      if (prev != nullptr) {
        prev->next = pending_request->next;
      } else {
        pending_requests_ = pending_request->next;
      }
      // Invoke the callback immediately with an error.
      grpc_core::ExecCtx::Run(DEBUG_LOCATION,
                              pending_request->on_request_metadata,
                              GRPC_ERROR_REF(error));
      gpr_free(pending_request);
      break;
    }
    prev = pending_request;
    pending_request = pending_request->next;
  }
  gpr_mu_unlock(&mu_);
  GRPC_ERROR_UNREF(error);
}

grpc_oauth2_token_fetcher_credentials::grpc_oauth2_token_fetcher_credentials()
    : grpc_call_credentials(GRPC_CALL_CREDENTIALS_TYPE_OAUTH2),
      token_expiration_(gpr_inf_past(GPR_CLOCK_MONOTONIC)),
      pollent_(grpc_polling_entity_create_from_pollset_set(
          grpc_pollset_set_create())) {
  gpr_mu_init(&mu_);
}

std::string grpc_oauth2_token_fetcher_credentials::debug_string() {
  return "OAuth2TokenFetcherCredentials";
}

//
//  Google Compute Engine credentials.
//

namespace {

class grpc_compute_engine_token_fetcher_credentials
    : public grpc_oauth2_token_fetcher_credentials {
 public:
  grpc_compute_engine_token_fetcher_credentials() = default;
  ~grpc_compute_engine_token_fetcher_credentials() override = default;

 protected:
  void fetch_oauth2(grpc_credentials_metadata_request* metadata_req,
                    grpc_polling_entity* pollent,
                    grpc_iomgr_cb_func response_cb,
                    grpc_millis deadline) override {
    grpc_http_header header = {const_cast<char*>("Metadata-Flavor"),
                               const_cast<char*>("Google")};
    grpc_httpcli_request request;
    memset(&request, 0, sizeof(grpc_httpcli_request));
    request.host = const_cast<char*>(GRPC_COMPUTE_ENGINE_METADATA_HOST);
    request.http.path =
        const_cast<char*>(GRPC_COMPUTE_ENGINE_METADATA_TOKEN_PATH);
    request.http.hdr_count = 1;
    request.http.hdrs = &header;
    /* TODO(ctiller): Carry the memory quota in ctx and share it with the host
       channel. This would allow us to cancel an authentication query when under
       extreme memory pressure. */
    grpc_httpcli_get(pollent, grpc_core::ResourceQuota::Default(), &request,
                     deadline,
                     GRPC_CLOSURE_INIT(&http_get_cb_closure_, response_cb,
                                       metadata_req, grpc_schedule_on_exec_ctx),
                     &metadata_req->response);
  }

  std::string debug_string() override {
    return absl::StrFormat(
        "GoogleComputeEngineTokenFetcherCredentials{%s}",
        grpc_oauth2_token_fetcher_credentials::debug_string());
  }

 private:
  grpc_closure http_get_cb_closure_;
};

}  // namespace

grpc_call_credentials* grpc_google_compute_engine_credentials_create(
    void* reserved) {
  GRPC_API_TRACE("grpc_compute_engine_credentials_create(reserved=%p)", 1,
                 (reserved));
  GPR_ASSERT(reserved == nullptr);
  return grpc_core::MakeRefCounted<
             grpc_compute_engine_token_fetcher_credentials>()
      .release();
}

//
// Google Refresh Token credentials.
//

grpc_google_refresh_token_credentials::
    ~grpc_google_refresh_token_credentials() {
  grpc_auth_refresh_token_destruct(&refresh_token_);
}

void grpc_google_refresh_token_credentials::fetch_oauth2(
    grpc_credentials_metadata_request* metadata_req,
    grpc_polling_entity* pollent, grpc_iomgr_cb_func response_cb,
    grpc_millis deadline) {
  grpc_http_header header = {
      const_cast<char*>("Content-Type"),
      const_cast<char*>("application/x-www-form-urlencoded")};
  grpc_httpcli_request request;
  std::string body = absl::StrFormat(
      GRPC_REFRESH_TOKEN_POST_BODY_FORMAT_STRING, refresh_token_.client_id,
      refresh_token_.client_secret, refresh_token_.refresh_token);
  memset(&request, 0, sizeof(grpc_httpcli_request));
  request.host = const_cast<char*>(GRPC_GOOGLE_OAUTH2_SERVICE_HOST);
  request.http.path = const_cast<char*>(GRPC_GOOGLE_OAUTH2_SERVICE_TOKEN_PATH);
  request.http.hdr_count = 1;
  request.http.hdrs = &header;
  request.handshaker = &grpc_httpcli_ssl;
  /* TODO(ctiller): Carry the memory quota in ctx and share it with the host
     channel. This would allow us to cancel an authentication query when under
     extreme memory pressure. */
  grpc_httpcli_post(pollent, grpc_core::ResourceQuota::Default(), &request,
                    body.c_str(), body.size(), deadline,
                    GRPC_CLOSURE_INIT(&http_post_cb_closure_, response_cb,
                                      metadata_req, grpc_schedule_on_exec_ctx),
                    &metadata_req->response);
}

grpc_google_refresh_token_credentials::grpc_google_refresh_token_credentials(
    grpc_auth_refresh_token refresh_token)
    : refresh_token_(refresh_token) {}

grpc_core::RefCountedPtr<grpc_call_credentials>
grpc_refresh_token_credentials_create_from_auth_refresh_token(
    grpc_auth_refresh_token refresh_token) {
  if (!grpc_auth_refresh_token_is_valid(&refresh_token)) {
    gpr_log(GPR_ERROR, "Invalid input for refresh token credentials creation");
    return nullptr;
  }
  return grpc_core::MakeRefCounted<grpc_google_refresh_token_credentials>(
      refresh_token);
}

std::string grpc_google_refresh_token_credentials::debug_string() {
  return absl::StrFormat("GoogleRefreshToken{ClientID:%s,%s}",
                         refresh_token_.client_id,
                         grpc_oauth2_token_fetcher_credentials::debug_string());
}

static std::string create_loggable_refresh_token(
    grpc_auth_refresh_token* token) {
  if (strcmp(token->type, GRPC_AUTH_JSON_TYPE_INVALID) == 0) {
    return "<Invalid json token>";
  }
  return absl::StrFormat(
      "{\n type: %s\n client_id: %s\n client_secret: "
      "<redacted>\n refresh_token: <redacted>\n}",
      token->type, token->client_id);
}

grpc_call_credentials* grpc_google_refresh_token_credentials_create(
    const char* json_refresh_token, void* reserved) {
  grpc_auth_refresh_token token =
      grpc_auth_refresh_token_create_from_string(json_refresh_token);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_api_trace)) {
    gpr_log(GPR_INFO,
            "grpc_refresh_token_credentials_create(json_refresh_token=%s, "
            "reserved=%p)",
            create_loggable_refresh_token(&token).c_str(), reserved);
  }
  GPR_ASSERT(reserved == nullptr);
  return grpc_refresh_token_credentials_create_from_auth_refresh_token(token)
      .release();
}

//
// STS credentials.
//

namespace grpc_core {

namespace {

void MaybeAddToBody(const char* field_name, const char* field,
                    std::vector<std::string>* body) {
  if (field == nullptr || strlen(field) == 0) return;
  body->push_back(absl::StrFormat("&%s=%s", field_name, field));
}

grpc_error_handle LoadTokenFile(const char* path, gpr_slice* token) {
  grpc_error_handle err = grpc_load_file(path, 1, token);
  if (err != GRPC_ERROR_NONE) return err;
  if (GRPC_SLICE_LENGTH(*token) == 0) {
    gpr_log(GPR_ERROR, "Token file %s is empty", path);
    err = GRPC_ERROR_CREATE_FROM_STATIC_STRING("Token file is empty.");
  }
  return err;
}

class StsTokenFetcherCredentials
    : public grpc_oauth2_token_fetcher_credentials {
 public:
  StsTokenFetcherCredentials(URI sts_url,
                             const grpc_sts_credentials_options* options)
      : sts_url_(std::move(sts_url)),
        resource_(gpr_strdup(options->resource)),
        audience_(gpr_strdup(options->audience)),
        scope_(gpr_strdup(options->scope)),
        requested_token_type_(gpr_strdup(options->requested_token_type)),
        subject_token_path_(gpr_strdup(options->subject_token_path)),
        subject_token_type_(gpr_strdup(options->subject_token_type)),
        actor_token_path_(gpr_strdup(options->actor_token_path)),
        actor_token_type_(gpr_strdup(options->actor_token_type)) {}

  std::string debug_string() override {
    return absl::StrFormat(
        "StsTokenFetcherCredentials{Path:%s,Authority:%s,%s}", sts_url_.path(),
        sts_url_.authority(),
        grpc_oauth2_token_fetcher_credentials::debug_string());
  }

 private:
  void fetch_oauth2(grpc_credentials_metadata_request* metadata_req,
                    grpc_polling_entity* pollent,
                    grpc_iomgr_cb_func response_cb,
                    grpc_millis deadline) override {
    char* body = nullptr;
    size_t body_length = 0;
    grpc_error_handle err = FillBody(&body, &body_length);
    if (err != GRPC_ERROR_NONE) {
      response_cb(metadata_req, err);
      GRPC_ERROR_UNREF(err);
      return;
    }
    grpc_http_header header = {
        const_cast<char*>("Content-Type"),
        const_cast<char*>("application/x-www-form-urlencoded")};
    grpc_httpcli_request request;
    memset(&request, 0, sizeof(grpc_httpcli_request));
    request.host = const_cast<char*>(sts_url_.authority().c_str());
    request.http.path = const_cast<char*>(sts_url_.path().c_str());
    request.http.hdr_count = 1;
    request.http.hdrs = &header;
    request.handshaker = (sts_url_.scheme() == "https")
                             ? &grpc_httpcli_ssl
                             : &grpc_httpcli_plaintext;
    /* TODO(ctiller): Carry the memory quota in ctx and share it with the host
       channel. This would allow us to cancel an authentication query when under
       extreme memory pressure. */
    grpc_httpcli_post(
        pollent, ResourceQuota::Default(), &request, body, body_length,
        deadline,
        GRPC_CLOSURE_INIT(&http_post_cb_closure_, response_cb, metadata_req,
                          grpc_schedule_on_exec_ctx),
        &metadata_req->response);
    gpr_free(body);
  }

  grpc_error_handle FillBody(char** body, size_t* body_length) {
    *body = nullptr;
    std::vector<std::string> body_parts;
    grpc_slice subject_token = grpc_empty_slice();
    grpc_slice actor_token = grpc_empty_slice();
    grpc_error_handle err = GRPC_ERROR_NONE;

    auto cleanup = [&body, &body_length, &body_parts, &subject_token,
                    &actor_token, &err]() {
      if (err == GRPC_ERROR_NONE) {
        std::string body_str = absl::StrJoin(body_parts, "");
        *body = gpr_strdup(body_str.c_str());
        *body_length = body_str.size();
      }
      grpc_slice_unref_internal(subject_token);
      grpc_slice_unref_internal(actor_token);
      return err;
    };

    err = LoadTokenFile(subject_token_path_.get(), &subject_token);
    if (err != GRPC_ERROR_NONE) return cleanup();
    body_parts.push_back(absl::StrFormat(
        GRPC_STS_POST_MINIMAL_BODY_FORMAT_STRING,
        reinterpret_cast<const char*>(GRPC_SLICE_START_PTR(subject_token)),
        subject_token_type_.get()));
    MaybeAddToBody("resource", resource_.get(), &body_parts);
    MaybeAddToBody("audience", audience_.get(), &body_parts);
    MaybeAddToBody("scope", scope_.get(), &body_parts);
    MaybeAddToBody("requested_token_type", requested_token_type_.get(),
                   &body_parts);
    if ((actor_token_path_ != nullptr) && *actor_token_path_ != '\0') {
      err = LoadTokenFile(actor_token_path_.get(), &actor_token);
      if (err != GRPC_ERROR_NONE) return cleanup();
      MaybeAddToBody(
          "actor_token",
          reinterpret_cast<const char*>(GRPC_SLICE_START_PTR(actor_token)),
          &body_parts);
      MaybeAddToBody("actor_token_type", actor_token_type_.get(), &body_parts);
    }
    return cleanup();
  }

  URI sts_url_;
  grpc_closure http_post_cb_closure_;
  UniquePtr<char> resource_;
  UniquePtr<char> audience_;
  UniquePtr<char> scope_;
  UniquePtr<char> requested_token_type_;
  UniquePtr<char> subject_token_path_;
  UniquePtr<char> subject_token_type_;
  UniquePtr<char> actor_token_path_;
  UniquePtr<char> actor_token_type_;
};

}  // namespace

absl::StatusOr<URI> ValidateStsCredentialsOptions(
    const grpc_sts_credentials_options* options) {
  absl::InlinedVector<grpc_error_handle, 3> error_list;
  absl::StatusOr<URI> sts_url =
      URI::Parse(options->token_exchange_service_uri == nullptr
                     ? ""
                     : options->token_exchange_service_uri);
  if (!sts_url.ok()) {
    error_list.push_back(GRPC_ERROR_CREATE_FROM_CPP_STRING(
        absl::StrFormat("Invalid or missing STS endpoint URL. Error: %s",
                        sts_url.status().ToString())));
  } else if (sts_url->scheme() != "https" && sts_url->scheme() != "http") {
    error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Invalid URI scheme, must be https to http."));
  }
  if (options->subject_token_path == nullptr ||
      strlen(options->subject_token_path) == 0) {
    error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "subject_token needs to be specified"));
  }
  if (options->subject_token_type == nullptr ||
      strlen(options->subject_token_type) == 0) {
    error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "subject_token_type needs to be specified"));
  }
  if (error_list.empty()) {
    return sts_url;
  }
  auto grpc_error_vec = GRPC_ERROR_CREATE_FROM_VECTOR(
      "Invalid STS Credentials Options", &error_list);
  auto retval =
      absl::InvalidArgumentError(grpc_error_std_string(grpc_error_vec));
  GRPC_ERROR_UNREF(grpc_error_vec);
  return retval;
}

}  // namespace grpc_core

grpc_call_credentials* grpc_sts_credentials_create(
    const grpc_sts_credentials_options* options, void* reserved) {
  GPR_ASSERT(reserved == nullptr);
  absl::StatusOr<grpc_core::URI> sts_url =
      grpc_core::ValidateStsCredentialsOptions(options);
  if (!sts_url.ok()) {
    gpr_log(GPR_ERROR, "STS Credentials creation failed. Error: %s.",
            sts_url.status().ToString().c_str());
    return nullptr;
  }
  return grpc_core::MakeRefCounted<grpc_core::StsTokenFetcherCredentials>(
             std::move(*sts_url), options)
      .release();
}

//
// Oauth2 Access Token credentials.
//

bool grpc_access_token_credentials::get_request_metadata(
    grpc_polling_entity* /*pollent*/, grpc_auth_metadata_context /*context*/,
    grpc_core::CredentialsMetadataArray* md_array,
    grpc_closure* /*on_request_metadata*/, grpc_error_handle* /*error*/) {
  md_array->emplace_back(
      grpc_core::Slice::FromStaticString(GRPC_AUTHORIZATION_METADATA_KEY),
      access_token_value_.Ref());
  return true;
}

void grpc_access_token_credentials::cancel_get_request_metadata(
    grpc_core::CredentialsMetadataArray* /*md_array*/,
    grpc_error_handle error) {
  GRPC_ERROR_UNREF(error);
}

grpc_access_token_credentials::grpc_access_token_credentials(
    const char* access_token)
    : grpc_call_credentials(GRPC_CALL_CREDENTIALS_TYPE_OAUTH2),
      access_token_value_(grpc_core::Slice::FromCopiedString(
          absl::StrCat("Bearer ", access_token))) {}

std::string grpc_access_token_credentials::debug_string() {
  return absl::StrFormat("AccessTokenCredentials{Token:present}");
}

grpc_call_credentials* grpc_access_token_credentials_create(
    const char* access_token, void* reserved) {
  GRPC_API_TRACE(
      "grpc_access_token_credentials_create(access_token=<redacted>, "
      "reserved=%p)",
      1, (reserved));
  GPR_ASSERT(reserved == nullptr);
  return grpc_core::MakeRefCounted<grpc_access_token_credentials>(access_token)
      .release();
}

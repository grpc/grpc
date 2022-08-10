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

#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <atomic>
#include <map>
#include <memory>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/impl/codegen/gpr_slice.h>
#include <grpc/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/http/httpcli_ssl_credentials.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/load_file.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/security/util/json_util.h"
#include "src/core/lib/slice/slice_refcount.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/transport.h"
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
  Json json;
  auto json_or = Json::Parse(json_string);
  if (!json_or.ok()) {
    gpr_log(GPR_ERROR, "JSON parsing failed: %s",
            json_or.status().ToString().c_str());
  } else {
    json = std::move(*json_or);
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
    grpc_core::Duration* token_lifetime) {
  char* null_terminated_body = nullptr;
  grpc_credentials_status status = GRPC_CREDENTIALS_OK;

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
    auto json = Json::Parse(
        null_terminated_body != nullptr ? null_terminated_body : "");
    if (!json.ok()) {
      gpr_log(GPR_ERROR, "Could not parse JSON from %s: %s",
              null_terminated_body, json.status().ToString().c_str());
      status = GRPC_CREDENTIALS_ERROR;
      goto end;
    }
    if (json->type() != Json::Type::OBJECT) {
      gpr_log(GPR_ERROR, "Response should be a JSON object");
      status = GRPC_CREDENTIALS_ERROR;
      goto end;
    }
    it = json->object_value().find("access_token");
    if (it == json->object_value().end() ||
        it->second.type() != Json::Type::STRING) {
      gpr_log(GPR_ERROR, "Missing or invalid access_token in JSON.");
      status = GRPC_CREDENTIALS_ERROR;
      goto end;
    }
    access_token = it->second.string_value().c_str();
    it = json->object_value().find("token_type");
    if (it == json->object_value().end() ||
        it->second.type() != Json::Type::STRING) {
      gpr_log(GPR_ERROR, "Missing or invalid token_type in JSON.");
      status = GRPC_CREDENTIALS_ERROR;
      goto end;
    }
    token_type = it->second.string_value().c_str();
    it = json->object_value().find("expires_in");
    if (it == json->object_value().end() ||
        it->second.type() != Json::Type::NUMBER) {
      gpr_log(GPR_ERROR, "Missing or invalid expires_in in JSON.");
      status = GRPC_CREDENTIALS_ERROR;
      goto end;
    }
    expires_in = it->second.string_value().c_str();
    *token_lifetime =
        grpc_core::Duration::Seconds(strtol(expires_in, nullptr, 10));
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
  grpc_core::Duration token_lifetime;
  grpc_credentials_status status =
      GRPC_ERROR_IS_NONE(error)
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
  token_expiration_ = status == GRPC_CREDENTIALS_OK
                          ? gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                                         token_lifetime.as_timespec())
                          : gpr_inf_past(GPR_CLOCK_MONOTONIC);
  grpc_oauth2_pending_get_request_metadata* pending_request = pending_requests_;
  pending_requests_ = nullptr;
  gpr_mu_unlock(&mu_);
  // Invoke callbacks for all pending requests.
  while (pending_request != nullptr) {
    if (status == GRPC_CREDENTIALS_OK) {
      pending_request->md->Append(
          GRPC_AUTHORIZATION_METADATA_KEY, access_token_value->Ref(),
          [](absl::string_view, const grpc_core::Slice&) { abort(); });
      pending_request->result = std::move(pending_request->md);
    } else {
      auto err = GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
          "Error occurred when fetching oauth2 token.", &error, 1);
      pending_request->result = grpc_error_to_absl_status(err);
      GRPC_ERROR_UNREF(err);
    }
    pending_request->done.store(true, std::memory_order_release);
    pending_request->waker.Wakeup();
    grpc_polling_entity_del_from_pollset_set(
        pending_request->pollent, grpc_polling_entity_pollset_set(&pollent_));
    grpc_oauth2_pending_get_request_metadata* prev = pending_request;
    pending_request = pending_request->next;
    prev->Unref();
  }
  delete r;
}

grpc_core::ArenaPromise<absl::StatusOr<grpc_core::ClientMetadataHandle>>
grpc_oauth2_token_fetcher_credentials::GetRequestMetadata(
    grpc_core::ClientMetadataHandle initial_metadata,
    const grpc_call_credentials::GetRequestMetadataArgs*) {
  // Check if we can use the cached token.
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
    initial_metadata->Append(
        GRPC_AUTHORIZATION_METADATA_KEY, std::move(*cached_access_token_value),
        [](absl::string_view, const grpc_core::Slice&) { abort(); });
    return grpc_core::Immediate(std::move(initial_metadata));
  }
  // Couldn't get the token from the cache.
  // Add request to pending_requests_ and start a new fetch if needed.
  grpc_core::Duration refresh_threshold =
      grpc_core::Duration::Seconds(GRPC_SECURE_TOKEN_REFRESH_THRESHOLD_SECS);
  auto pending_request =
      grpc_core::MakeRefCounted<grpc_oauth2_pending_get_request_metadata>();
  pending_request->pollent = grpc_core::GetContext<grpc_polling_entity>();
  pending_request->waker = grpc_core::Activity::current()->MakeNonOwningWaker();
  grpc_polling_entity_add_to_pollset_set(
      pending_request->pollent, grpc_polling_entity_pollset_set(&pollent_));
  pending_request->next = pending_requests_;
  pending_request->md = std::move(initial_metadata);
  pending_requests_ = pending_request->Ref().release();
  bool start_fetch = false;
  if (!token_fetch_pending_) {
    token_fetch_pending_ = true;
    start_fetch = true;
  }
  gpr_mu_unlock(&mu_);
  if (start_fetch) {
    fetch_oauth2(new grpc_credentials_metadata_request(Ref()), &pollent_,
                 on_oauth2_token_fetcher_http_response,
                 grpc_core::ExecCtx::Get()->Now() + refresh_threshold);
  }
  return
      [pending_request]()
          -> grpc_core::Poll<absl::StatusOr<grpc_core::ClientMetadataHandle>> {
        if (!pending_request->done.load(std::memory_order_acquire)) {
          return grpc_core::Pending{};
        }
        return std::move(pending_request->result);
      };
}

grpc_oauth2_token_fetcher_credentials::grpc_oauth2_token_fetcher_credentials()
    : token_expiration_(gpr_inf_past(GPR_CLOCK_MONOTONIC)),
      pollent_(grpc_polling_entity_create_from_pollset_set(
          grpc_pollset_set_create())) {
  gpr_mu_init(&mu_);
}

std::string grpc_oauth2_token_fetcher_credentials::debug_string() {
  return "OAuth2TokenFetcherCredentials";
}

grpc_core::UniqueTypeName grpc_oauth2_token_fetcher_credentials::type() const {
  static grpc_core::UniqueTypeName::Factory kFactory("Oauth2");
  return kFactory.Create();
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
                    grpc_core::Timestamp deadline) override {
    grpc_http_header header = {const_cast<char*>("Metadata-Flavor"),
                               const_cast<char*>("Google")};
    grpc_http_request request;
    memset(&request, 0, sizeof(grpc_http_request));
    request.hdr_count = 1;
    request.hdrs = &header;
    /* TODO(ctiller): Carry the memory quota in ctx and share it with the host
       channel. This would allow us to cancel an authentication query when under
       extreme memory pressure. */
    auto uri = grpc_core::URI::Create("http", GRPC_COMPUTE_ENGINE_METADATA_HOST,
                                      GRPC_COMPUTE_ENGINE_METADATA_TOKEN_PATH,
                                      {} /* query params */, "" /* fragment */);
    GPR_ASSERT(uri.ok());  // params are hardcoded
    http_request_ = grpc_core::HttpRequest::Get(
        std::move(*uri), nullptr /* channel args */, pollent, &request,
        deadline,
        GRPC_CLOSURE_INIT(&http_get_cb_closure_, response_cb, metadata_req,
                          grpc_schedule_on_exec_ctx),
        &metadata_req->response,
        grpc_core::RefCountedPtr<grpc_channel_credentials>(
            grpc_insecure_credentials_create()));
    http_request_->Start();
  }

  std::string debug_string() override {
    return absl::StrFormat(
        "GoogleComputeEngineTokenFetcherCredentials{%s}",
        grpc_oauth2_token_fetcher_credentials::debug_string());
  }

 private:
  grpc_closure http_get_cb_closure_;
  grpc_core::OrphanablePtr<grpc_core::HttpRequest> http_request_;
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
    grpc_core::Timestamp deadline) {
  grpc_http_header header = {
      const_cast<char*>("Content-Type"),
      const_cast<char*>("application/x-www-form-urlencoded")};
  grpc_http_request request;
  std::string body = absl::StrFormat(
      GRPC_REFRESH_TOKEN_POST_BODY_FORMAT_STRING, refresh_token_.client_id,
      refresh_token_.client_secret, refresh_token_.refresh_token);
  memset(&request, 0, sizeof(grpc_http_request));
  request.hdr_count = 1;
  request.hdrs = &header;
  request.body = const_cast<char*>(body.c_str());
  request.body_length = body.size();
  /* TODO(ctiller): Carry the memory quota in ctx and share it with the host
     channel. This would allow us to cancel an authentication query when under
     extreme memory pressure. */
  auto uri = grpc_core::URI::Create("https", GRPC_GOOGLE_OAUTH2_SERVICE_HOST,
                                    GRPC_GOOGLE_OAUTH2_SERVICE_TOKEN_PATH,
                                    {} /* query params */, "" /* fragment */);
  GPR_ASSERT(uri.ok());  // params are hardcoded
  http_request_ = grpc_core::HttpRequest::Post(
      std::move(*uri), nullptr /* channel args */, pollent, &request, deadline,
      GRPC_CLOSURE_INIT(&http_post_cb_closure_, response_cb, metadata_req,
                        grpc_schedule_on_exec_ctx),
      &metadata_req->response, grpc_core::CreateHttpRequestSSLCredentials());
  http_request_->Start();
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

grpc_core::UniqueTypeName grpc_google_refresh_token_credentials::type() const {
  static grpc_core::UniqueTypeName::Factory kFactory("GoogleRefreshToken");
  return kFactory.Create();
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
  if (!GRPC_ERROR_IS_NONE(err)) return err;
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
                    Timestamp deadline) override {
    grpc_http_request request;
    memset(&request, 0, sizeof(grpc_http_request));
    grpc_error_handle err = FillBody(&request.body, &request.body_length);
    if (!GRPC_ERROR_IS_NONE(err)) {
      response_cb(metadata_req, err);
      GRPC_ERROR_UNREF(err);
      return;
    }
    grpc_http_header header = {
        const_cast<char*>("Content-Type"),
        const_cast<char*>("application/x-www-form-urlencoded")};
    request.hdr_count = 1;
    request.hdrs = &header;
    /* TODO(ctiller): Carry the memory quota in ctx and share it with the host
       channel. This would allow us to cancel an authentication query when under
       extreme memory pressure. */
    RefCountedPtr<grpc_channel_credentials> http_request_creds;
    if (sts_url_.scheme() == "http") {
      http_request_creds = RefCountedPtr<grpc_channel_credentials>(
          grpc_insecure_credentials_create());
    } else {
      http_request_creds = CreateHttpRequestSSLCredentials();
    }
    http_request_ = HttpRequest::Post(
        sts_url_, nullptr /* channel args */, pollent, &request, deadline,
        GRPC_CLOSURE_INIT(&http_post_cb_closure_, response_cb, metadata_req,
                          grpc_schedule_on_exec_ctx),
        &metadata_req->response, std::move(http_request_creds));
    http_request_->Start();
    gpr_free(request.body);
  }

  grpc_error_handle FillBody(char** body, size_t* body_length) {
    *body = nullptr;
    std::vector<std::string> body_parts;
    grpc_slice subject_token = grpc_empty_slice();
    grpc_slice actor_token = grpc_empty_slice();
    grpc_error_handle err = GRPC_ERROR_NONE;

    auto cleanup = [&body, &body_length, &body_parts, &subject_token,
                    &actor_token, &err]() {
      if (GRPC_ERROR_IS_NONE(err)) {
        std::string body_str = absl::StrJoin(body_parts, "");
        *body = gpr_strdup(body_str.c_str());
        *body_length = body_str.size();
      }
      grpc_slice_unref_internal(subject_token);
      grpc_slice_unref_internal(actor_token);
      return err;
    };

    err = LoadTokenFile(subject_token_path_.get(), &subject_token);
    if (!GRPC_ERROR_IS_NONE(err)) return cleanup();
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
      if (!GRPC_ERROR_IS_NONE(err)) return cleanup();
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
  OrphanablePtr<HttpRequest> http_request_;
};

}  // namespace

absl::StatusOr<URI> ValidateStsCredentialsOptions(
    const grpc_sts_credentials_options* options) {
  std::vector<grpc_error_handle> error_list;
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

grpc_core::ArenaPromise<absl::StatusOr<grpc_core::ClientMetadataHandle>>
grpc_access_token_credentials::GetRequestMetadata(
    grpc_core::ClientMetadataHandle initial_metadata,
    const grpc_call_credentials::GetRequestMetadataArgs*) {
  initial_metadata->Append(
      GRPC_AUTHORIZATION_METADATA_KEY, access_token_value_.Ref(),
      [](absl::string_view, const grpc_core::Slice&) { abort(); });
  return grpc_core::Immediate(std::move(initial_metadata));
}

grpc_core::UniqueTypeName grpc_access_token_credentials::Type() {
  static grpc_core::UniqueTypeName::Factory kFactory("AccessToken");
  return kFactory.Create();
}

grpc_access_token_credentials::grpc_access_token_credentials(
    const char* access_token)
    : access_token_value_(grpc_core::Slice::FromCopiedString(
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

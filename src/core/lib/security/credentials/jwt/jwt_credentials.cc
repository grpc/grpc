/*
 *
 * Copyright 2016 gRPC authors.
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

#include "src/core/lib/security/credentials/jwt/jwt_credentials.h"

#include <inttypes.h>
#include <string.h>

#include <string>

#include "absl/strings/str_cat.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>

#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/lib/uri/uri_parser.h"

using grpc_core::Json;

grpc_service_account_jwt_access_credentials::
    ~grpc_service_account_jwt_access_credentials() {
  grpc_auth_json_key_destruct(&key_);
  gpr_mu_destroy(&cache_mu_);
}

bool grpc_service_account_jwt_access_credentials::get_request_metadata(
    grpc_polling_entity* /*pollent*/, grpc_auth_metadata_context context,
    grpc_core::CredentialsMetadataArray* md_array,
    grpc_closure* /*on_request_metadata*/, grpc_error_handle* error) {
  gpr_timespec refresh_threshold = gpr_time_from_seconds(
      GRPC_SECURE_TOKEN_REFRESH_THRESHOLD_SECS, GPR_TIMESPAN);

  // Remove service name from service_url to follow the audience format
  // dictated in https://google.aip.dev/auth/4111.
  absl::StatusOr<std::string> uri =
      grpc_core::RemoveServiceNameFromJwtUri(context.service_url);
  if (!uri.ok()) {
    *error = absl_status_to_grpc_error(uri.status());
    return true;
  }
  /* See if we can return a cached jwt. */
  absl::optional<grpc_core::Slice> jwt_value;
  {
    gpr_mu_lock(&cache_mu_);
    if (cached_.has_value() && cached_->service_url == *uri &&
        (gpr_time_cmp(
             gpr_time_sub(cached_->jwt_expiration, gpr_now(GPR_CLOCK_REALTIME)),
             refresh_threshold) > 0)) {
      jwt_value = cached_->jwt_value.Ref();
    }
    gpr_mu_unlock(&cache_mu_);
  }

  if (!jwt_value.has_value()) {
    char* jwt = nullptr;
    /* Generate a new jwt. */
    gpr_mu_lock(&cache_mu_);
    cached_.reset();
    jwt = grpc_jwt_encode_and_sign(&key_, uri->c_str(), jwt_lifetime_, nullptr);
    if (jwt != nullptr) {
      std::string md_value = absl::StrCat("Bearer ", jwt);
      gpr_free(jwt);
      jwt_value = grpc_core::Slice::FromCopiedString(md_value);
      cached_ = {jwt_value->Ref(), std::move(*uri),
                 gpr_time_add(gpr_now(GPR_CLOCK_REALTIME), jwt_lifetime_)};
    }
    gpr_mu_unlock(&cache_mu_);
  }

  if (jwt_value.has_value()) {
    md_array->emplace_back(
        grpc_core::Slice::FromStaticString(GRPC_AUTHORIZATION_METADATA_KEY),
        std::move(*jwt_value));
  } else {
    *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("Could not generate JWT.");
  }
  return true;
}

void grpc_service_account_jwt_access_credentials::cancel_get_request_metadata(
    grpc_core::CredentialsMetadataArray* /*md_array*/,
    grpc_error_handle error) {
  GRPC_ERROR_UNREF(error);
}

grpc_service_account_jwt_access_credentials::
    grpc_service_account_jwt_access_credentials(grpc_auth_json_key key,
                                                gpr_timespec token_lifetime)
    : grpc_call_credentials(GRPC_CALL_CREDENTIALS_TYPE_JWT), key_(key) {
  gpr_timespec max_token_lifetime = grpc_max_auth_token_lifetime();
  if (gpr_time_cmp(token_lifetime, max_token_lifetime) > 0) {
    gpr_log(GPR_INFO,
            "Cropping token lifetime to maximum allowed value (%d secs).",
            static_cast<int>(max_token_lifetime.tv_sec));
    token_lifetime = grpc_max_auth_token_lifetime();
  }
  jwt_lifetime_ = token_lifetime;
  gpr_mu_init(&cache_mu_);
}

grpc_core::RefCountedPtr<grpc_call_credentials>
grpc_service_account_jwt_access_credentials_create_from_auth_json_key(
    grpc_auth_json_key key, gpr_timespec token_lifetime) {
  if (!grpc_auth_json_key_is_valid(&key)) {
    gpr_log(GPR_ERROR, "Invalid input for jwt credentials creation");
    return nullptr;
  }
  return grpc_core::MakeRefCounted<grpc_service_account_jwt_access_credentials>(
      key, token_lifetime);
}

static char* redact_private_key(const char* json_key) {
  grpc_error_handle error = GRPC_ERROR_NONE;
  Json json = Json::Parse(json_key, &error);
  if (error != GRPC_ERROR_NONE || json.type() != Json::Type::OBJECT) {
    GRPC_ERROR_UNREF(error);
    return gpr_strdup("<Json failed to parse.>");
  }
  (*json.mutable_object())["private_key"] = "<redacted>";
  return gpr_strdup(json.Dump(/*indent=*/2).c_str());
}

grpc_call_credentials* grpc_service_account_jwt_access_credentials_create(
    const char* json_key, gpr_timespec token_lifetime, void* reserved) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_api_trace)) {
    char* clean_json = redact_private_key(json_key);
    gpr_log(GPR_INFO,
            "grpc_service_account_jwt_access_credentials_create("
            "json_key=%s, "
            "token_lifetime="
            "gpr_timespec { tv_sec: %" PRId64
            ", tv_nsec: %d, clock_type: %d }, "
            "reserved=%p)",
            clean_json, token_lifetime.tv_sec, token_lifetime.tv_nsec,
            static_cast<int>(token_lifetime.clock_type), reserved);
    gpr_free(clean_json);
  }
  GPR_ASSERT(reserved == nullptr);
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
  grpc_core::ExecCtx exec_ctx;
  return grpc_service_account_jwt_access_credentials_create_from_auth_json_key(
             grpc_auth_json_key_create_from_string(json_key), token_lifetime)
      .release();
}

namespace grpc_core {

absl::StatusOr<std::string> RemoveServiceNameFromJwtUri(absl::string_view uri) {
  auto parsed = URI::Parse(uri);
  if (!parsed.ok()) {
    return parsed.status();
  }
  return absl::StrFormat("%s://%s/", parsed->scheme(), parsed->authority());
}

}  // namespace grpc_core

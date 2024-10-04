//
// Copyright 2016 gRPC authors.
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
//
//

#include "src/core/lib/security/credentials/jwt/jwt_credentials.h"

#include <grpc/credentials.h>
#include <grpc/support/alloc.h>
#include <grpc/support/json.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>
#include <inttypes.h>
#include <stdlib.h>

#include <memory>
#include <string>
#include <utility>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/security/credentials/call_creds_util.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/util/json/json.h"
#include "src/core/util/json/json_reader.h"
#include "src/core/util/json/json_writer.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/uri.h"

using grpc_core::Json;

grpc_service_account_jwt_access_credentials::
    ~grpc_service_account_jwt_access_credentials() {
  grpc_auth_json_key_destruct(&key_);
  gpr_mu_destroy(&cache_mu_);
}

grpc_core::ArenaPromise<absl::StatusOr<grpc_core::ClientMetadataHandle>>
grpc_service_account_jwt_access_credentials::GetRequestMetadata(
    grpc_core::ClientMetadataHandle initial_metadata,
    const grpc_call_credentials::GetRequestMetadataArgs* args) {
  gpr_timespec refresh_threshold = gpr_time_from_seconds(
      GRPC_SECURE_TOKEN_REFRESH_THRESHOLD_SECS, GPR_TIMESPAN);

  // Remove service name from service_url to follow the audience format
  // dictated in https://google.aip.dev/auth/4111.
  absl::StatusOr<std::string> uri = grpc_core::RemoveServiceNameFromJwtUri(
      grpc_core::MakeJwtServiceUrl(initial_metadata, args));
  if (!uri.ok()) {
    return grpc_core::Immediate(uri.status());
  }
  // See if we can return a cached jwt.
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
    // Generate a new jwt.
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

  if (!jwt_value.has_value()) {
    return grpc_core::Immediate(
        absl::UnauthenticatedError("Could not generate JWT."));
  }

  initial_metadata->Append(
      GRPC_AUTHORIZATION_METADATA_KEY, std::move(*jwt_value),
      [](absl::string_view, const grpc_core::Slice&) { abort(); });
  return grpc_core::Immediate(std::move(initial_metadata));
}

grpc_service_account_jwt_access_credentials::
    grpc_service_account_jwt_access_credentials(grpc_auth_json_key key,
                                                gpr_timespec token_lifetime)
    : key_(key) {
  gpr_timespec max_token_lifetime = grpc_max_auth_token_lifetime();
  if (gpr_time_cmp(token_lifetime, max_token_lifetime) > 0) {
    VLOG(2) << "Cropping token lifetime to maximum allowed value ("
            << max_token_lifetime.tv_sec << " secs).";
    token_lifetime = grpc_max_auth_token_lifetime();
  }
  jwt_lifetime_ = token_lifetime;
  gpr_mu_init(&cache_mu_);
}

grpc_core::UniqueTypeName grpc_service_account_jwt_access_credentials::Type() {
  static grpc_core::UniqueTypeName::Factory kFactory("Jwt");
  return kFactory.Create();
}

grpc_core::RefCountedPtr<grpc_call_credentials>
grpc_service_account_jwt_access_credentials_create_from_auth_json_key(
    grpc_auth_json_key key, gpr_timespec token_lifetime) {
  if (!grpc_auth_json_key_is_valid(&key)) {
    LOG(ERROR) << "Invalid input for jwt credentials creation";
    return nullptr;
  }
  return grpc_core::MakeRefCounted<grpc_service_account_jwt_access_credentials>(
      key, token_lifetime);
}

static char* redact_private_key(const char* json_key) {
  auto json = grpc_core::JsonParse(json_key);
  if (!json.ok() || json->type() != Json::Type::kObject) {
    return gpr_strdup("<Json failed to parse.>");
  }
  Json::Object object = json->object();
  object["private_key"] = Json::FromString("<redacted>");
  return gpr_strdup(
      grpc_core::JsonDump(Json::FromObject(std::move(object)), /*indent=*/2)
          .c_str());
}

grpc_call_credentials* grpc_service_account_jwt_access_credentials_create(
    const char* json_key, gpr_timespec token_lifetime, void* reserved) {
  if (GRPC_TRACE_FLAG_ENABLED(api)) {
    char* clean_json = redact_private_key(json_key);
    VLOG(2) << "grpc_service_account_jwt_access_credentials_create("
            << "json_key=" << clean_json
            << ", token_lifetime=gpr_timespec { tv_sec: "
            << token_lifetime.tv_sec << ", tv_nsec: " << token_lifetime.tv_nsec
            << ", clock_type: " << token_lifetime.clock_type
            << " }, reserved=" << reserved << ")";
    gpr_free(clean_json);
  }
  CHECK_EQ(reserved, nullptr);
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

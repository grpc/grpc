//
//
// Copyright 2015 gRPC authors.
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

#include "src/core/credentials/call/regional_access_boundary_util.h"

#include <grpc/grpc_security_constants.h>
#include <grpc/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>
#include <string.h>

#include <unordered_set>

#include "absl/log/log.h"
#include "src/core/call/metadata_batch.h"
#include "src/core/credentials/call/call_credentials.h"
#include "src/core/credentials/call/call_creds_util.h"
#include "src/core/credentials/transport/transport_credentials.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/arena_promise.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/util/backoff.h"
#include "src/core/util/env.h"
#include "src/core/util/http_client/httpcli.h"
#include "src/core/util/http_client/httpcli_ssl_credentials.h"
#include "src/core/util/json/json.h"
#include "src/core/util/json/json_reader.h"

namespace grpc_core {

namespace {
constexpr int kMaxRegionalAccessBoundaryRetries = 6;
constexpr absl::string_view kRegionalEndpoint = "rep.googleapis.com";
}  // namespace

struct RegionalAccessBoundaryRequest
    : public grpc_core::RefCounted<RegionalAccessBoundaryRequest> {
  grpc_core::RefCountedPtr<grpc_call_credentials> creds;
  grpc_http_response response;
  grpc_core::OrphanablePtr<grpc_core::HttpRequest> http_request;

  // Retry state
  grpc_core::BackOff backoff;
  std::string access_token;
  grpc_core::URI uri;
  grpc_polling_entity pollent;
  grpc_timer retry_timer;
  grpc_closure retry_callback;
  int num_retries = 0;

  explicit RegionalAccessBoundaryRequest(
      grpc_core::BackOff::Options backoff_options)
      : backoff(backoff_options) {
    pollent =
        grpc_polling_entity_create_from_pollset_set(grpc_pollset_set_create());
  }

  ~RegionalAccessBoundaryRequest() override {
    grpc_http_response_destroy(&response);
    grpc_pollset_set_destroy(grpc_polling_entity_pollset_set(&pollent));
  }
};

void StartRegionalAccessBoundaryFetch(
    grpc_core::RefCountedPtr<RegionalAccessBoundaryRequest> req);

void RetryFetchRegionalAccessBoundary(void* arg, grpc_error_handle error) {
  grpc_core::RefCountedPtr<RegionalAccessBoundaryRequest> req(
      static_cast<RegionalAccessBoundaryRequest*>(arg));
  if (error.ok()) {
    StartRegionalAccessBoundaryFetch(std::move(req));
  } else {
    gpr_mu_lock(&req->creds->regional_access_boundary_cache_mu);
    req->creds->regional_access_boundary_fetch_in_flight = false;
    req->creds->regional_access_boundary_cooldown_deadline = gpr_time_add(
        gpr_now(GPR_CLOCK_REALTIME),
        gpr_time_from_seconds(
            GRPC_REGIONAL_ACCESS_BOUNDARY_BASE_COOLDOWN_DURATION_SECS *
                req->creds->regional_access_boundary_cooldown_multiplier,
            GPR_TIMESPAN));
    if (req->creds->regional_access_boundary_cooldown_multiplier *
            GRPC_REGIONAL_ACCESS_BOUNDARY_BASE_COOLDOWN_DURATION_SECS <
        GRPC_REGIONAL_ACCESS_BOUNDARY_MAX_COOLDOWN_DURATION_SECS) {
      req->creds->regional_access_boundary_cooldown_multiplier *= 2;
    }
    gpr_mu_unlock(&req->creds->regional_access_boundary_cache_mu);
  }
}

void OnRegionalAccessBoundaryResponse(void* arg, grpc_error_handle error) {
  grpc_core::RefCountedPtr<RegionalAccessBoundaryRequest> req(
      static_cast<RegionalAccessBoundaryRequest*>(arg));

  ::grpc_call_credentials* creds = req->creds.get();
  bool should_retry = false;
  bool success = false;

  if (error.ok() && req->response.status == 200) {
    auto json = grpc_core::JsonParse(req->response.body);

    if (json.ok() && json->type() == grpc_core::Json::Type::kObject) {
      std::string encoded_locations;
      std::vector<std::string> locations;

      auto it_encoded = json->object().find("encodedLocations");
      if (it_encoded != json->object().end() &&
          it_encoded->second.type() == grpc_core::Json::Type::kString) {
        encoded_locations = it_encoded->second.string();
      }

      auto it_locations = json->object().find("locations");
      if (it_locations != json->object().end() &&
          it_locations->second.type() == grpc_core::Json::Type::kArray) {
        for (const auto& loc : it_locations->second.array()) {
          if (loc.type() == grpc_core::Json::Type::kString) {
            locations.push_back(loc.string());
          }
        }
      }

      if (!encoded_locations.empty()) {
        gpr_timespec ttl = gpr_time_from_seconds(
            GRPC_REGIONAL_ACCESS_BOUNDARY_CACHE_DURATION_SECS, GPR_TIMESPAN);
        gpr_mu_lock(&creds->regional_access_boundary_cache_mu);
        creds->regional_access_boundary_cache = {
            std::move(encoded_locations),
            std::move(locations),
            gpr_time_add(gpr_now(GPR_CLOCK_REALTIME), ttl)};

        // On success, reset the cooldown multiplier.
        req->creds->regional_access_boundary_cooldown_multiplier = 1;
        gpr_mu_unlock(&creds->regional_access_boundary_cache_mu);
        success = true;
      }
    }
  }

  if (!success && req->num_retries < kMaxRegionalAccessBoundaryRetries) {
    // Retry on network errors, 403, 404, or 5xx HTTP errors
    if (!error.ok() || req->response.status == 403 ||
        req->response.status == 404 ||
        (req->response.status >= 500 && req->response.status < 600)) {
      should_retry = true;
    }
  }

  if (should_retry) {
    req->num_retries++;
    grpc_core::Duration delay = req->backoff.NextAttemptDelay();
    req->Ref().release();
    grpc_timer_init(&req->retry_timer, grpc_core::Timestamp::Now() + delay,
                    GRPC_CLOSURE_INIT(&req->retry_callback,
                                      RetryFetchRegionalAccessBoundary,
                                      req.get(), grpc_schedule_on_exec_ctx));
  } else {
    gpr_mu_lock(&creds->regional_access_boundary_cache_mu);
    creds->regional_access_boundary_fetch_in_flight = false;
    // Log failure and set cooldown on failure
    if (!success) {
      LOG(ERROR) << "Regional access boundary request failed. Entering "
                    "cooldown period. Error: "
                 << grpc_core::StatusToString(error)
                 << ", HTTP Status: " << req->response.status << ", Body: "
                 << absl::string_view(req->response.body,
                                      req->response.body_length);
      req->creds->regional_access_boundary_cooldown_deadline = gpr_time_add(
          gpr_now(GPR_CLOCK_REALTIME),
          gpr_time_from_seconds(
              GRPC_REGIONAL_ACCESS_BOUNDARY_BASE_COOLDOWN_DURATION_SECS *
                  req->creds->regional_access_boundary_cooldown_multiplier,
              GPR_TIMESPAN));
      if (req->creds->regional_access_boundary_cooldown_multiplier *
              GRPC_REGIONAL_ACCESS_BOUNDARY_BASE_COOLDOWN_DURATION_SECS <
          GRPC_REGIONAL_ACCESS_BOUNDARY_MAX_COOLDOWN_DURATION_SECS) {
        req->creds->regional_access_boundary_cooldown_multiplier *= 2;
      }
    }
    gpr_mu_unlock(&creds->regional_access_boundary_cache_mu);
  }
  req->http_request.reset();
}

void StartRegionalAccessBoundaryFetch(
    grpc_core::RefCountedPtr<RegionalAccessBoundaryRequest> req) {
  grpc_http_request request;
  memset(&request, 0, sizeof(request));

  char* key = gpr_strdup("Authorization");
  char* value = gpr_strdup(req->access_token.data());
  grpc_http_header header = {key, value};
  request.hdr_count = 1;
  request.hdrs = &header;

  // We pass req.get() as arg to OnRegionalAccessBoundaryResponse. We must
  // manually take a ref because C-callback doesn't. The Ref is consumed in
  // OnHttpResponse.
  req->Ref().release();

  grpc_http_response_destroy(&req->response);
  req->response = grpc_http_response();

  req->http_request = grpc_core::HttpRequest::Get(
      req->uri,
      nullptr,  // channel_args
      &req->pollent, &request,
      grpc_core::Timestamp::Now() + grpc_core::Duration::Seconds(60),
      GRPC_CLOSURE_CREATE(OnRegionalAccessBoundaryResponse, req.get(),
                          grpc_schedule_on_exec_ctx),
      &req->response,
      grpc_core::RefCountedPtr<grpc_channel_credentials>(
          grpc_core::CreateHttpRequestSSLCredentials()));
  gpr_mu_lock(&req->creds->regional_access_boundary_cache_mu);
  req->creds->regional_access_boundary_fetch_in_flight = true;
  gpr_mu_unlock(&req->creds->regional_access_boundary_cache_mu);
  if (req->http_request != nullptr) {
    req->http_request->Start();
  }
  gpr_free(key);
  gpr_free(value);
}

grpc_core::ArenaPromise<absl::StatusOr<grpc_core::ClientMetadataHandle>>
FetchRegionalAccessBoundary(
    grpc_core::RefCountedPtr<grpc_call_credentials> creds,
    grpc_core::ClientMetadataHandle initial_metadata) {
  if (!grpc_core::IsRegionalAccessBoundaryLookupEnabled()) {
    return grpc_core::Immediate(std::move(initial_metadata));
  }

  if (initial_metadata->get_pointer(grpc_core::HttpAuthorityMetadata())
          ->as_string_view()
          .find(kRegionalEndpoint) != absl::string_view::npos) {
    return grpc_core::Immediate(std::move(initial_metadata));
  }

  {
    gpr_mu_lock(&creds->regional_access_boundary_cache_mu);
    auto hasValidCache = creds->regional_access_boundary_cache.has_value() &&
                         creds->regional_access_boundary_cache->isValid();
    if (hasValidCache) {
      initial_metadata->Append(
          GRPC_ALLOWED_LOCATIONS_KEY,
          grpc_core::Slice::FromCopiedString(
              creds->regional_access_boundary_cache->encoded_locations),
          [](absl::string_view, const grpc_core::Slice&) { abort(); });
    }
    if (hasValidCache || creds->regional_access_boundary_fetch_in_flight ||
        gpr_time_cmp(gpr_now(GPR_CLOCK_REALTIME),
                     creds->regional_access_boundary_cooldown_deadline) < 0) {
      gpr_mu_unlock(&creds->regional_access_boundary_cache_mu);
      return grpc_core::Immediate(std::move(initial_metadata));
    }
    gpr_mu_unlock(&creds->regional_access_boundary_cache_mu);
  }

  auto url = creds->build_regional_access_boundary_url();

  auto request_uri = grpc_core::URI::Parse(url);

  if (!request_uri.ok()) {
    LOG(ERROR) << "Unable to create URI for the credential type: "
               << creds->debug_string();
    return grpc_core::Immediate(std::move(initial_metadata));
  }

  auto req = grpc_core::MakeRefCounted<RegionalAccessBoundaryRequest>(
      grpc_core::BackOff::Options()
          .set_initial_backoff(grpc_core::Duration::Seconds(1))
          .set_multiplier(2.0)
          .set_jitter(0.2)
          .set_max_backoff(grpc_core::Duration::Seconds(60)));
  req->creds = std::move(creds);
  req->uri = std::move(*request_uri);

  auto* caller_pollent = grpc_core::MaybeGetContext<grpc_polling_entity>();
  if (caller_pollent != nullptr) {
    grpc_polling_entity_add_to_pollset_set(
        caller_pollent, grpc_polling_entity_pollset_set(&req->pollent));
  }

  std::string access_token;
  auto auth_val = initial_metadata->GetStringValue(
      GRPC_AUTHORIZATION_METADATA_KEY, &access_token);
  if (auth_val.has_value()) {
    req->access_token = std::string(*auth_val);
  } else {
    return grpc_core::Immediate(std::move(initial_metadata));
  }

  StartRegionalAccessBoundaryFetch(req);

  return grpc_core::Immediate(std::move(initial_metadata));
}
}  // namespace grpc_core
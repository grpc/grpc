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

#include "src/core/credentials/call/regional_access_boundary_fetcher.h"

#include <grpc/support/string_util.h>

#include "src/core/util/env.h"
#include "src/core/credentials/call/call_credentials.h" // Used only for authorization key, can be removed if needed
#include "src/core/util/http_client/httpcli_ssl_credentials.h"
#include "src/core/credentials/transport/transport_credentials.h"
#include "src/core/util/json/json.h"
#include "src/core/util/json/json_reader.h"

namespace grpc_core {

namespace {
  constexpr int kMaxRegionalAccessBoundaryRetries = 6;
  constexpr absl::string_view kRegionalEndpoint = "rep.googleapis.com";
  constexpr absl::string_view kGoogleApisEndpoint = "googleapis.com";
  // Retryable HTTP Status Codes
  constexpr int HTTP_INTERNAL_SERVER_ERROR = 500;
  constexpr int HTTP_BAD_GATEWAY = 502;
  constexpr int HTTP_SERVICE_UNAVAILABLE = 503;
  constexpr int HTTP_GATEWAY_TIMEOUT = 504;
  const std::unordered_set<int> kRetryableStatusCodes = {
    HTTP_INTERNAL_SERVER_ERROR, HTTP_BAD_GATEWAY,
    HTTP_SERVICE_UNAVAILABLE,   HTTP_GATEWAY_TIMEOUT};
}

bool IsRegionalAccessBoundaryLookupEnabled() {
  auto is_rab_lookup_enabled =
      grpc_core::GetEnv("GOOGLE_AUTH_REGIONAL_ACCESS_BOUNDARY_ENABLED");
  if (is_rab_lookup_enabled.has_value() &&
      !is_rab_lookup_enabled.value().empty()) {
    std::string value = is_rab_lookup_enabled.value();
    for (auto& c : value) c = std::tolower(c);

    // Use a set for the "in" behavior
    static const std::unordered_set<std::string> targets = {"true", "1"};
    return targets.count(value);
  }
  return false;
}

struct RegionalAccessBoundaryFetcher::RegionalAccessBoundaryRequest
    : public grpc_core::RefCounted<RegionalAccessBoundaryRequest> {
  grpc_http_response response;
  grpc_core::OrphanablePtr<grpc_core::HttpRequest> http_request;
  std::string access_token;
  grpc_core::URI uri;
  grpc_polling_entity pollent;
  int num_retries = 0;
  grpc_core::RefCountedPtr<RegionalAccessBoundaryFetcher> fetcher;

  explicit RegionalAccessBoundaryRequest() {
    pollent =
        grpc_polling_entity_create_from_pollset_set(grpc_pollset_set_create());
  }

  ~RegionalAccessBoundaryRequest() override {
    grpc_http_response_destroy(&response);
    grpc_pollset_set_destroy(grpc_polling_entity_pollset_set(&pollent));
  }
};

RegionalAccessBoundaryFetcher::RegionalAccessBoundaryFetcher() : backoff_(BackOff::Options()
          .set_initial_backoff(Duration::Seconds(1))
          .set_multiplier(2.0)
          .set_jitter(0.2)
          .set_max_backoff(Duration::Seconds(60))) {}

void RegionalAccessBoundaryFetcher::StartRegionalAccessBoundaryFetch(
    grpc_core::RefCountedPtr<RegionalAccessBoundaryRequest> req) {
  grpc_http_request request;
  memset(&request, 0, sizeof(request));

  char* key = gpr_strdup("Authorization");
  char* value = gpr_strdup(req->access_token.data());
  grpc_http_header header = {key, value};
  request.hdr_count = 1;
  request.hdrs = &header;

  req->fetcher = Ref();

  // We pass req.get() as arg to OnRegionalAccessBoundaryResponse. We must
  // manually take a ref because C-callback doesn't. The Ref is consumed in
  // OnHttpResponse.
  req->Ref().release();

  grpc_http_response_destroy(&req->response);
  req->response = grpc_http_response();

  grpc_closure* closure = GRPC_CLOSURE_CREATE(
    OnRegionalAccessBoundaryResponseWrapper, 
    req.get(),
    grpc_schedule_on_exec_ctx
  );


  req->http_request = grpc_core::HttpRequest::Get(
      req->uri,
      nullptr,  // channel_args
      &req->pollent, &request,
      grpc_core::Timestamp::Now() + grpc_core::Duration::Seconds(60),
      closure,
      &req->response,
      grpc_core::RefCountedPtr<grpc_channel_credentials>(
          grpc_core::CreateHttpRequestSSLCredentials()));
  {
    grpc_core::MutexLock lock(&cache_mu_);
    fetch_in_flight_ = true;
  }
  
  if (req->http_request != nullptr) {
    req->http_request->Start();
  }
  gpr_free(key);
  gpr_free(value);
}

void RegionalAccessBoundaryFetcher::OnRegionalAccessBoundaryResponseWrapper(
    void* arg, grpc_error_handle error) {
  RegionalAccessBoundaryRequest* req =
      static_cast<RegionalAccessBoundaryRequest*>(arg);
  req->fetcher->OnRegionalAccessBoundaryResponse(arg, error);
}

void RegionalAccessBoundaryFetcher::OnRegionalAccessBoundaryResponse(
    void* arg, grpc_error_handle error) {
  grpc_core::RefCountedPtr<RegionalAccessBoundaryRequest> req(
      static_cast<RegionalAccessBoundaryRequest*>(arg));

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
        grpc_core::Duration ttl = grpc_core::Duration::Seconds(
            GRPC_REGIONAL_ACCESS_BOUNDARY_CACHE_DURATION_SECS);
        {
            grpc_core::MutexLock lock(&cache_mu_);
            cache_ = {
                std::move(encoded_locations),
                std::move(locations),
                grpc_core::Timestamp::Now() + ttl
            };
            // On success, reset the cooldown multiplier.
            cooldown_multiplier_ = 1;
        }
        success = true;
      }
    }
  }
  if (!success && req->num_retries < kMaxRegionalAccessBoundaryRetries) {
    // Retry on 5xx HTTP errors
    if (!error.ok() || kRetryableStatusCodes.find(req->response.status) !=
                           kRetryableStatusCodes.end()) {
      should_retry = true;
    }
  }
  
  if (should_retry) {
    std::cout << "Regional access boundary request failed. Entering "
                        "cooldown period. Error: "
                    << grpc_core::StatusToString(error)
                    << ", HTTP Status: " << req->response.status << ", Body: "
                    << absl::string_view(req->response.body,
                                        req->response.body_length);
    req->num_retries++;
    grpc_core::Duration delay = backoff_.NextAttemptDelay();
    RegionalAccessBoundaryRequest* raw_req = req->Ref().release();
    retry_timer_handle_ = event_engine_->RunAfter(delay, [this, raw_req]() {
        RetryFetchRegionalAccessBoundary(raw_req, absl::OkStatus());
    });
  } else {
    {
        grpc_core::MutexLock lock(&cache_mu_);
        fetch_in_flight_ = false;
        // Log failure and set cooldown on failure
        if (!success) {
            std::cout << "Regional access boundary request failed. Entering "
                        "cooldown period. Error: "
                    << grpc_core::StatusToString(error)
                    << ", HTTP Status: " << req->response.status << ", Body: "
                    << absl::string_view(req->response.body,
                                        req->response.body_length);
            cooldown_deadline_ = grpc_core::Timestamp::Now() + 
                grpc_core::Duration::Seconds(
                    GRPC_REGIONAL_ACCESS_BOUNDARY_BASE_COOLDOWN_DURATION_SECS *
                    cooldown_multiplier_);
            if (cooldown_multiplier_ *
                    GRPC_REGIONAL_ACCESS_BOUNDARY_BASE_COOLDOWN_DURATION_SECS <
                GRPC_REGIONAL_ACCESS_BOUNDARY_MAX_COOLDOWN_DURATION_SECS) {
                cooldown_multiplier_ *= 2;
            }
        }
    } 
  }
  req->http_request.reset();
}

void RegionalAccessBoundaryFetcher::RetryFetchRegionalAccessBoundary(
    void* arg, grpc_error_handle error) {
  retry_timer_handle_.reset();
  grpc_core::RefCountedPtr<RegionalAccessBoundaryRequest> req(
      static_cast<RegionalAccessBoundaryRequest*>(arg));
  if (error.ok()) {
    StartRegionalAccessBoundaryFetch(std::move(req));
  } else {
    {
        grpc_core::MutexLock lock(&cache_mu_);
        fetch_in_flight_ = false;
        cooldown_deadline_ = grpc_core::Timestamp::Now() + 
            grpc_core::Duration::Seconds(
                GRPC_REGIONAL_ACCESS_BOUNDARY_BASE_COOLDOWN_DURATION_SECS *
                cooldown_multiplier_);
        if (cooldown_multiplier_ *
                GRPC_REGIONAL_ACCESS_BOUNDARY_BASE_COOLDOWN_DURATION_SECS <
            GRPC_REGIONAL_ACCESS_BOUNDARY_MAX_COOLDOWN_DURATION_SECS) {
            cooldown_multiplier_ *= 2;
        }
    }
  }
}

ArenaPromise<absl::StatusOr<ClientMetadataHandle>> RegionalAccessBoundaryFetcher::Fetch(
    std::string lookup_url,
    ClientMetadataHandle initial_metadata) {
  // if (!IsRegionalAccessBoundaryLookupEnabled()) {
  //   return Immediate(std::move(initial_metadata));
  // }

  auto authority = initial_metadata->get_pointer(HttpAuthorityMetadata())
                       ->as_string_view();
  if (authority.find(kRegionalEndpoint) != std::string_view::npos ) {
    return Immediate(std::move(initial_metadata));
  }

  {
    grpc_core::MutexLock lock(&cache_mu_);
    auto hasValidCache = cache_.has_value() && cache_->isValid();
    if (hasValidCache) {
        initial_metadata->Append(
            GRPC_ALLOWED_LOCATIONS_KEY,
            Slice::FromCopiedString(
                cache_->encoded_locations),
            [](absl::string_view, const Slice&) { abort(); });
    }
    if (hasValidCache ||
        fetch_in_flight_ ||
        grpc_core::Timestamp::Now() < cooldown_deadline_) {
      return Immediate(std::move(initial_metadata));
    }
    fetch_in_flight_ = true;
  }
  auto request_uri = URI::Parse(lookup_url);

  if (!request_uri.ok()) {
    LOG(ERROR) << "Unable to create URI for the lookup URL: "
               << lookup_url;
    return Immediate(std::move(initial_metadata));
  }

  auto req = MakeRefCounted<RegionalAccessBoundaryRequest>();
  req->uri = std::move(*request_uri);
  auto* caller_pollent = MaybeGetContext<grpc_polling_entity>();
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
    return Immediate(std::move(initial_metadata));
  }

  StartRegionalAccessBoundaryFetch(req);

  return Immediate(std::move(initial_metadata));
}

void RegionalAccessBoundaryFetcher::InvalidateCache() {
  grpc_core::MutexLock lock(&cache_mu_);
  cache_.reset();
}

void RegionalAccessBoundaryFetcher::Cancel() {
  if (retry_timer_handle_.has_value()) {
    if (event_engine_->Cancel(*retry_timer_handle_)) {
      retry_timer_handle_.reset();
    }
  }
}


}  // namespace grpc_core
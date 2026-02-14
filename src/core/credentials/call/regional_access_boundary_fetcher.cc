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
#include "src/core/credentials/call/call_credentials.h"
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
  constexpr int kInternalServerErrorCode = 500;
  constexpr int kBadGatewayErrorCode = 502;
  constexpr int kServiceUnavailableErrorCode = 503;
  constexpr int kGatewayTimeoutErrorCode = 504;
  const std::unordered_set<int> kRetryableStatusCodes = {
    kInternalServerErrorCode, kBadGatewayErrorCode,
    kServiceUnavailableErrorCode, kGatewayTimeoutErrorCode};
}

RegionalAccessBoundaryFetcher::RegionalAccessBoundaryFetcher() : backoff_(BackOff::Options()
          .set_initial_backoff(Duration::Seconds(1))
          .set_multiplier(2.0)
          .set_jitter(0.2)
          .set_max_backoff(Duration::Seconds(60))) {}

void RegionalAccessBoundaryFetcher::StartRegionalAccessBoundaryFetch(
    grpc_core::RefCountedPtr<RegionalAccessBoundaryRequest> req) {
  {
    grpc_core::MutexLock lock(&cache_mu_);
    if (!fetch_in_flight_) { 
      return; 
    }
  }

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
  // OnRegionalAccessBoundaryResponse.
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
    pending_request_ = req;
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

  {
    grpc_core::MutexLock lock(&cache_mu_);
    // Only clear pending_request_ if it's the one we're processing now
    if (pending_request_ == req) {
      pending_request_.reset();
    }
  }

  if (error.ok() && req->response.status == 200) {
    auto json = grpc_core::JsonParse(absl::string_view(req->response.body, req->response.body_length));

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
            backoff_.Reset();
        }
        success = true;
      }
    }
  }
  if (!success && !absl::IsCancelled(error) && req->num_retries < kMaxRegionalAccessBoundaryRetries) {
    // Retry on 5xx HTTP errors
    if (!error.ok() || kRetryableStatusCodes.find(req->response.status) !=
                           kRetryableStatusCodes.end()) {
      should_retry = true;
    }
  }
  
  if (should_retry) {
    LOG(WARNING) << "Regional access boundary request will be retried after failing with error: "
                    << grpc_core::StatusToString(error)
                    << ", HTTP Status: " << req->response.status << ", Body: "
                    << absl::string_view(req->response.body,
                                        req->response.body_length);
    req->num_retries++;
    grpc_core::Duration delay;
    auto self = Ref();
    {
      grpc_core::MutexLock lock(&cache_mu_);
      delay = backoff_.NextAttemptDelay();
      retry_timer_handle_ = event_engine_->RunAfter(delay, [self, req]() {
          self->RetryFetchRegionalAccessBoundary(req.get(), absl::OkStatus());
      });
    }
  } else {
    {
        grpc_core::MutexLock lock(&cache_mu_);
        fetch_in_flight_ = false;
        // Log failure and set cooldown on failure
        if (!success) {
            LOG(WARNING) << "Regional access boundary request failed. Entering "
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
}

void RegionalAccessBoundaryFetcher::RetryFetchRegionalAccessBoundary(
    RegionalAccessBoundaryRequest* raw_req, grpc_error_handle error) {
  {
    grpc_core::MutexLock lock(&cache_mu_);
    retry_timer_handle_.reset();
  }
  if (error.ok()) {
    StartRegionalAccessBoundaryFetch(raw_req->Ref());
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

ClientMetadataHandle RegionalAccessBoundaryFetcher::Fetch(
    std::string lookup_url,
    std::string access_token,
    ClientMetadataHandle initial_metadata) {
  auto authority_ptr = initial_metadata->get_pointer(HttpAuthorityMetadata());
  if (authority_ptr == nullptr) {
    return initial_metadata;
  }
  auto authority = authority_ptr->as_string_view();
  // Regional access boundary is only applicable for non-regional googleapis endpoints.
  // All other endpoints would not benefit from the regional access boundary metadata.
  if (authority.find(kRegionalEndpoint) != std::string_view::npos || 
      authority.find(kGoogleApisEndpoint) == std::string_view::npos) {
    return initial_metadata;
  }
  if (lookup_url.empty()) {
    // If we have an empty lookup URL, we cannot fetch the regional access boundary.
    // This can happen if the credential does not have enough information to construct
    // the URL, such as a missing workforce/workload pool ID or service account email.
    return initial_metadata;
  }

  auto request_uri = URI::Parse(lookup_url);

  if (!request_uri.ok()) {
    LOG(ERROR) << "Unable to create URI for the lookup URL: "
               << lookup_url;
    return initial_metadata;
  }

  {
    grpc_core::MutexLock lock(&cache_mu_);
    auto hasValidCache = cache_.has_value() && cache_->isValid();
    bool shouldTriggerAsyncFetch = false;
    if (hasValidCache) {
      initial_metadata->Append(
          GRPC_ALLOWED_LOCATIONS_KEY,
          Slice::FromCopiedString(
              cache_->encoded_locations),
          [](absl::string_view, const Slice&) { abort(); });
      
      if (cache_->isSoftExpired() && !fetch_in_flight_) {
        shouldTriggerAsyncFetch = true;
      }
    }
    
    auto shouldSkipNewLookup = (hasValidCache && !shouldTriggerAsyncFetch) || fetch_in_flight_ || grpc_core::Timestamp::Now() < cooldown_deadline_;
    if (shouldSkipNewLookup) {
      return initial_metadata;
    }
    fetch_in_flight_ = true;
  }

  auto req = MakeRefCounted<RegionalAccessBoundaryRequest>();
  req->uri = std::move(*request_uri);
  auto* caller_pollent = MaybeGetContext<grpc_polling_entity>();
  if (caller_pollent != nullptr) {
    grpc_polling_entity_add_to_pollset_set(
        caller_pollent, grpc_polling_entity_pollset_set(&req->pollent));
  }
  req->access_token = std::move(access_token);

  StartRegionalAccessBoundaryFetch(req);

  // Do not wait for the regional access boundary to be fetched as we do not want to block 
  // the underlying call. Instead, immediately return the initial metadata. The lookup
  // will happen in the background and the result will be cached for future use.
  return initial_metadata;
}

void RegionalAccessBoundaryFetcher::CancelPendingFetch() {
  grpc_core::MutexLock lock(&cache_mu_);
  if (retry_timer_handle_.has_value()) {
    if (event_engine_->Cancel(*retry_timer_handle_)) {
      retry_timer_handle_.reset();
    }
  }
  if (pending_request_ != nullptr) {
    if (pending_request_->http_request != nullptr) {
      pending_request_->http_request.reset();
    }
    pending_request_.reset();
  }
  fetch_in_flight_ = false;
}
}  // namespace grpc_core
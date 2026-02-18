//
// Copyright 2026 gRPC authors.
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

constexpr absl::string_view kAllowedLocationsKey = "x-allowed-locations";
constexpr Duration kRegionalAccessBoundaryBaseCooldownDuration = Duration::Minutes(15);
constexpr Duration kRegionalAccessBoundaryMaxCooldownDuration = Duration::Hours(1);
constexpr int kMaxRegionalAccessBoundaryRetries = 6;
constexpr absl::string_view kRegionalEndpoint = "rep.googleapis.com";
constexpr absl::string_view kGoogleApisEndpoint = "googleapis.com";
// Retryable HTTP Status Codes
constexpr int kInternalServerErrorCode = 500;
constexpr int kBadGatewayErrorCode = 502;
constexpr int kServiceUnavailableErrorCode = 503;
constexpr int kGatewayTimeoutErrorCode = 504;
const int kRetryableStatusCodes[] = {
  kInternalServerErrorCode, kBadGatewayErrorCode,
  kServiceUnavailableErrorCode, kGatewayTimeoutErrorCode};

RegionalAccessBoundaryFetcher::RegionalAccessBoundaryFetcher(
    absl::string_view lookup_url,
    std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine)
    : event_engine_(std::move(event_engine)),
      lookup_url_(lookup_url),
      backoff_(BackOff::Options()
                   .set_initial_backoff(Duration::Seconds(1))
                   .set_multiplier(2.0)
                   .set_jitter(0.2)
                   .set_max_backoff(Duration::Seconds(60))) {
  if (event_engine_ == nullptr) {
    event_engine_ = grpc_event_engine::experimental::GetDefaultEventEngine();
  }
}

RegionalAccessBoundaryFetcher::RegionalAccessBoundaryRequest::
    RegionalAccessBoundaryRequest(grpc_core::URI uri,
                                  absl::string_view access_token)
    : access_token_(access_token), uri_(std::move(uri)) {
  memset(&response_, 0, sizeof(response_));
  pollent_ =
      grpc_polling_entity_create_from_pollset_set(grpc_pollset_set_create());
  grpc_polling_entity* caller_pollent = MaybeGetContext<grpc_polling_entity>();
  if (caller_pollent != nullptr) {
    grpc_polling_entity_add_to_pollset_set(
        caller_pollent, grpc_polling_entity_pollset_set(&pollent_));
  }
}

void RegionalAccessBoundaryFetcher::StartRegionalAccessBoundaryFetch(
    grpc_core::RefCountedPtr<RegionalAccessBoundaryRequest> req) {
  grpc_core::MutexLock lock(&cache_mu_);
  if (pending_request_ != nullptr) { 
    return; 
  }
  grpc_http_request request;
  memset(&request, 0, sizeof(request));
  grpc_http_header header = {const_cast<char*>("Authorization"),
                               const_cast<char*>(req->access_token_.data())};
  request.hdr_count = 1;
  request.hdrs = &header;
  req->fetcher_ = Ref();
  // We pass req.get() as arg to OnRegionalAccessBoundaryResponse. We must
  // manually take a ref because C-callback doesn't. The Ref is consumed in
  // OnRegionalAccessBoundaryResponse.
  req->Ref().release();
  GRPC_CLOSURE_INIT(&req->closure_, OnRegionalAccessBoundaryResponseWrapper,
                    req.get(), grpc_schedule_on_exec_ctx);
  req->http_request_ = grpc_core::HttpRequest::Get(
      req->uri_,
      nullptr,  // channel_args
      &req->pollent_, &request,
      grpc_core::Timestamp::Now() + grpc_core::Duration::Seconds(60),
      &req->closure_,
      &req->response_,
      grpc_core::RefCountedPtr<grpc_channel_credentials>(
          grpc_core::CreateHttpRequestSSLCredentials()));
  pending_request_ = req;
  if (req->http_request_ != nullptr) {
    std::cout << "Starting regional access boundary fetch" << std::endl;
    req->http_request_->Start();
  }
}

void RegionalAccessBoundaryFetcher::OnRegionalAccessBoundaryResponseWrapper(
    void* arg, grpc_error_handle error) {
  grpc_core::RefCountedPtr<RegionalAccessBoundaryRequest> req(
      static_cast<RegionalAccessBoundaryRequest*>(arg));
  req->fetcher_->OnRegionalAccessBoundaryResponse(std::move(req), error);
}

void RegionalAccessBoundaryFetcher::OnRegionalAccessBoundaryResponse(
    grpc_core::RefCountedPtr<RegionalAccessBoundaryRequest> req, grpc_error_handle error) {
  bool should_retry = false;
  bool success = false;
  std::string encoded_locations;
  std::vector<std::string> locations;
  if (error.ok() && req->response_.status == 200) {
    absl::StatusOr<Json> json = grpc_core::JsonParse(
        absl::string_view(req->response_.body, req->response_.body_length));
    if (json.ok() && json->type() == grpc_core::Json::Type::kObject) {
      Json::Object::const_iterator it_encoded = json->object().find("encodedLocations");
      if (it_encoded != json->object().end() &&
          it_encoded->second.type() == grpc_core::Json::Type::kString) {
        encoded_locations = it_encoded->second.string();
      }
      Json::Object::const_iterator it_locations = json->object().find("locations");
      if (it_locations != json->object().end() &&
          it_locations->second.type() == grpc_core::Json::Type::kArray) {
        for (const Json& loc : it_locations->second.array()) {
          if (loc.type() == grpc_core::Json::Type::kString) {
            locations.push_back(loc.string());
          }
        }
      }
      if (!encoded_locations.empty()) {
        success = true;
      }
    }
  }
  if (!success && !absl::IsCancelled(error) &&
      req->num_retries_ < kMaxRegionalAccessBoundaryRetries) {
    // Retry on 5xx HTTP errors
    if (!error.ok()) {
      should_retry = true;
    } else {
      for (int code : kRetryableStatusCodes) {
        if (req->response_.status == code) {
          should_retry = true;
          break;
        }
      }
    }
  }
  grpc_core::MutexLock lock(&cache_mu_);
  if (success) {
    cache_ = {std::move(encoded_locations), std::move(locations),
              grpc_core::Timestamp::Now() +
                  kRegionalAccessBoundaryCacheDuration};
    // On success, reset the cooldown multiplier.
    cooldown_multiplier_ = 1;
    backoff_.Reset();
    pending_request_.reset();
  } else if (should_retry) {
    LOG(WARNING) << "Regional access boundary request will be retried after "
                    "failing with error: "
                  << grpc_core::StatusToString(error)
                  << ", HTTP Status: " << req->response_.status << ", Body: "
                  << absl::string_view(req->response_.body,
                                      req->response_.body_length);
  ++req->num_retries_;
  grpc_core::Duration delay;
  RefCountedPtr<RegionalAccessBoundaryFetcher> self = Ref();
  delay = backoff_.NextAttemptDelay();
  retry_timer_handle_ = event_engine_->RunAfter(delay, [self, req]() {
      self->RetryFetchRegionalAccessBoundary(req.get(), absl::OkStatus());
  });
} else {
    pending_request_.reset();
    // Log failure and set cooldown on failure
    if (!success) {
        LOG(WARNING) << "Regional access boundary request failed. Entering "
                    "cooldown period. Error: "
                << grpc_core::StatusToString(error)
                << ", HTTP Status: " << req->response_.status << ", Body: "
                << absl::string_view(req->response_.body,
                                    req->response_.body_length);
        cooldown_deadline_ = grpc_core::Timestamp::Now() + 
            kRegionalAccessBoundaryBaseCooldownDuration * cooldown_multiplier_;
        if (cooldown_multiplier_ *
                kRegionalAccessBoundaryBaseCooldownDuration <
            kRegionalAccessBoundaryMaxCooldownDuration) {
            cooldown_multiplier_ *= 2;
        }
    }
  }
}

void RegionalAccessBoundaryFetcher::RetryFetchRegionalAccessBoundary(
    RegionalAccessBoundaryRequest* raw_req, grpc_error_handle error) {
  grpc_core::MutexLock lock(&cache_mu_);
  retry_timer_handle_.reset();
  if (error.ok()) {
    RefCountedPtr<RegionalAccessBoundaryRequest> new_req = MakeRefCounted<RegionalAccessBoundaryRequest>(
        raw_req->uri_, raw_req->access_token_);
    new_req->num_retries_ = raw_req->num_retries_;
    new_req->SwapPollent(*raw_req);
    StartRegionalAccessBoundaryFetch(std::move(new_req));
  } else {
    pending_request_.reset();
    cooldown_deadline_ = grpc_core::Timestamp::Now() + 
        kRegionalAccessBoundaryBaseCooldownDuration * cooldown_multiplier_;
    if (cooldown_multiplier_ *
            kRegionalAccessBoundaryBaseCooldownDuration <
        kRegionalAccessBoundaryMaxCooldownDuration) {
        cooldown_multiplier_ *= 2;
    }
  }
}

void RegionalAccessBoundaryFetcher::Fetch(absl::string_view access_token,
                                          ClientMetadata& initial_metadata) {
  const Slice* authority_ptr =
      initial_metadata.get_pointer(HttpAuthorityMetadata());
  if (authority_ptr == nullptr) {
    return;
  }
  std::string_view authority = authority_ptr->as_string_view();
  size_t split = authority.rfind(':');
  if (split != absl::string_view::npos) {
    authority = authority.substr(0, split);
  }
  // Regional access boundary is only applicable for non-regional googleapis
  // endpoints. All other endpoints would not benefit from the regional access
  // boundary metadata.
  bool is_regional = authority == kRegionalEndpoint ||
                     absl::EndsWith(authority, absl::StrCat(".", kRegionalEndpoint));
  if (is_regional) {
    return;
  }
  bool is_googleapis = authority == kGoogleApisEndpoint ||
                       absl::EndsWith(authority, absl::StrCat(".", kGoogleApisEndpoint));
  if (!is_googleapis) {
    return;
  }

  if (lookup_url_.empty()) {
    // If we have an empty lookup URL, we cannot fetch the regional access
    // boundary. This can happen if the credential does not have enough
    // information to construct the URL, such as a missing workforce/workload pool
    // ID or service account email.
    return;
  }
  absl::StatusOr<grpc_core::URI> request_uri = URI::Parse(lookup_url_);
  if (!request_uri.ok()) {
    LOG(ERROR) << "Unable to create URI for the lookup URL: " << lookup_url_;
    return;
  }
  {
    grpc_core::MutexLock lock(&cache_mu_);
    bool has_valid_cache = cache_.has_value() && cache_->IsValid();
    bool cache_is_soft_expired = cache_.has_value() && cache_->IsSoftExpired();
    if (has_valid_cache) {
      std::cout << "Adding cache to metadata: " << cache_->encoded_locations << std::endl;
      initial_metadata.Append(
        kAllowedLocationsKey,
        Slice::FromCopiedString(
            cache_->encoded_locations),
        [](absl::string_view, const Slice&) { abort(); });  
    }
    bool should_skip_new_lookup = (has_valid_cache && !cache_is_soft_expired) || pending_request_ != nullptr || grpc_core::Timestamp::Now() < cooldown_deadline_;
    if (should_skip_new_lookup) {
      return;
    }
  }
  RefCountedPtr<RegionalAccessBoundaryRequest> req = MakeRefCounted<RegionalAccessBoundaryRequest>(
      std::move(*request_uri), access_token);
  // Do not wait for the regional access boundary to be fetched as we do not want to block 
  // the underlying call. 
  StartRegionalAccessBoundaryFetch(req);
}

void RegionalAccessBoundaryFetcher::Orphan() {
  grpc_core::MutexLock lock(&cache_mu_);
  if (retry_timer_handle_.has_value()) {
    if (event_engine_->Cancel(*retry_timer_handle_)) {
      retry_timer_handle_.reset();
    }
  }
  if (pending_request_ != nullptr) {
    if (pending_request_->http_request_ != nullptr) {
      pending_request_->http_request_.reset();
    }
    pending_request_.reset();
  }
}
}  // namespace grpc_core
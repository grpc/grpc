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

#include "src/core/util/host_port.h"
#include "src/core/util/env.h"
#include "src/core/credentials/call/call_credentials.h"
#include "src/core/util/http_client/httpcli_ssl_credentials.h"
#include "src/core/credentials/transport/transport_credentials.h"
#include "src/core/util/json/json.h"
#include "src/core/util/json/json_reader.h"

namespace grpc_core {

namespace {
  constexpr absl::string_view kAllowedLocationsKey = "x-allowed-locations";
  constexpr Duration kRegionalAccessBoundaryBaseCooldownDuration = Duration::Minutes(15);
  constexpr Duration kRegionalAccessBoundaryMaxCooldownDuration = Duration::Hours(1);
  constexpr int kMaxRegionalAccessBoundaryRetries = 6;
  constexpr Duration kRegioanlAccessBoundarySoftCacheGraceDuration = Duration::Hours(1);
  constexpr Duration kRegionalAccessBoundaryCacheDuration = Duration::Hours(6);
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
}

RegionalAccessBoundaryFetcher::RegionalAccessBoundaryFetcher(
    absl::string_view lookup_url,
    std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine,
    std::optional<grpc_core::BackOff::Options> backoff_options)
    : event_engine_(event_engine == nullptr ? grpc_event_engine::experimental::GetDefaultEventEngine() : std::move(event_engine)),
      backoff_(
          backoff_options.has_value()
              ? *backoff_options
              : BackOff::Options()
                    .set_initial_backoff(Duration::Seconds(1))
                    .set_multiplier(2.0)
                    .set_jitter(0.2)
                    .set_max_backoff(Duration::Seconds(60))) {
  if (event_engine_ == nullptr) {
    CHECK(event_engine_ != nullptr);
  }
  if (!lookup_url.empty()) {
    auto parsed_uri = URI::Parse(lookup_url);
    if (parsed_uri.ok()) {
      lookup_uri_ = std::move(*parsed_uri);
    } else {
      LOG(ERROR) << "Unable to create URI for the lookup URL: " << lookup_url;
    }
  }
}

RegionalAccessBoundaryFetcher::Request::
    Request(grpc_core::WeakRefCountedPtr<RegionalAccessBoundaryFetcher> fetcher,
                                  grpc_core::URI uri,
                                  absl::string_view access_token)
    : access_token_(access_token), uri_(std::move(uri)), fetcher_(std::move(fetcher)) {
  memset(&response_, 0, sizeof(response_));
  pollent_ = grpc_polling_entity_create_from_pollset_set(nullptr);
}

void RegionalAccessBoundaryFetcher::Request::Start() {
  grpc_http_request request;
  memset(&request, 0, sizeof(request));
  grpc_http_header header = {const_cast<char*>("Authorization"),
                               const_cast<char*>(access_token_.data())};
  request.hdr_count = 1;
  request.hdrs = &header;
  // We pass this as arg to OnResponseWrapper. We must
  // manually take a ref because C-callback doesn't. The Ref is consumed in
  // OnResponseWrapper.
  Ref().release();
  GRPC_CLOSURE_INIT(&closure_, OnResponseWrapper,
                    this, grpc_schedule_on_exec_ctx);
  http_request_ = grpc_core::HttpRequest::Get(
      uri_,
      nullptr,  // channel_args
      &pollent_, &request,
      grpc_core::Timestamp::Now() + grpc_core::Duration::Seconds(60),
      &closure_,
      &response_,
      grpc_core::RefCountedPtr<grpc_channel_credentials>(
          grpc_core::CreateHttpRequestSSLCredentials()));
  if (http_request_ != nullptr) {
    http_request_->Start();
  }
}

void RegionalAccessBoundaryFetcher::Request::Orphan() {
   http_request_.reset();
   Unref();
}

grpc_core::OrphanablePtr<RegionalAccessBoundaryFetcher::Request> RegionalAccessBoundaryFetcher::Request::MakeRetryRequest() {
    return MakeOrphanable<Request>(fetcher_, uri_, access_token_);
}

void RegionalAccessBoundaryFetcher::Request::OnResponseWrapper(
    void* arg, grpc_error_handle error) {
  grpc_core::RefCountedPtr<Request> req(
      static_cast<Request*>(arg));
  req->OnResponse(error);
}

void RegionalAccessBoundaryFetcher::Request::OnResponse(grpc_error_handle error) {
  bool should_retry = false;
  bool success = false;
  std::string encoded_locations;
  std::vector<std::string> locations;
  if (error.ok() && response_.status == 200) {
    absl::StatusOr<Json> json = grpc_core::JsonParse(
        absl::string_view(response_.body, response_.body_length));
    if (json.ok() && json->type() == grpc_core::Json::Type::kObject) {
      auto it_encoded = json->object().find("encodedLocations");
      if (it_encoded != json->object().end() &&
          it_encoded->second.type() == grpc_core::Json::Type::kString) {
        encoded_locations = it_encoded->second.string();
      }
      auto it_locations = json->object().find("locations");
      if (it_locations != json->object().end() &&
          it_locations->second.type() == grpc_core::Json::Type::kArray) {
        for (auto& loc : it_locations->second.array()) {
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
  auto fetcher = fetcher_->RefIfNonZero();
  if (fetcher != nullptr) {
    if (success) {
      fetcher->OnFetchSuccess(std::move(encoded_locations), std::move(locations));
    } else {
      fetcher->OnFetchFailure(Ref(), error, response_.status, absl::string_view(response_.body, response_.body_length));
    }
  }
}

void RegionalAccessBoundaryFetcher::OnFetchSuccess(std::string encoded_locations, std::vector<std::string> locations) {
  grpc_core::MutexLock lock(&cache_mu_);
  cache_ = {std::move(encoded_locations), std::move(locations),
            grpc_core::Timestamp::Now() +
                kRegionalAccessBoundaryCacheDuration};
  // On success, reset the cooldown multiplier.
  cooldown_multiplier_ = 1;
  backoff_.Reset();
  num_retries_ = 0;
  pending_request_.reset();
}

void RegionalAccessBoundaryFetcher::OnFetchFailure(
    grpc_core::RefCountedPtr<Request> req, grpc_error_handle error,
    int http_status, absl::string_view response_body) {
  grpc_core::MutexLock lock(&cache_mu_);
  bool should_enter_cooldown = true;
  if (!absl::IsCancelled(error) &&
      num_retries_ < kMaxRegionalAccessBoundaryRetries) {
    // Retry on 5xx HTTP errors
    if (!error.ok()) {
      should_enter_cooldown = false;
    } else {
      for (int code : kRetryableStatusCodes) {
        if (http_status == code) {
          should_enter_cooldown = false;
          break;
        }
      }
    }
  }
  if (!should_enter_cooldown) {
    ++num_retries_;
    LOG(WARNING) << "Regional access boundary request will be retried after "
                    "failing with error: "
                 << grpc_core::StatusToString(error)
                 << ", HTTP Status: " << http_status << ", Body: "
                 << response_body;
    next_fetch_time_ = Timestamp::Now() + backoff_.NextAttemptDelay();
  } else {
    LOG(WARNING) << "Regional access boundary request failed. Entering "
                    "cooldown period. Error: "
                 << grpc_core::StatusToString(error)
                 << ", HTTP Status: " << http_status << ", Body: "
                 << response_body;
    pending_request_.reset();
    backoff_.Reset();
    num_retries_ = 0;
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
  if (!lookup_uri_.has_value()) {
    // If we have an empty lookup URL, we cannot fetch the regional access
    // boundary. This can happen if the credential does not have enough
    // information to construct the URL, such as a missing workforce/workload pool
    // ID or service account email.
    return;
  }
  const Slice* authority_ptr =
      initial_metadata.get_pointer(HttpAuthorityMetadata());
  if (authority_ptr == nullptr) {
    return;
  }
  std::string_view authority = authority_ptr->as_string_view();
  absl::string_view host;
  absl::string_view port;
  if (SplitHostPort(authority, &host, &port)) {
    if (!host.empty()) {
      authority = host;
    }
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
  {
    const Timestamp now = Timestamp::Now();
    MutexLock lock(&cache_mu_);
    // We kick off a new fetch attempt if all of the following are true:
    // - We have no cached token, or the cached token's expiration time is less
    //   than the grace period in the future.
    // - There is no pending fetch currently in flight.
    // - We are not currently in backoff after a failed fetch attempt.
    // - We are not currently in cooldown after a failed fetch attempt.
    if ((!cache_.has_value() ||
        (cache_->expiration - now) <=
            kRegioanlAccessBoundarySoftCacheGraceDuration) &&
        pending_request_ == nullptr &&
        next_fetch_time_ <= now &&
        cooldown_deadline_ <= now) {
      pending_request_ = MakeOrphanable<Request>(
          WeakRef(), *lookup_uri_, access_token);
      pending_request_->Start();
    }
    // If we have a cached non-expired token, use it.
    if (cache_.has_value() && cache_->expiration > now) {
      std::cout << "Appending x-allowed-locations header: " << cache_->encoded_locations << std::endl;
      initial_metadata.Append(
          kAllowedLocationsKey,
          Slice::FromCopiedString(
              cache_->encoded_locations),
          [](absl::string_view, const Slice&) { abort(); });
    }
  }
}

void RegionalAccessBoundaryFetcher::Orphaned() {
  grpc_core::MutexLock lock(&cache_mu_);
  if (pending_request_ != nullptr) {
    pending_request_.reset();
  }
}
}  // namespace grpc_core
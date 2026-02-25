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

#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>

#include "src/core/util/host_port.h"
#include "src/core/util/env.h"
#include "src/core/credentials/call/call_credentials.h"
#include "src/core/util/http_client/httpcli_ssl_credentials.h"
#include "src/core/credentials/transport/transport_credentials.h"
#include "src/core/util/json/json.h"
#include "src/core/util/json/json_reader.h"
#include "absl/log/log.h"
#include "absl/strings/str_format.h"

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
  constexpr char kComputeEngineDefaultSaEmailPath[] =
    "/computeMetadata/v1/instance/service-accounts/default/email";
}

// static
RefCountedPtr<RegionalAccessBoundaryFetcher>
RegionalAccessBoundaryFetcher::Create(
    absl::string_view lookup_url,
    std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine,
    std::optional<grpc_core::BackOff::Options> backoff_options) {
  auto uri = URI::Parse(lookup_url);
  if (!uri.ok()) {
    LOG(WARNING) << "Invalid RegionalAccessBoundary lookup URI \"" << lookup_url
                 << "\" (" << uri.status() << "); RAB data will not be fetched";
    return nullptr;
  }
  return MakeRefCounted<RegionalAccessBoundaryFetcher>(
      std::move(*uri), std::move(event_engine), backoff_options);
}

RegionalAccessBoundaryFetcher::RegionalAccessBoundaryFetcher(
    grpc_core::URI lookup_uri,
    std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine,
    std::optional<grpc_core::BackOff::Options> backoff_options)
    : event_engine_(event_engine == nullptr
                        ? grpc_event_engine::experimental::GetDefaultEventEngine()
                        : std::move(event_engine)),
      lookup_uri_(std::move(lookup_uri)),
      backoff_(
          backoff_options.has_value()
              ? *backoff_options
              : BackOff::Options()
                    .set_initial_backoff(Duration::Seconds(1))
                    .set_multiplier(2.0)
                    .set_jitter(0.2)
                    .set_max_backoff(Duration::Seconds(60))) {
  CHECK(event_engine_ != nullptr);
}

void RegionalAccessBoundaryFetcher::OnFetchSuccess(std::string encoded_locations, std::vector<std::string> locations) {
  grpc_core::MutexLock lock(&cache_mu_);
  if (shutdown_) return;
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
  if (shutdown_) return;
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
  pending_request_.reset();
}

void RegionalAccessBoundaryFetcher::Fetch(absl::string_view access_token,
                                          ClientMetadata& initial_metadata) {
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
      pending_request_ = MakeOrphanable<Request>(WeakRef(), access_token);
      pending_request_->Start();
    }
    // If we have a cached non-expired token, use it.
    if (cache_.has_value() && cache_->expiration > now) {
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
  shutdown_ = true;
  pending_request_.reset();
}

RegionalAccessBoundaryFetcher::Request::
    Request(grpc_core::WeakRefCountedPtr<RegionalAccessBoundaryFetcher> fetcher,
                                  absl::string_view access_token)
    : access_token_(access_token), fetcher_(std::move(fetcher)) {
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
      fetcher_->lookup_uri_,
      nullptr,  // channel_args
      &pollent_, &request,
      grpc_core::Timestamp::Now() + grpc_core::Duration::Seconds(60),
      &closure_,
      &response_,
      grpc_core::RefCountedPtr<grpc_channel_credentials>(
          grpc_core::CreateHttpRequestSSLCredentials()));
  http_request_->Start();
}

void RegionalAccessBoundaryFetcher::Request::Orphan() {
   http_request_.reset();
   Unref();
}

void RegionalAccessBoundaryFetcher::Request::OnResponseWrapper(
    void* arg, grpc_error_handle error) {
  grpc_core::RefCountedPtr<Request> req(
      static_cast<Request*>(arg));
  req->OnResponse(error);
}

void RegionalAccessBoundaryFetcher::Request::OnResponse(grpc_error_handle error) {
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
  if (success) {
    fetcher_->OnFetchSuccess(std::move(encoded_locations), std::move(locations));
  } else {
    fetcher_->OnFetchFailure(Ref(), error, response_.status, absl::string_view(response_.body, response_.body_length));
  }
}

class EmailFetcher::EmailRequest final : public InternallyRefCounted<EmailRequest> {
 public:
  explicit EmailRequest(WeakRefCountedPtr<EmailFetcher> fetcher)
      : fetcher_(std::move(fetcher)) {
    pollent_ = grpc_polling_entity_create_from_pollset_set(nullptr);
  }

  ~EmailRequest() override { grpc_http_response_destroy(&response_); }

  void Start() {
    grpc_http_header header = {const_cast<char*>("Metadata-Flavor"),
                               const_cast<char*>("Google")};
    grpc_http_request request;
    memset(&request, 0, sizeof(grpc_http_request));
    request.hdr_count = 1;
    request.hdrs = &header;
    auto uri = URI::Create("http", /*user_info=*/"",
                           GRPC_COMPUTE_ENGINE_METADATA_HOST,
                           kComputeEngineDefaultSaEmailPath, /*query_params=*/{},
                           /*fragment=*/"");
    GRPC_CHECK(uri.ok());
    GRPC_CLOSURE_INIT(&closure_, OnResponseWrapper, this,
                      grpc_schedule_on_exec_ctx);
    http_request_ = HttpRequest::Get(
        std::move(*uri), /*args=*/nullptr, &pollent_, &request,
        Timestamp::Now() + Duration::Seconds(60), &closure_, &response_,
        RefCountedPtr<grpc_channel_credentials>(
            grpc_insecure_credentials_create()));
    Ref().release();  // Ref held by HTTP request callback.
    http_request_->Start();
  }

  void Orphan() override {
    http_request_.reset();
    Unref();
  }

 private:
  static void OnResponseWrapper(void* arg, grpc_error_handle error) {
    RefCountedPtr<EmailRequest> req(static_cast<EmailRequest*>(arg));
    // RefCountedPtr adopts the reference from Start().
    req->OnResponse(error);
  }

  void OnResponse(grpc_error_handle error) {
    if (!error.ok()) {
      fetcher_->OnEmailFetchError(error);
    } else if (response_.status != 200) {
      fetcher_->OnEmailFetchError(GRPC_ERROR_CREATE(
          absl::StrCat("Failed to fetch service account email: HTTP ",
                       response_.status)));
    } else {
      fetcher_->OnEmailFetchComplete(
          absl::string_view(response_.body, response_.body_length));
    }
  }

  grpc_http_response response_;
  OrphanablePtr<HttpRequest> http_request_;
  grpc_polling_entity pollent_;
  WeakRefCountedPtr<EmailFetcher> fetcher_;
  grpc_closure closure_;
};

EmailFetcher::EmailFetcher(
    std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine)
    : event_engine_(event_engine == nullptr
                        ? grpc_event_engine::experimental::GetDefaultEventEngine()
                        : std::move(event_engine)) {}

void EmailFetcher::StartEmailFetch() {
  MutexLock lock(&mu_);
  if (Timestamp::Now() < next_fetch_earliest_time_) {
    return;
  }
  // Check if we are in the initial/retryable state (null EmailRequest)
  if (auto* pending = std::get_if<OrphanablePtr<EmailRequest>>(&state_)) {
    if (*pending != nullptr) {
      return;  // Already fetching email
    }
  } else {
    return;  // Already have RAB fetcher
  }

  auto request = MakeOrphanable<EmailRequest>(WeakRef());
  // We keep a temporary ref to start it, but transfer ownership to state_ first.
  auto* request_ptr = request.get();
  state_ = std::move(request);
  request_ptr->Start();
}

void EmailFetcher::Fetch(absl::string_view token,
                         ClientMetadata& initial_metadata) {
  MutexLock lock(&mu_);
  if (auto* rab_fetcher =
          std::get_if<RefCountedPtr<RegionalAccessBoundaryFetcher>>(&state_)) {
    (*rab_fetcher)->Fetch(token, initial_metadata);
  }
}

EmailFetcher::~EmailFetcher() = default;

void EmailFetcher::Orphaned() {
  MutexLock lock(&mu_);
  state_ = OrphanablePtr<EmailRequest>(nullptr);
}

void EmailFetcher::OnEmailFetchComplete(absl::string_view email) {
  MutexLock lock(&mu_);
  if (std::holds_alternative<OrphanablePtr<EmailRequest>>(state_)) {
      std::string rab_url = absl::StrFormat(
          "https://iamcredentials.googleapis.com/v1/projects/-/serviceAccounts/"
          "%s/allowedLocations",
          email);
      auto rab_fetcher = RegionalAccessBoundaryFetcher::Create(
          rab_url, event_engine_);
      if (rab_fetcher != nullptr) {
        state_ = std::move(rab_fetcher);
      } else {
        state_ = OrphanablePtr<EmailRequest>(nullptr);
      }
  }
}

void EmailFetcher::OnEmailFetchError(grpc_error_handle error) {
  MutexLock lock(&mu_);
  if (std::holds_alternative<OrphanablePtr<EmailRequest>>(state_)) {
    LOG_EVERY_N_SEC(ERROR, 60)
        << "Regional Access Boundary fetch skipped due to service account email "
           "fetch failure: "
        << StatusToString(error);
    state_ = OrphanablePtr<EmailRequest>(nullptr);  // Reset to allow retry on next token fetch.
    next_fetch_earliest_time_ = Timestamp::Now() + backoff_.NextAttemptDelay();
  }
}
}  // namespace grpc_core

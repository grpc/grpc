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

#include "src/core/credentials/call/call_credentials.h"
#include "src/core/credentials/transport/transport_credentials.h"
#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/util/env.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/host_port.h"
#include "src/core/util/http_client/httpcli_ssl_credentials.h"
#include "src/core/util/json/json.h"
#include "src/core/util/json/json_reader.h"
#include "src/core/util/status_helper.h"
#include "src/core/util/uri.h"
#include "absl/log/log.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"

namespace grpc_core {

namespace {
constexpr absl::string_view kAllowedLocationsKey = "x-allowed-locations";
constexpr Duration kRegionalAccessBoundarySoftCacheGraceDuration =
    Duration::Hours(1);
constexpr Duration kRegionalAccessBoundaryCacheDuration = Duration::Hours(6);
constexpr absl::string_view kRegionalEndpoint = "rep.googleapis.com";
constexpr char kComputeEngineDefaultSaEmailPath[] =
    "/computeMetadata/v1/instance/service-accounts/default/email";
constexpr char kComputeEngineUniverseDomainPath[] =
    "/computeMetadata/v1/universe/universe-domain";
constexpr absl::string_view kDefaultUniverseDomain = "googleapis.com";
}  // namespace

RefCountedPtr<RegionalAccessBoundaryFetcher>
RegionalAccessBoundaryFetcher::Create(
    absl::string_view lookup_url,
    std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine,
    std::optional<BackOff::Options> backoff_options) {
  auto uri = URI::Parse(lookup_url);
  if (!uri.ok()) {
    LOG(INFO) << "Invalid RegionalAccessBoundary lookup URI \"" << lookup_url
              << "\" (" << uri.status() << "); RAB data will not be fetched";
    return nullptr;
  }
  return MakeRefCounted<RegionalAccessBoundaryFetcher>(
      std::move(*uri), std::move(event_engine), backoff_options);
}

RegionalAccessBoundaryFetcher::RegionalAccessBoundaryFetcher(
    URI lookup_uri,
    std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine,
    std::optional<BackOff::Options> backoff_options)
    : event_engine_(
          event_engine == nullptr
              ? grpc_event_engine::experimental::GetDefaultEventEngine()
              : std::move(event_engine)),
      lookup_uri_(std::move(lookup_uri)),
      backoff_(backoff_options.has_value()
                   ? *backoff_options
                   : BackOff::Options()
                         .set_initial_backoff(Duration::Seconds(1))
                         .set_multiplier(2.0)
                         .set_jitter(0.2)
                         .set_max_backoff(Duration::Seconds(60))) {
  GRPC_CHECK(event_engine_ != nullptr);
}

void RegionalAccessBoundaryFetcher::OnFetchSuccess(Slice encoded_locations) {
  MutexLock lock(cache_mu_);
  if (shutdown_) return;
  cache_ = {std::move(encoded_locations),
            Timestamp::Now() + kRegionalAccessBoundaryCacheDuration};
  backoff_.Reset();
  pending_request_.reset();
}

void RegionalAccessBoundaryFetcher::OnFetchFailure(
    grpc_error_handle error, int http_status, absl::string_view response_body) {
  MutexLock lock(cache_mu_);
  if (shutdown_) return;
  auto log_message = absl::StrCat(
      "Regional access boundary request will be retried after failing with "
      "error: ",
      StatusToString(error), ", HTTP Status: ", http_status);
  if (response_body.data() != nullptr) {
    absl::StrAppend(&log_message, ", Body: ", response_body);
  }
  LOG(INFO) << log_message;
  next_fetch_time_ = Timestamp::Now() + backoff_.NextAttemptDelay();
  pending_request_.reset();
}

void RegionalAccessBoundaryFetcher::Fetch(absl::string_view access_token,
                                          ClientMetadata& initial_metadata) {
  const Slice* authority_ptr =
      initial_metadata.get_pointer(HttpAuthorityMetadata());
  if (authority_ptr == nullptr) {
    return;
  }
  absl::string_view authority = authority_ptr->as_string_view();
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
  bool is_regional =
      authority == kRegionalEndpoint ||
      absl::EndsWith(authority, absl::StrCat(".", kRegionalEndpoint));
  if (is_regional) {
    return;
  }
  {
    const Timestamp now = Timestamp::Now();
    MutexLock lock(cache_mu_);
    // We kick off a new fetch attempt if all of the following are true:
    // - We have no cached Regioanl Access Boundary data, or the cached
    //   Regional Access Boundary data's expiration time is less than the grace
    //   period in the future.
    // - There is no pending fetch currently in flight.
    // - We are not currently in backoff after a failed fetch attempt.
    if ((!cache_.has_value() ||
         cache_->expiration <=
             now + kRegionalAccessBoundarySoftCacheGraceDuration) &&
        pending_request_ == nullptr && next_fetch_time_ <= now) {
      pending_request_ = MakeOrphanable<Request>(WeakRef(), access_token);
      pending_request_->Start();
    }
    // If we have cached non-expired Regional Access Boundary data, use it.
    if (cache_.has_value() && cache_->expiration > now) {
      initial_metadata.Append(
          kAllowedLocationsKey, cache_->encoded_locations.Ref(),
          [](absl::string_view, const Slice&) {
            LOG_EVERY_N_SEC(WARNING, 60)
                << "Regional access boundary header could not be appended";
          });
    }
  }
}

void RegionalAccessBoundaryFetcher::Orphaned() {
  MutexLock lock(cache_mu_);
  shutdown_ = true;
  pending_request_.reset();
}

RegionalAccessBoundaryFetcher::Request::Request(
    WeakRefCountedPtr<RegionalAccessBoundaryFetcher> fetcher,
    absl::string_view access_token)
    : access_token_(access_token), fetcher_(std::move(fetcher)) {
  memset(&response_, 0, sizeof(response_));
  pollent_ =
      grpc_polling_entity_create_from_pollset_set(grpc_pollset_set_create());
}

RegionalAccessBoundaryFetcher::Request::~Request() {
  grpc_http_response_destroy(&response_);
  grpc_pollset_set_destroy(grpc_polling_entity_pollset_set(&pollent_));
}

void RegionalAccessBoundaryFetcher::Request::Start() {
  grpc_http_request request;
  memset(&request, 0, sizeof(grpc_http_request));
  grpc_http_header header = {const_cast<char*>("Authorization"),
                             const_cast<char*>(access_token_.c_str())};
  request.hdr_count = 1;
  request.hdrs = &header;
  // We pass this as arg to OnResponseWrapper. We must
  // manually take a ref because C-callback doesn't. The Ref is consumed in
  // OnResponseWrapper.
  Ref().release();
  GRPC_CLOSURE_INIT(&closure_, OnResponseWrapper, this,
                    grpc_schedule_on_exec_ctx);
  http_request_ = HttpRequest::Get(fetcher_->lookup_uri_,
                                   nullptr,  // channel_args
                                   &pollent_, &request,
                                   Timestamp::Now() + Duration::Seconds(60),
                                   &closure_, &response_,
                                   RefCountedPtr<grpc_channel_credentials>(
                                       CreateHttpRequestSSLCredentials()));
  http_request_->Start();
}

void RegionalAccessBoundaryFetcher::Request::Orphan() {
  http_request_.reset();
  Unref();
}

void RegionalAccessBoundaryFetcher::Request::OnResponseWrapper(
    void* arg, grpc_error_handle error) {
  RefCountedPtr<Request> req(static_cast<Request*>(arg));
  req->OnResponse(error);
}

void RegionalAccessBoundaryFetcher::Request::OnResponse(
    grpc_error_handle error) {
  bool success = false;
  std::string encoded_locations;
  absl::string_view response_body;
  if (response_.body != nullptr) {
    response_body = absl::string_view(response_.body, response_.body_length);
  }
  if (error.ok() && response_.status == 200) {
    absl::StatusOr<Json> json = JsonParse(response_body);
    if (json.ok() && json->type() == Json::Type::kObject) {
      auto it_encoded = json->object().find("encodedLocations");
      if (it_encoded != json->object().end() &&
          it_encoded->second.type() == Json::Type::kString) {
        encoded_locations = it_encoded->second.string();
      }
      if (!encoded_locations.empty()) {
        success = true;
      }
    }
  }
  if (success) {
    fetcher_->OnFetchSuccess(Slice::FromCopiedString(encoded_locations));
  } else {
    fetcher_->OnFetchFailure(error, response_.status, response_body);
  }
}

class MetadataFetcher::Request final : public InternallyRefCounted<Request> {
 public:
  enum class Type { kEmail, kUniverseDomain };

  Request(WeakRefCountedPtr<MetadataFetcher> fetcher, Type type)
      : fetcher_(std::move(fetcher)), type_(type) {
    memset(&response_, 0, sizeof(response_));
    pollent_ =
        grpc_polling_entity_create_from_pollset_set(grpc_pollset_set_create());
  }

  ~Request() override {
    grpc_http_response_destroy(&response_);
    grpc_pollset_set_destroy(grpc_polling_entity_pollset_set(&pollent_));
  }

  void Start() {
    grpc_http_header header = {const_cast<char*>("Metadata-Flavor"),
                               const_cast<char*>("Google")};
    grpc_http_request request = {};
    request.hdr_count = 1;
    request.hdrs = &header;
    const char* path = type_ == Type::kEmail
                           ? kComputeEngineDefaultSaEmailPath
                           : kComputeEngineUniverseDomainPath;
    auto uri = URI::Create(
        "http", /*user_info=*/"", GRPC_COMPUTE_ENGINE_METADATA_HOST, path,
        /*query_parameter_pairs=*/{}, /*fragment=*/"");
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
    RefCountedPtr<Request> req(static_cast<Request*>(arg));
    // RefCountedPtr adopts the reference from Start().
    req->OnResponse(error);
  }

  void OnResponse(grpc_error_handle error) {
    absl::string_view response_body;
    if (response_.body != nullptr) {
      response_body = absl::string_view(response_.body, response_.body_length);
    }
    if (type_ == Type::kEmail) {
      if (!error.ok()) {
        fetcher_->OnEmailFetchError(error);
      } else if (response_.status != 200) {
        fetcher_->OnEmailFetchError(GRPC_ERROR_CREATE(absl::StrCat(
            "Failed to fetch service account email: HTTP ", response_.status)));
      } else {
        fetcher_->OnEmailFetchComplete(response_body);
      }
    } else {
      if (!error.ok()) {
        fetcher_->OnUniverseDomainFetchError(error);
      } else if (response_.status == 404) {
        fetcher_->OnUniverseDomainFetchComplete("");
      } else if (response_.status != 200) {
        fetcher_->OnUniverseDomainFetchError(GRPC_ERROR_CREATE(
            absl::StrCat("Failed to fetch universe domain: HTTP ",
                         response_.status)));
      } else {
        fetcher_->OnUniverseDomainFetchComplete(response_body);
      }
    }
  }

  grpc_http_response response_;
  OrphanablePtr<HttpRequest> http_request_;
  grpc_polling_entity pollent_;
  WeakRefCountedPtr<MetadataFetcher> fetcher_;
  const Type type_;
  grpc_closure closure_;
};

MetadataFetcher::MetadataFetcher(
    std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine)
    : event_engine_(
          event_engine == nullptr
              ? grpc_event_engine::experimental::GetDefaultEventEngine()
              : std::move(event_engine)) {}

MetadataFetcher::~MetadataFetcher() = default;

void MetadataFetcher::StartMetadataFetch() {
  MutexLock lock(mu_);
  if (Timestamp::Now() < next_fetch_earliest_time_) {
    return;
  }
  // Check if we are in the initial/retryable state (PendingRequests).
  auto* pending = std::get_if<PendingRequests>(&state_);
  if (pending == nullptr) return;  // Already have RAB fetcher.
  if (pending->email_request != nullptr ||
      pending->universe_domain_request != nullptr) {
    return;  // Metadata fetch already in progress.
  }
  if (!email_.has_value()) {
    pending->email_request =
        MakeOrphanable<Request>(WeakRef(), Request::Type::kEmail);
  }
  if (!is_gdu_.has_value()) {
    pending->universe_domain_request =
        MakeOrphanable<Request>(WeakRef(), Request::Type::kUniverseDomain);
  }
  if (pending->email_request != nullptr) {
    pending->email_request->Start();
  }
  if (pending->universe_domain_request != nullptr) {
    pending->universe_domain_request->Start();
  }
}

void MetadataFetcher::Fetch(absl::string_view token,
                            ClientMetadata& initial_metadata) {
  MutexLock lock(mu_);
  auto* rab_fetcher =
      std::get_if<RefCountedPtr<RegionalAccessBoundaryFetcher>>(&state_);
  if (rab_fetcher == nullptr) return;   // Don't yet have RAB fetcher.
  if (*rab_fetcher == nullptr) return;  // Shouldn't happen, but be defensive.
  (*rab_fetcher)->Fetch(token, initial_metadata);
}

void MetadataFetcher::Orphaned() {
  MutexLock lock(mu_);
  state_ = RefCountedPtr<RegionalAccessBoundaryFetcher>(nullptr);
}

void MetadataFetcher::OnEmailFetchComplete(absl::string_view email) {
  MutexLock lock(mu_);
  auto* pending = std::get_if<PendingRequests>(&state_);
  if (pending == nullptr) return;
  pending->email_request.reset();
  absl::string_view trimmed_email = absl::StripAsciiWhitespace(email);
  if (!IsValidEmail(trimmed_email)) {
    LOG(INFO) << "Regional Access Boundary fetch skipped: service account "
                 "email is not a valid email address (could be a principal "
                 "string or pool id): \""
              << trimmed_email << "\"";
    email_ = "";
  } else {
    email_ = std::string(trimmed_email);
  }
  MaybeFinishMetadataFetch();
}

void MetadataFetcher::OnEmailFetchError(grpc_error_handle error) {
  MutexLock lock(mu_);
  auto* pending = std::get_if<PendingRequests>(&state_);
  if (pending == nullptr) return;
  pending->email_request.reset();
  pending->error = error;
  MaybeFinishMetadataFetch();
}

void MetadataFetcher::OnUniverseDomainFetchComplete(
    absl::string_view universe_domain) {
  MutexLock lock(mu_);
  auto* pending = std::get_if<PendingRequests>(&state_);
  if (pending == nullptr) return;
  pending->universe_domain_request.reset();
  absl::string_view trimmed_domain =
      absl::StripAsciiWhitespace(universe_domain);
  is_gdu_ = trimmed_domain.empty() || trimmed_domain == kDefaultUniverseDomain;
  if (!*is_gdu_) {
    LOG(INFO) << "Regional Access Boundary fetch skipped for non-GDU "
                 "universe domain: \""
              << trimmed_domain << "\"";
  }
  MaybeFinishMetadataFetch();
}

void MetadataFetcher::OnUniverseDomainFetchError(grpc_error_handle error) {
  MutexLock lock(mu_);
  auto* pending = std::get_if<PendingRequests>(&state_);
  if (pending == nullptr) return;
  pending->universe_domain_request.reset();
  pending->error = error;
  MaybeFinishMetadataFetch();
}

void MetadataFetcher::MaybeFinishMetadataFetch() {
  auto* pending = std::get_if<PendingRequests>(&state_);
  if (pending == nullptr) return;
  if (pending->email_request != nullptr ||
      pending->universe_domain_request != nullptr) {
    return;
  }
  if ((email_.has_value() && email_->empty()) ||
      (is_gdu_.has_value() && !*is_gdu_)) {
    state_ = RefCountedPtr<RegionalAccessBoundaryFetcher>(nullptr);
    return;
  }
  if (email_.has_value() && is_gdu_.has_value() && *is_gdu_) {
    std::string rab_url = absl::StrFormat(
        "https://iamcredentials.googleapis.com/v1/projects/-/serviceAccounts/"
        "%s/allowedLocations",
        *email_);
    auto rab_fetcher =
        RegionalAccessBoundaryFetcher::Create(rab_url, event_engine_);
    if (rab_fetcher != nullptr) {
      state_ = std::move(rab_fetcher);
    } else {
      state_ = RefCountedPtr<RegionalAccessBoundaryFetcher>(nullptr);
    }
    return;
  }
  LOG_EVERY_N_SEC(INFO, 60) << "Regional Access Boundary fetch skipped due "
                               "to metadata fetch failure: "
                            << StatusToString(pending->error);
  pending->error = absl::OkStatus();
  next_fetch_earliest_time_ = Timestamp::Now() + backoff_.NextAttemptDelay();
}

bool MetadataFetcher::IsValidEmail(absl::string_view email) {
  size_t at_pos = email.find('@');
  if (at_pos == absl::string_view::npos || at_pos == 0 ||
      at_pos == email.size() - 1) {
    return false;
  }
  if (email.find('@', at_pos + 1) != absl::string_view::npos) {
    return false;
  }
  absl::string_view domain = email.substr(at_pos + 1);
  size_t dot_pos = domain.find('.');
  while (dot_pos != absl::string_view::npos) {
    if (dot_pos > 0 && dot_pos < domain.size() - 1) {
      return true;
    }
    dot_pos = domain.find('.', dot_pos + 1);
  }
  return false;
}
}  // namespace grpc_core

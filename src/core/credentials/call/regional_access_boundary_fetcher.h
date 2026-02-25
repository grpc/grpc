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

#ifndef GRPC_SRC_CORE_CREDENTIALS_CALL_REGIONAL_ACCESS_BOUNDARY_FETCHER_H
#define GRPC_SRC_CORE_CREDENTIALS_CALL_REGIONAL_ACCESS_BOUNDARY_FETCHER_H

#include <grpc/support/port_platform.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>

#include <string>
#include <vector>
#include <optional>

#include <grpc/event_engine/event_engine.h>
#include "absl/status/statusor.h"
#include "src/core/call/metadata.h"
#include "src/core/util/dual_ref_counted.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/backoff.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/util/http_client/httpcli.h"

namespace grpc_core {

class RegionalAccessBoundaryFetcher final : public DualRefCounted<RegionalAccessBoundaryFetcher> {
 public:
  static grpc_core::RefCountedPtr<RegionalAccessBoundaryFetcher> Create(
      absl::string_view lookup_url,
      std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine = nullptr,
      std::optional<grpc_core::BackOff::Options> backoff_options = std::nullopt);

  explicit RegionalAccessBoundaryFetcher(
      grpc_core::URI lookup_uri,
      std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine = nullptr,
      std::optional<grpc_core::BackOff::Options> backoff_options = std::nullopt);

   // Attaches regional access boundary header (x-allowed-locations) to the initial metadata 
   // if available, otherwise initiates non-blocking, asynchronous fetch of regional access
   // boundary if not already cached or in flight.
  void Fetch(
      absl::string_view access_token,
      ClientMetadata& initial_metadata);

  // Cancels any pending fetch of regional access boundary which must be called during
  // destruction of any CallCredential which supports regional access boundary to
  // avoid memory leaks from pending http requests.
  void Orphaned() override;

 private:
  friend class RegionalAccessBoundaryFetcherTest;

  struct RegionalAccessBoundary {
    std::string encoded_locations;
    std::vector<std::string> locations;
    grpc_core::Timestamp expiration;
  };

  class Request;

  void OnFetchSuccess(std::string encoded_locations, std::vector<std::string> locations);
  void OnFetchFailure(grpc_core::RefCountedPtr<Request> req, grpc_error_handle error, int http_status, absl::string_view response_body);

  std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine_;
  grpc_core::URI lookup_uri_;
  grpc_core::Mutex cache_mu_;
  std::optional<RegionalAccessBoundary> cache_ ABSL_GUARDED_BY(&cache_mu_) ;
  Timestamp next_fetch_time_ ABSL_GUARDED_BY(&cache_mu_) = Timestamp::InfPast();
  int cooldown_multiplier_ ABSL_GUARDED_BY(&cache_mu_) = 1;
  grpc_core::Timestamp cooldown_deadline_ ABSL_GUARDED_BY(&cache_mu_) = grpc_core::Timestamp::ProcessEpoch();
  grpc_core::BackOff backoff_ ABSL_GUARDED_BY(&cache_mu_);
  int num_retries_ ABSL_GUARDED_BY(&cache_mu_) = 0;
  grpc_core::OrphanablePtr<Request> pending_request_ ABSL_GUARDED_BY(&cache_mu_);
  bool shutdown_ ABSL_GUARDED_BY(&cache_mu_) = false;
};

class RegionalAccessBoundaryFetcher::Request final
: public grpc_core::InternallyRefCounted<Request> {
 public:
  Request(grpc_core::WeakRefCountedPtr<RegionalAccessBoundaryFetcher> fetcher,
          absl::string_view access_token);

  ~Request() override {
    grpc_http_response_destroy(&response_);
  }

  void Start();

  // Cancels any pending http request which must be called during
  // destruction to avoid memory leaks from pending http requests.
  void Orphan() override;

 private:

  static void OnResponseWrapper(void* arg, grpc_error_handle error);
  void OnResponse(grpc_error_handle error);

  grpc_http_response response_;
  grpc_core::OrphanablePtr<grpc_core::HttpRequest> http_request_;
  std::string access_token_;
  grpc_polling_entity pollent_;
  grpc_core::WeakRefCountedPtr<RegionalAccessBoundaryFetcher> fetcher_;
  grpc_closure closure_;
};

class EmailFetcher final : public DualRefCounted<EmailFetcher> {
 public:
  explicit EmailFetcher(
      std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine = nullptr);

  void StartEmailFetch();

  // Wrapper for RAB fetcher.
  void Fetch(absl::string_view token, ClientMetadata& metadata);

  ~EmailFetcher() override;

  void Orphaned() override;

 private:
  class EmailRequest;

  void OnEmailFetchComplete(absl::string_view email);
  void OnEmailFetchError(grpc_error_handle error);

  std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine_;
  grpc_core::Mutex mu_;

  // Either a pending email request or an RAB fetcher.
  std::variant<OrphanablePtr<EmailRequest>, RefCountedPtr<RegionalAccessBoundaryFetcher>> state_
      ABSL_GUARDED_BY(&mu_);
  BackOff backoff_ ABSL_GUARDED_BY(&mu_) = BackOff(
      BackOff::Options()
          .set_initial_backoff(Duration::Seconds(1))
          .set_multiplier(1.6)
          .set_jitter(0.2)
          .set_max_backoff(Duration::Seconds(60)));
  Timestamp next_fetch_earliest_time_ ABSL_GUARDED_BY(&mu_) = Timestamp::InfPast();
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_CREDENTIALS_CALL_REGIONAL_ACCESS_BOUNDARY_FETCHER_H
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
#include "src/core/lib/promise/arena_promise.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/backoff.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/util/http_client/httpcli.h"

#define GRPC_REGIONAL_ACCESS_BOUNDARY_CACHE_DURATION_SECS 21600 // 6 hours, in seconds
#define GRPC_REGIONAL_ACCESS_BOUNDARY_BASE_COOLDOWN_DURATION_SECS 900 // 15 minutes, in seconds
#define GRPC_REGIONAL_ACCESS_BOUNDARY_MAX_COOLDOWN_DURATION_SECS 3600 // 60 minutes, in seconds
#define GRPC_ALLOWED_LOCATIONS_KEY "x-allowed-locations"

struct RegionalAccessBoundary {
  std::string encoded_locations;
  std::vector<std::string> locations;
  grpc_core::Timestamp expiration = grpc_core::Timestamp::Now() + 
    grpc_core::Duration::Seconds(GRPC_REGIONAL_ACCESS_BOUNDARY_CACHE_DURATION_SECS);

  bool isValid() const {
    return expiration > grpc_core::Timestamp::Now();
  }
};

namespace grpc_core {

class RegionalAccessBoundaryFetcher : public RefCounted<RegionalAccessBoundaryFetcher> {
 public:
  RegionalAccessBoundaryFetcher();
  ArenaPromise<absl::StatusOr<ClientMetadataHandle>> Fetch(
      std::string lookup_url,
      ClientMetadataHandle initial_metadata);

  void InvalidateCache();
  void Cancel();

 private:

  struct RegionalAccessBoundaryRequest;

  void StartRegionalAccessBoundaryFetch(grpc_core::RefCountedPtr<RegionalAccessBoundaryRequest> req);
  
  void OnRegionalAccessBoundaryResponse(void* arg, grpc_error_handle error);
  static void OnRegionalAccessBoundaryResponseWrapper(void* arg, grpc_error_handle error);
  void RetryFetchRegionalAccessBoundary(void* arg, grpc_error_handle error);

  std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine_ = 
    grpc_event_engine::experimental::GetDefaultEventEngine();
  std::optional<grpc_event_engine::experimental::EventEngine::TaskHandle> retry_timer_handle_;
  grpc_core::Mutex cache_mu_;
  std::optional<RegionalAccessBoundary> cache_;
  bool fetch_in_flight_ = false;
  int cooldown_multiplier_ = 1;
  grpc_core::Timestamp cooldown_deadline_ = grpc_core::Timestamp::ProcessEpoch();
  grpc_core::BackOff backoff_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_CREDENTIALS_CALL_REGIONAL_ACCESS_BOUNDARY_FETCHER_H
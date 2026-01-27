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

#include "absl/status/statusor.h"
#include "src/core/call/metadata.h"
#include "src/core/lib/promise/arena_promise.h"
#include "src/core/util/ref_counted_ptr.h"

#define GRPC_REGIONAL_ACCESS_BOUNDARY_CACHE_DURATION_SECS 21600 // 6 hours, in seconds
#define GRPC_REGIONAL_ACCESS_BOUNDARY_BASE_COOLDOWN_DURATION_SECS 900 // 15 minutes, in seconds
#define GRPC_REGIONAL_ACCESS_BOUNDARY_MAX_COOLDOWN_DURATION_SECS 3600 // 60 minutes, in seconds
#define GRPC_ALLOWED_LOCATIONS_KEY "x-allowed-locations"

struct grpc_call_credentials;

struct RegionalAccessBoundary {
  std::string encoded_locations;
  std::vector<std::string> locations;
  gpr_timespec expiration = gpr_time_add(gpr_now(GPR_CLOCK_REALTIME), gpr_time_from_seconds(
          GRPC_REGIONAL_ACCESS_BOUNDARY_CACHE_DURATION_SECS, GPR_TIMESPAN));

  bool isValid() const {
    // 0 because we do not allow any grace after the expiration time passes
    gpr_timespec grace_period = gpr_time_from_seconds(0, GPR_TIMESPAN);
    return gpr_time_cmp(
              gpr_time_sub(expiration, gpr_now(GPR_CLOCK_REALTIME)),
              grace_period) > 0;
  }
};

namespace grpc_core {

class RegionalAccessBoundaryFetcher {
 public:
  RegionalAccessBoundaryFetcher();
  ~RegionalAccessBoundaryFetcher();

  ArenaPromise<absl::StatusOr<ClientMetadataHandle>> Fetch(
      RefCountedPtr<grpc_call_credentials> creds,
      ClientMetadataHandle initial_metadata);

  void InvalidateCache();

 private:
  friend class RegionalAccessBoundaryFetcherTest;
  friend class CredentialsTest;
  friend struct RegionalAccessBoundaryRequest;
  friend void RetryFetchRegionalAccessBoundary(void* arg, grpc_error_handle error);
  friend void OnRegionalAccessBoundaryResponse(void* arg, grpc_error_handle error);
  friend void StartRegionalAccessBoundaryFetch(
      grpc_core::RefCountedPtr<RegionalAccessBoundaryRequest> req);

  gpr_mu cache_mu_;
  std::optional<RegionalAccessBoundary> cache_;
  bool fetch_in_flight_ = false;
  int cooldown_multiplier_ = 1;
  gpr_timespec cooldown_deadline_ = gpr_inf_past(GPR_CLOCK_REALTIME);
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_CREDENTIALS_CALL_REGIONAL_ACCESS_BOUNDARY_FETCHER_H

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

#include "src/core/credentials/call/call_credentials.h"
#include "src/core/credentials/call/call_creds_util.h"

namespace grpc_core {

namespace {
  constexpr int kMaxRegionalAccessBoundaryRetries = 6;
  constexpr absl::string_view kRegionalEndpoint = "rep.googleapis.com";
  constexpr absl::string_view kGoogleApisEndpoint = "googleapis.com";
}

RegionalAccessBoundaryFetcher::RegionalAccessBoundaryFetcher() {
  gpr_mu_init(&cache_mu_);
}

RegionalAccessBoundaryFetcher::~RegionalAccessBoundaryFetcher() {
  gpr_mu_destroy(&cache_mu_);
}

void RegionalAccessBoundaryFetcher::InvalidateCache() {
  gpr_mu_lock(&cache_mu_);
  cache_.reset();
  gpr_mu_unlock(&cache_mu_);
}

ArenaPromise<absl::StatusOr<ClientMetadataHandle>> RegionalAccessBoundaryFetcher::Fetch(
    RefCountedPtr<grpc_call_credentials> creds,
    ClientMetadataHandle initial_metadata) {
  if (!IsRegionalAccessBoundaryLookupEnabled()) {
    return Immediate(std::move(initial_metadata));
  }

  auto authority = initial_metadata->get_pointer(HttpAuthorityMetadata())
                       ->as_string_view();
  if (authority.find(kRegionalEndpoint) != std::string_view::npos ||
      authority.find(kGoogleApisEndpoint) == std::string_view::npos) {
    return Immediate(std::move(initial_metadata));
  }

  {
    gpr_mu_lock(&cache_mu_);
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
        gpr_time_cmp(gpr_now(GPR_CLOCK_REALTIME),
                     cooldown_deadline_) < 0) {
      
      gpr_mu_unlock(&cache_mu_);
      return Immediate(std::move(initial_metadata));
    }
    fetch_in_flight_ = true;
    gpr_mu_unlock(&cache_mu_);
  }

  return Immediate(std::move(initial_metadata));
}

}  // namespace grpc_core

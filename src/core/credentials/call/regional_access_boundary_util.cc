//
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
//

#include "src/core/credentials/call/regional_access_boundary_util.h"

#include "src/core/credentials/call/call_creds_util.h"

namespace grpc_core {

namespace {
  constexpr int kMaxRegionalAccessBoundaryRetries = 6;
  constexpr absl::string_view kRegionalEndpoint = "rep.googleapis.com";
}

grpc_core::ArenaPromise<absl::StatusOr<grpc_core::ClientMetadataHandle>> FetchRegionalAccessBoundary(
    grpc_core::RefCountedPtr<grpc_call_credentials> creds,
    grpc_core::ClientMetadataHandle initial_metadata) {
  if (!grpc_core::IsRegionalAccessBoundaryLookupEnabled()) {
    return grpc_core::Immediate(std::move(initial_metadata));
  }

  if(initial_metadata->get_pointer(grpc_core::HttpAuthorityMetadata())->as_string_view().find(kRegionalEndpoint) != 
      std::string_view::npos) {
    return grpc_core::Immediate(std::move(initial_metadata));
  }

  {
    gpr_mu_lock(&creds->regional_access_boundary_cache_mu);
    auto hasValidCache = creds->regional_access_boundary_cache.has_value() &&
          creds->regional_access_boundary_cache->isValid();
    if (hasValidCache) {
        initial_metadata->Append(
            GRPC_ALLOWED_LOCATIONS_KEY,
            grpc_core::Slice::FromCopiedString(
                creds->regional_access_boundary_cache->encoded_locations),
            [](absl::string_view, const grpc_core::Slice&) { abort(); });
    }
    if (hasValidCache ||
        creds->regional_access_boundary_fetch_in_flight ||
        gpr_time_cmp(gpr_now(GPR_CLOCK_REALTIME),
                     creds->regional_access_boundary_cooldown_deadline) < 0) {
      
      gpr_mu_unlock(&creds->regional_access_boundary_cache_mu);
      return grpc_core::Immediate(std::move(initial_metadata));
    }
    creds->regional_access_boundary_fetch_in_flight = true;
    gpr_mu_unlock(&creds->regional_access_boundary_cache_mu);
  }

  return grpc_core::Immediate(std::move(initial_metadata));
}
}  // namespace grpc_core
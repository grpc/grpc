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

#include "src/core/lib/security/credentials/token_fetcher/token_fetcher_credentials.h"

#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/promise/promise.h"

namespace grpc_core {

namespace {

// Amount of time before the token's expiration that we consider it
// invalid and start a new fetch.  Also determines the timeout for the
// fetch request.
constexpr Duration kTokenRefreshDuration = Duration::Seconds(60);

}  // namespace

TokenFetcherCredentials::TokenFetcherCredentials()
    : pollent_(grpc_polling_entity_create_from_pollset_set(
          grpc_pollset_set_create())) {}

TokenFetcherCredentials::~TokenFetcherCredentials() {
  grpc_pollset_set_destroy(grpc_polling_entity_pollset_set(&pollent_));
}

ArenaPromise<absl::StatusOr<ClientMetadataHandle>>
TokenFetcherCredentials::GetRequestMetadata(
    ClientMetadataHandle initial_metadata, const GetRequestMetadataArgs*) {
  RefCountedPtr<PendingCall> pending_call;
  {
    MutexLock lock(&mu_);
    // Check if we can use the cached token.
    auto* cached_token = absl::get_if<RefCountedPtr<Token>>(&token_);
    if (cached_token != nullptr && *cached_token != nullptr &&
        ((*cached_token)->ExpirationTime() - Timestamp::Now()) >
            kTokenRefreshDuration) {
      (*cached_token)->AddTokenToClientInitialMetadata(*initial_metadata);
      return Immediate(std::move(initial_metadata));
    }
    // Couldn't get the token from the cache.
    // Add this call to the pending list.
    pending_call = MakeRefCounted<PendingCall>();
    pending_call->waker = GetContext<Activity>()->MakeNonOwningWaker();
    pending_call->pollent = GetContext<grpc_polling_entity>();
    grpc_polling_entity_add_to_pollset_set(
        pending_call->pollent, grpc_polling_entity_pollset_set(&pollent_));
    pending_call->md = std::move(initial_metadata);
    pending_calls_.insert(pending_call);
    // Start a new fetch if needed.
    if (!absl::holds_alternative<OrphanablePtr<FetchRequest>>(token_)) {
      token_ = FetchToken(
          &pollent_, /*deadline=*/Timestamp::Now() + kTokenRefreshDuration,
          [self = RefAsSubclass<TokenFetcherCredentials>()](
              absl::StatusOr<RefCountedPtr<Token>> token) mutable {
            self->TokenFetchComplete(std::move(token));
          });
    }
  }
  return [pending_call = std::move(pending_call)]()
             -> Poll<absl::StatusOr<ClientMetadataHandle>> {
           if (!pending_call->done.load(std::memory_order_acquire)) {
             return Pending{};
           }
           if (!pending_call->result.ok()) {
             return pending_call->result.status();
           }
           (*pending_call->result)->AddTokenToClientInitialMetadata(
               *pending_call->md);
           return std::move(pending_call->md);
         };
}

void TokenFetcherCredentials::TokenFetchComplete(
    absl::StatusOr<RefCountedPtr<Token>> token) {
  // Update cache and grab list of pending requests.
  absl::flat_hash_set<RefCountedPtr<PendingCall>> pending_calls;
  {
    MutexLock lock(&mu_);
    token_ = token.value_or(nullptr);
    pending_calls_.swap(pending_calls);
  }
  // Invoke callbacks for all pending requests.
  for (auto& pending_call : pending_calls) {
    pending_call->result = token;
    pending_call->done.store(true, std::memory_order_release);
    pending_call->waker.Wakeup();
    grpc_polling_entity_del_from_pollset_set(
        pending_call->pollent, grpc_polling_entity_pollset_set(&pollent_));
  }
}

}  // namespace grpc_core

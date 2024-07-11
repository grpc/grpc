//
// Copyright 2016 gRPC authors.
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

#ifndef GRPC_SRC_CORE_LIB_SECURITY_CREDENTIALS_TOKEN_FETCHER_TOKEN_FETCHER_CREDENTIALS_H
#define GRPC_SRC_CORE_LIB_SECURITY_CREDENTIALS_TOKEN_FETCHER_TOKEN_FETCHER_CREDENTIALS_H

#include <atomic>
#include <memory>
#include <utility>

#include "absl/container/flat_hash_set.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/statusor.h"
#include "absl/types/variant.h"

#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/promise/arena_promise.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/transport/metadata.h"
#include "src/core/util/http_client/httpcli.h"
#include "src/core/util/useful.h"

namespace grpc_core {

// A base class for credentials that fetch tokens via an HTTP request.
class TokenFetcherCredentials : public grpc_call_credentials {
 public:
  // Represents a token.
  class Token : public RefCounted<Token> {
   public:
    virtual ~Token() = default;

    // Returns the token's expiration time.
    virtual Timestamp ExpirationTime() = 0;

    // Adds the token to the call's client initial metadata.
    virtual void AddTokenToClientInitialMetadata(ClientMetadata& metadata) = 0;
  };

  // A token fetcher interface.
  class TokenFetcher {
   public:
    virtual ~TokenFetcher() = default;

    // Fetches a token.  The on_done callback will be invoked when complete.
    // The fetch may be cancelled by orphaning the returned HttpRequest.
    virtual OrphanablePtr<HttpRequest> FetchToken(
        grpc_polling_entity* pollent, Timestamp deadline,
        absl::AnyInvocable<void(absl::StatusOr<RefCountedPtr<Token>>)>
            on_done) = 0;
  };

  ~TokenFetcherCredentials() override;

  ArenaPromise<absl::StatusOr<ClientMetadataHandle>>
  GetRequestMetadata(ClientMetadataHandle initial_metadata,
                     const GetRequestMetadataArgs* args) override;

 protected:
  explicit TokenFetcherCredentials(std::unique_ptr<TokenFetcher> token_fetcher);

 private:
  // A call that is waiting for a token fetch request to complete.
  struct PendingCall : public RefCounted<PendingCall> {
    std::atomic<bool> done{false};
    Waker waker;
    grpc_polling_entity* pollent;
    ClientMetadataHandle md;
    absl::StatusOr<RefCountedPtr<Token>> result;
  };

  int cmp_impl(const grpc_call_credentials* other) const override {
    // TODO(yashykt): Check if we can do something better here
    return QsortCompare(static_cast<const grpc_call_credentials*>(this), other);
  }

  void TokenFetchComplete(absl::StatusOr<RefCountedPtr<Token>> token);

  const std::unique_ptr<TokenFetcher> token_fetcher_;

  Mutex mu_;
  absl::variant<RefCountedPtr<Token>, OrphanablePtr<HttpRequest>> token_
      ABSL_GUARDED_BY(&mu_);
  absl::flat_hash_set<RefCountedPtr<PendingCall>> pending_calls_
      ABSL_GUARDED_BY(&mu_);
  grpc_polling_entity pollent_ ABSL_GUARDED_BY(&mu_);
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_SECURITY_CREDENTIALS_TOKEN_FETCHER_TOKEN_FETCHER_CREDENTIALS_H

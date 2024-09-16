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

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/promise/promise.h"

namespace grpc_core {

namespace {

// Amount of time before the token's expiration that we consider it
// invalid to account for server processing time and clock skew.
constexpr Duration kTokenExpirationAdjustmentDuration = Duration::Seconds(30);

// Amount of time before the token's expiration that we pre-fetch a new
// token.  Also determines the timeout for the fetch request.
constexpr Duration kTokenRefreshDuration = Duration::Seconds(60);

}  // namespace

//
// TokenFetcherCredentials::Token
//

TokenFetcherCredentials::Token::Token(Slice token, Timestamp expiration)
    : token_(std::move(token)),
      expiration_(expiration - kTokenExpirationAdjustmentDuration) {}

void TokenFetcherCredentials::Token::AddTokenToClientInitialMetadata(
    ClientMetadata& metadata) const {
  metadata.Append(GRPC_AUTHORIZATION_METADATA_KEY, token_.Ref(),
                  [](absl::string_view, const Slice&) { abort(); });
}

//
// TokenFetcherCredentials::FetchState::BackoffTimer
//

TokenFetcherCredentials::FetchState::BackoffTimer::BackoffTimer(
    RefCountedPtr<FetchState> fetch_state)
    : fetch_state_(std::move(fetch_state)) {
  const Duration delay = fetch_state_->backoff_.NextAttemptDelay();
  GRPC_TRACE_LOG(token_fetcher_credentials, INFO)
      << "[TokenFetcherCredentials " << fetch_state_->creds_.get()
      << "]: fetch_state=" << fetch_state_.get() << " backoff_timer=" << this
      << ": starting backoff timer for " << delay;
  timer_handle_ = fetch_state_->creds_->event_engine().RunAfter(
      delay, [self = Ref()]() mutable {
        ApplicationCallbackExecCtx callback_exec_ctx;
        ExecCtx exec_ctx;
        self->OnTimer();
        self.reset();
      });
}

void TokenFetcherCredentials::FetchState::BackoffTimer::Orphan() {
  GRPC_TRACE_LOG(token_fetcher_credentials, INFO)
      << "[TokenFetcherCredentials " << fetch_state_->creds_.get()
      << "]: fetch_state=" << fetch_state_.get() << " backoff_timer=" << this
      << ": backoff timer shut down";
  if (timer_handle_.has_value()) {
    GRPC_TRACE_LOG(token_fetcher_credentials, INFO)
        << "[TokenFetcherCredentials " << fetch_state_->creds_.get()
        << "]: fetch_state=" << fetch_state_.get() << " backoff_timer=" << this
        << ": cancelling timer";
    fetch_state_->creds_->event_engine().Cancel(*timer_handle_);
    timer_handle_.reset();
    fetch_state_->ResumeQueuedCalls(
        absl::CancelledError("credentials shutdown"));
  }
  Unref();
}

void TokenFetcherCredentials::FetchState::BackoffTimer::OnTimer() {
  MutexLock lock(&fetch_state_->creds_->mu_);
  if (!timer_handle_.has_value()) return;
  timer_handle_.reset();
  GRPC_TRACE_LOG(token_fetcher_credentials, INFO)
      << "[TokenFetcherCredentials " << fetch_state_->creds_.get()
      << "]: fetch_state=" << fetch_state_.get() << " backoff_timer=" << this
      << ": backoff timer fired";
  if (fetch_state_->queued_calls_.empty()) {
    // If there are no pending calls when the timer fires, then orphan
    // the FetchState object.  Note that this drops the backoff state,
    // but that's probably okay, because if we didn't have any pending
    // calls during the backoff period, we probably won't see any
    // immediately now either.
    GRPC_TRACE_LOG(token_fetcher_credentials, INFO)
        << "[TokenFetcherCredentials " << fetch_state_->creds_.get()
        << "]: fetch_state=" << fetch_state_.get() << " backoff_timer=" << this
        << ": no pending calls, clearing state";
    fetch_state_->creds_->fetch_state_.reset();
  } else {
    // If there are pending calls, then start a new fetch attempt.
    GRPC_TRACE_LOG(token_fetcher_credentials, INFO)
        << "[TokenFetcherCredentials " << fetch_state_->creds_.get()
        << "]: fetch_state=" << fetch_state_.get() << " backoff_timer=" << this
        << ": starting new fetch attempt";
    fetch_state_->StartFetchAttempt();
  }
}

//
// TokenFetcherCredentials::FetchState
//

TokenFetcherCredentials::FetchState::FetchState(
    WeakRefCountedPtr<TokenFetcherCredentials> creds)
    : creds_(std::move(creds)),
      backoff_(BackOff::Options()
                   .set_initial_backoff(Duration::Seconds(1))
                   .set_multiplier(1.6)
                   .set_jitter(creds_->test_only_use_backoff_jitter_ ? 0.2 : 0)
                   .set_max_backoff(Duration::Seconds(120))) {
  StartFetchAttempt();
}

void TokenFetcherCredentials::FetchState::Orphan() {
  GRPC_TRACE_LOG(token_fetcher_credentials, INFO)
      << "[TokenFetcherCredentials " << creds_.get()
      << "]: fetch_state=" << this << ": shutting down";
  // Cancels fetch or backoff timer, if any.
  state_ = Shutdown{};
  Unref();
}

void TokenFetcherCredentials::FetchState::StartFetchAttempt() {
  GRPC_TRACE_LOG(token_fetcher_credentials, INFO)
      << "[TokenFetcherCredentials " << creds_.get()
      << "]: fetch_state=" << this << ": starting fetch";
  state_ = creds_->FetchToken(
      /*deadline=*/Timestamp::Now() + kTokenRefreshDuration,
      [self = Ref()](absl::StatusOr<RefCountedPtr<Token>> token) mutable {
        self->TokenFetchComplete(std::move(token));
      });
}

void TokenFetcherCredentials::FetchState::TokenFetchComplete(
    absl::StatusOr<RefCountedPtr<Token>> token) {
  MutexLock lock(&creds_->mu_);
  // If we were shut down, clean up.
  if (absl::holds_alternative<Shutdown>(state_)) {
    if (token.ok()) token = absl::CancelledError("credentials shutdown");
    GRPC_TRACE_LOG(token_fetcher_credentials, INFO)
        << "[TokenFetcherCredentials " << creds_.get()
        << "]: fetch_state=" << this
        << ": shut down before fetch completed: " << token.status();
    ResumeQueuedCalls(std::move(token));
    return;
  }
  // If succeeded, update cache in creds object.
  if (token.ok()) {
    GRPC_TRACE_LOG(token_fetcher_credentials, INFO)
        << "[TokenFetcherCredentials " << creds_.get()
        << "]: fetch_state=" << this << ": token fetch succeeded";
    creds_->token_ = *token;
    creds_->fetch_state_.reset();  // Orphan ourselves.
  } else {
    GRPC_TRACE_LOG(token_fetcher_credentials, INFO)
        << "[TokenFetcherCredentials " << creds_.get()
        << "]: fetch_state=" << this
        << ": token fetch failed: " << token.status();
    // If failed, start backoff timer.
    state_ = OrphanablePtr<BackoffTimer>(new BackoffTimer(Ref()));
  }
  ResumeQueuedCalls(std::move(token));
}

void TokenFetcherCredentials::FetchState::ResumeQueuedCalls(
    absl::StatusOr<RefCountedPtr<Token>> token) {
  // Invoke callbacks for all pending requests.
  for (auto& queued_call : queued_calls_) {
    queued_call->result = token;
    queued_call->done.store(true, std::memory_order_release);
    queued_call->waker.Wakeup();
    grpc_polling_entity_del_from_pollset_set(
        queued_call->pollent,
        grpc_polling_entity_pollset_set(&creds_->pollent_));
  }
  queued_calls_.clear();
}

RefCountedPtr<TokenFetcherCredentials::QueuedCall>
TokenFetcherCredentials::FetchState::QueueCall(
    ClientMetadataHandle initial_metadata) {
  // Add call to pending list.
  auto queued_call = MakeRefCounted<QueuedCall>();
  queued_call->waker = GetContext<Activity>()->MakeNonOwningWaker();
  queued_call->pollent = GetContext<grpc_polling_entity>();
  grpc_polling_entity_add_to_pollset_set(
      queued_call->pollent, grpc_polling_entity_pollset_set(&creds_->pollent_));
  queued_call->md = std::move(initial_metadata);
  queued_calls_.insert(queued_call);
  return queued_call;
}

//
// TokenFetcherCredentials
//

TokenFetcherCredentials::TokenFetcherCredentials(
    std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine,
    bool test_only_use_backoff_jitter)
    : event_engine_(
          event_engine == nullptr
              ? grpc_event_engine::experimental::GetDefaultEventEngine()
              : std::move(event_engine)),
      test_only_use_backoff_jitter_(test_only_use_backoff_jitter),
      pollent_(grpc_polling_entity_create_from_pollset_set(
          grpc_pollset_set_create())) {}

TokenFetcherCredentials::~TokenFetcherCredentials() {
  grpc_pollset_set_destroy(grpc_polling_entity_pollset_set(&pollent_));
}

void TokenFetcherCredentials::Orphaned() {
  MutexLock lock(&mu_);
  fetch_state_.reset();
}

ArenaPromise<absl::StatusOr<ClientMetadataHandle>>
TokenFetcherCredentials::GetRequestMetadata(
    ClientMetadataHandle initial_metadata, const GetRequestMetadataArgs*) {
  RefCountedPtr<QueuedCall> queued_call;
  {
    MutexLock lock(&mu_);
    // If we don't have a cached token or the token is within the
    // refresh duration, start a new fetch if there isn't a pending one.
    if ((token_ == nullptr || (token_->ExpirationTime() - Timestamp::Now()) <=
                                  kTokenRefreshDuration) &&
        fetch_state_ == nullptr) {
      GRPC_TRACE_LOG(token_fetcher_credentials, INFO)
          << "[TokenFetcherCredentials " << this
          << "]: " << GetContext<Activity>()->DebugTag()
          << " triggering new token fetch";
      fetch_state_ = OrphanablePtr<FetchState>(
          new FetchState(WeakRefAsSubclass<TokenFetcherCredentials>()));
    }
    // If we have a cached non-expired token, use it.
    if (token_ != nullptr &&
        (token_->ExpirationTime() - Timestamp::Now()) > Duration::Zero()) {
      GRPC_TRACE_LOG(token_fetcher_credentials, INFO)
          << "[TokenFetcherCredentials " << this
          << "]: " << GetContext<Activity>()->DebugTag()
          << " using cached token";
      token_->AddTokenToClientInitialMetadata(*initial_metadata);
      return Immediate(std::move(initial_metadata));
    }
    // If we don't have a cached token, this call will need to be queued.
    GRPC_TRACE_LOG(token_fetcher_credentials, INFO)
        << "[TokenFetcherCredentials " << this
        << "]: " << GetContext<Activity>()->DebugTag()
        << " no cached token; queuing call";
    queued_call = fetch_state_->QueueCall(std::move(initial_metadata));
  }
  return [this, queued_call = std::move(queued_call)]()
             -> Poll<absl::StatusOr<ClientMetadataHandle>> {
    if (!queued_call->done.load(std::memory_order_acquire)) {
      return Pending{};
    }
    if (!queued_call->result.ok()) {
      GRPC_TRACE_LOG(token_fetcher_credentials, INFO)
          << "[TokenFetcherCredentials " << this
          << "]: " << GetContext<Activity>()->DebugTag()
          << " token fetch failed; failing call";
      return queued_call->result.status();
    }
    GRPC_TRACE_LOG(token_fetcher_credentials, INFO)
        << "[TokenFetcherCredentials " << this
        << "]: " << GetContext<Activity>()->DebugTag()
        << " token fetch complete; resuming call";
    (*queued_call->result)->AddTokenToClientInitialMetadata(*queued_call->md);
    return std::move(queued_call->md);
  };
}

}  // namespace grpc_core

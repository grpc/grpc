// Copyright 2024 gRPC authors.
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

#include "src/core/client_channel/retry_interceptor.h"

#include "src/core/lib/promise/cancel_callback.h"
#include "src/core/lib/promise/for_each.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/sleep.h"

namespace grpc_core {

namespace {
size_t GetMaxPerRpcRetryBufferSize(const ChannelArgs& args) {
  // By default, we buffer 256 KiB per RPC for retries.
  // TODO(roth): Do we have any data to suggest a better value?
  static constexpr int kDefaultPerRpcRetryBufferSize = (256 << 10);
  return Clamp(args.GetInt(GRPC_ARG_PER_RPC_RETRY_BUFFER_SIZE)
                   .value_or(kDefaultPerRpcRetryBufferSize),
               0, INT_MAX);
}
}  // namespace

////////////////////////////////////////////////////////////////////////////////
// RetryInterceptor

RetryInterceptor::RetryInterceptor(const ChannelArgs& args)
    : per_rpc_retry_buffer_size_(GetMaxPerRpcRetryBufferSize(args)) {}

void RetryInterceptor::InterceptCall(
    UnstartedCallHandler unstarted_call_handler) {
  auto call_handler = unstarted_call_handler.StartCall();
  auto* arena = call_handler.arena();
  auto call = arena->MakeRefCounted<Call>(RefAsSubclass<RetryInterceptor>(),
                                          std::move(call_handler));
  call->StartAttempt();
  call->Start();
}

////////////////////////////////////////////////////////////////////////////////
// RetryInterceptor::Call

RetryInterceptor::Call::Call(RefCountedPtr<RetryInterceptor> interceptor,
                             CallHandler call_handler)
    : call_handler_(std::move(call_handler)),
      interceptor_(std::move(interceptor)) {}

auto RetryInterceptor::Call::ClientToBuffer() {
  return TrySeq(
      call_handler_.PullClientInitialMetadata(),
      [self = Ref()](ClientMetadataHandle metadata) mutable {
        return self->request_buffer_.PushClientInitialMetadata(
            std::move(metadata));
      },
      [self = Ref()](size_t buffered) {
        self->MaybeCommit(buffered);
        return ForEach(OutgoingMessages(self->call_handler_),
                       [self](MessageHandle message) {
                         return TrySeq(self->request_buffer_.PushMessage(
                                           std::move(message)),
                                       [self](size_t buffered) {
                                         self->MaybeCommit(buffered);
                                         return absl::OkStatus();
                                       });
                       });
      });
}

void RetryInterceptor::Call::Start() {
  call_handler_.SpawnGuarded("client_to_buffer", [self = Ref()]() {
    return OnCancel(Map(self->ClientToBuffer(),
                        [self](absl::Status status) {
                          if (status.ok()) {
                            self->request_buffer_.FinishSends();
                          } else {
                            self->request_buffer_.Cancel(status);
                          }
                          return status;
                        }),
                    [self]() { self->request_buffer_.Cancel(); });
  });
}

void RetryInterceptor::Call::StartAttempt() {
  if (current_attempt_ != nullptr) {
    current_attempt_->Cancel();
  }
  current_attempt_ = call_handler_.arena()->MakeRefCounted<Attempt>(Ref());
  current_attempt_->Start();
}

void RetryInterceptor::Call::MaybeCommit(size_t buffered) {
  if (buffered >= interceptor_->per_rpc_retry_buffer_size_) {
    current_attempt_->Commit();
  }
}

absl::optional<Duration> RetryInterceptor::Call::ShouldRetry(
    const ServerMetadata& md,
    absl::FunctionRef<std::string()> lazy_attempt_debug_string) {
  // If no retry policy, don't retry.
  if (retry_policy_ == nullptr) return absl::nullopt;
  const auto status = md.get(GrpcStatusMetadata());
  if (status.has_value()) {
    if (GPR_LIKELY(*status == GRPC_STATUS_OK)) {
      if (retry_throttle_data_ != nullptr) {
        retry_throttle_data_->RecordSuccess();
      }
      GRPC_TRACE_LOG(retry, INFO)
          << lazy_attempt_debug_string() << ": call succeeded";
      return absl::nullopt;
    }
    // Status is not OK.  Check whether the status is retryable.
    if (!retry_policy_->retryable_status_codes().Contains(*status)) {
      GRPC_TRACE_LOG(retry, INFO) << lazy_attempt_debug_string() << ": status "
                                  << grpc_status_code_to_string(*status)
                                  << " not configured as retryable";
      return absl::nullopt;
    }
  }
  // Record the failure and check whether retries are throttled.
  // Note that it's important for this check to come after the status
  // code check above, since we should only record failures whose statuses
  // match the configured retryable status codes, so that we don't count
  // things like failures due to malformed requests (INVALID_ARGUMENT).
  // Conversely, it's important for this to come before the remaining
  // checks, so that we don't fail to record failures due to other factors.
  if (retry_throttle_data_ != nullptr &&
      !retry_throttle_data_->RecordFailure()) {
    GRPC_TRACE_LOG(retry, INFO)
        << lazy_attempt_debug_string() << ": retries throttled";
    return absl::nullopt;
  }
  // Check whether the call is committed.
  if (request_buffer_.committed()) {
    GRPC_TRACE_LOG(retry, INFO)
        << lazy_attempt_debug_string() << ": retries already committed";
    return absl::nullopt;
  }
  // Check whether we have retries remaining.
  ++num_attempts_completed_;
  if (num_attempts_completed_ >= retry_policy_->max_attempts()) {
    GRPC_TRACE_LOG(retry, INFO)
        << lazy_attempt_debug_string() << ": exceeded "
        << retry_policy_->max_attempts() << " retry attempts";
    return absl::nullopt;
  }
  // Check server push-back.
  auto server_pushback = md.get(GrpcRetryPushbackMsMetadata());
  if (server_pushback.has_value()) {
    if (*server_pushback < Duration::Zero()) {
      GRPC_TRACE_LOG(retry, INFO) << lazy_attempt_debug_string()
                                  << ": not retrying due to server push-back";
      return absl::nullopt;
    } else {
      GRPC_TRACE_LOG(retry, INFO)
          << lazy_attempt_debug_string() << ": server push-back: retry in "
          << server_pushback->millis() << " ms";
    }
  }
  // We should retry.
  return server_pushback;
}

////////////////////////////////////////////////////////////////////////////////
// RetryInterceptor::Attempt

RetryInterceptor::Attempt::Attempt(RefCountedPtr<Call> call)
    : reader_(call->request_buffer()), call_(std::move(call)) {}

auto RetryInterceptor::Attempt::ServerToClientGotInitialMetadata(
    ServerMetadataHandle md) {
  Commit();
  call_->call_handler()->SpawnPushServerInitialMetadata(std::move(md));
  return Seq(
      ForEach(OutgoingMessages(&initiator_),
              [call = call_](MessageHandle message) {
                return call->call_handler()->SpawnPushMessage(
                    std::move(message));
              }),
      initiator_.PullServerTrailingMetadata(),
      [call = call_](ServerMetadataHandle md) {
        call->call_handler()->SpawnPushServerTrailingMetadata(std::move(md));
        return absl::OkStatus();
      });
}

auto RetryInterceptor::Attempt::ServerToClientGotTrailersOnlyResponse() {
  return Seq(initiator_.PullServerTrailingMetadata(),
             [self = Ref()](ServerMetadataHandle md) {
               auto pushback = self->call_->ShouldRetry(
                   *md, [self = self.get()]() -> std::string {
                     return absl::StrFormat("call:%s attempt:%p",
                                            Activity::current()->DebugTag(),
                                            self);
                   });
               return If(
                   pushback.has_value(),
                   [self, pushback]() {
                     return Map(Sleep(*pushback),
                                [call = self->call_](absl::Status) {
                                  call->StartAttempt();
                                  return absl::OkStatus();
                                });
                   },
                   [self, md = std::move(md)]() mutable {
                     self->Commit();
                     self->call_->call_handler()
                         ->SpawnPushServerTrailingMetadata(std::move(md));
                     return absl::OkStatus();
                   });
             });
}

auto RetryInterceptor::Attempt::ServerToClient() {
  return TrySeq(
      initiator_.PullServerInitialMetadata(),
      [self = Ref()](absl::optional<ServerMetadataHandle> metadata) {
        const bool has_md = metadata.has_value();
        return If(
            has_md,
            [self = self.get(), md = std::move(metadata)]() mutable {
              return self->ServerToClientGotInitialMetadata(std::move(*md));
            },
            [self = self.get()]() {
              return self->ServerToClientGotTrailersOnlyResponse();
            });
      });
}

void RetryInterceptor::Attempt::Commit() {
  call_->request_buffer()->Commit(reader());
}

void RetryInterceptor::Attempt::Start() {
  call_->call_handler()->SpawnGuarded("buffer_to_server", [self = Ref()]() {
    return TrySeq(
        self->reader_.PullClientInitialMetadata(),
        [self](ClientMetadataHandle metadata) {
          self->initiator_ = self->call_->interceptor()->MakeChildCall(
              std::move(metadata), self->call_->call_handler()->arena()->Ref());
          self->initiator_.SpawnGuarded(
              "server_to_client", [self]() { return self->ServerToClient(); });
          return ForEach(
              OutgoingMessages(&self->reader_), [self](MessageHandle message) {
                return self->initiator_.PushMessage(std::move(message));
              });
        });
  });
}

void RetryInterceptor::Attempt::Cancel() { initiator_.SpawnCancel(); }

}  // namespace grpc_core

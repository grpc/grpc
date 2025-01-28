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
#include "src/core/service_config/service_config_call_data.h"

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

namespace retry_detail {

RetryState::RetryState(
    const internal::RetryMethodConfig* retry_policy,
    RefCountedPtr<internal::ServerRetryThrottleData> retry_throttle_data)
    : retry_policy_(retry_policy),
      retry_throttle_data_(std::move(retry_throttle_data)),
      retry_backoff_(
          BackOff::Options()
              .set_initial_backoff(retry_policy_ == nullptr
                                       ? Duration::Zero()
                                       : retry_policy_->initial_backoff())
              .set_multiplier(retry_policy_ == nullptr
                                  ? 0
                                  : retry_policy_->backoff_multiplier())
              // This value was picked arbitrarily.  It can be changed if
              // there is any even moderately compelling reason to do so.
              .set_jitter(0.2)
              .set_max_backoff(retry_policy_ == nullptr
                                   ? Duration::Zero()
                                   : retry_policy_->max_backoff())) {}

std::optional<Duration> RetryState::ShouldRetry(
    const ServerMetadata& md, bool committed,
    absl::FunctionRef<std::string()> lazy_attempt_debug_string) {
  // If no retry policy, don't retry.
  if (retry_policy_ == nullptr) {
    GRPC_TRACE_LOG(retry, INFO)
        << lazy_attempt_debug_string() << " no retry policy";
    return std::nullopt;
  }
  const auto status = md.get(GrpcStatusMetadata());
  if (status.has_value()) {
    if (GPR_LIKELY(*status == GRPC_STATUS_OK)) {
      if (retry_throttle_data_ != nullptr) {
        retry_throttle_data_->RecordSuccess();
      }
      GRPC_TRACE_LOG(retry, INFO)
          << lazy_attempt_debug_string() << " call succeeded";
      return std::nullopt;
    }
    // Status is not OK.  Check whether the status is retryable.
    if (!retry_policy_->retryable_status_codes().Contains(*status)) {
      GRPC_TRACE_LOG(retry, INFO) << lazy_attempt_debug_string() << ": status "
                                  << grpc_status_code_to_string(*status)
                                  << " not configured as retryable";
      return std::nullopt;
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
        << lazy_attempt_debug_string() << " retries throttled";
    return std::nullopt;
  }
  // Check whether the call is committed.
  if (committed) {
    GRPC_TRACE_LOG(retry, INFO)
        << lazy_attempt_debug_string() << " retries already committed";
    return std::nullopt;
  }
  // Check whether we have retries remaining.
  ++num_attempts_completed_;
  if (num_attempts_completed_ >= retry_policy_->max_attempts()) {
    GRPC_TRACE_LOG(retry, INFO)
        << lazy_attempt_debug_string() << " exceeded "
        << retry_policy_->max_attempts() << " retry attempts";
    return std::nullopt;
  }
  // Check server push-back.
  const auto server_pushback = md.get(GrpcRetryPushbackMsMetadata());
  if (server_pushback.has_value() && server_pushback < Duration::Zero()) {
    GRPC_TRACE_LOG(retry, INFO) << lazy_attempt_debug_string()
                                << " not retrying due to server push-back";
    return std::nullopt;
  }
  // We should retry.
  Duration next_attempt_timeout;
  if (server_pushback.has_value()) {
    CHECK_GE(*server_pushback, Duration::Zero());
    next_attempt_timeout = *server_pushback;
    retry_backoff_.Reset();
  } else {
    next_attempt_timeout = retry_backoff_.NextAttemptDelay();
  }
  GRPC_TRACE_LOG(retry, INFO)
      << lazy_attempt_debug_string() << " server push-back: retry in "
      << next_attempt_timeout;
  return next_attempt_timeout;
}

absl::StatusOr<RefCountedPtr<internal::ServerRetryThrottleData>>
ServerRetryThrottleDataFromChannelArgs(const ChannelArgs& args) {
  // Get retry throttling parameters from service config.
  auto* service_config = args.GetObject<ServiceConfig>();
  if (service_config == nullptr) return nullptr;
  const auto* config = static_cast<const internal::RetryGlobalConfig*>(
      service_config->GetGlobalParsedConfig(
          internal::RetryServiceConfigParser::ParserIndex()));
  if (config == nullptr) return nullptr;
  // Get server name from target URI.
  auto server_uri = args.GetString(GRPC_ARG_SERVER_URI);
  if (!server_uri.has_value()) {
    return GRPC_ERROR_CREATE(
        "server URI channel arg missing or wrong type in client channel "
        "filter");
  }
  absl::StatusOr<URI> uri = URI::Parse(*server_uri);
  if (!uri.ok() || uri->path().empty()) {
    return GRPC_ERROR_CREATE("could not extract server name from target URI");
  }
  std::string server_name(absl::StripPrefix(uri->path(), "/"));
  // Get throttling config for server_name.
  return internal::ServerRetryThrottleMap::Get()->GetDataForServer(
      server_name, config->max_milli_tokens(), config->milli_token_ratio());
}

}  // namespace retry_detail

////////////////////////////////////////////////////////////////////////////////
// RetryInterceptor

absl::StatusOr<RefCountedPtr<RetryInterceptor>> RetryInterceptor::Create(
    const ChannelArgs& args, const FilterArgs&) {
  auto retry_throttle_data =
      retry_detail::ServerRetryThrottleDataFromChannelArgs(args);
  if (!retry_throttle_data.ok()) {
    return retry_throttle_data.status();
  }
  return MakeRefCounted<RetryInterceptor>(args,
                                          std::move(*retry_throttle_data));
}

RetryInterceptor::RetryInterceptor(
    const ChannelArgs& args,
    RefCountedPtr<internal::ServerRetryThrottleData> retry_throttle_data)
    : per_rpc_retry_buffer_size_(GetMaxPerRpcRetryBufferSize(args)),
      service_config_parser_index_(
          internal::RetryServiceConfigParser::ParserIndex()),
      retry_throttle_data_(std::move(retry_throttle_data)) {}

void RetryInterceptor::InterceptCall(
    UnstartedCallHandler unstarted_call_handler) {
  auto call_handler = unstarted_call_handler.StartCall();
  auto* arena = call_handler.arena();
  auto call = arena->MakeRefCounted<Call>(RefAsSubclass<RetryInterceptor>(),
                                          std::move(call_handler));
  call->StartAttempt();
  call->Start();
}

const internal::RetryMethodConfig* RetryInterceptor::GetRetryPolicy() {
  auto* svc_cfg_call_data = MaybeGetContext<ServiceConfigCallData>();
  if (svc_cfg_call_data == nullptr) return nullptr;
  return static_cast<const internal::RetryMethodConfig*>(
      svc_cfg_call_data->GetMethodParsedConfig(service_config_parser_index_));
}

////////////////////////////////////////////////////////////////////////////////
// RetryInterceptor::Call

RetryInterceptor::Call::Call(RefCountedPtr<RetryInterceptor> interceptor,
                             CallHandler call_handler)
    : call_handler_(std::move(call_handler)),
      interceptor_(std::move(interceptor)),
      retry_state_(interceptor_->GetRetryPolicy(),
                   interceptor_->retry_throttle_data_) {
  GRPC_TRACE_LOG(retry, INFO)
      << DebugTag() << " retry call created: " << retry_state_;
}

auto RetryInterceptor::Call::ClientToBuffer() {
  return TrySeq(
      call_handler_.PullClientInitialMetadata(),
      [self = Ref()](ClientMetadataHandle metadata) mutable {
        GRPC_TRACE_LOG(retry, INFO)
            << self->DebugTag()
            << " got client initial metadata: " << metadata->DebugString();
        return self->request_buffer_.PushClientInitialMetadata(
            std::move(metadata));
      },
      [self = Ref()](size_t buffered) {
        self->MaybeCommit(buffered);
        return ForEach(
            MessagesFrom(self->call_handler_), [self](MessageHandle message) {
              GRPC_TRACE_LOG(retry, INFO)
                  << self->DebugTag() << " got client message "
                  << message->DebugString();
              return TrySeq(
                  self->request_buffer_.PushMessage(std::move(message)),
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
  auto current_attempt = call_handler_.arena()->MakeRefCounted<Attempt>(Ref());
  current_attempt_ = current_attempt.get();
  current_attempt->Start();
}

void RetryInterceptor::Call::MaybeCommit(size_t buffered) {
  GRPC_TRACE_LOG(retry, INFO) << DebugTag() << " buffered:" << buffered << "/"
                              << interceptor_->per_rpc_retry_buffer_size_;
  if (buffered >= interceptor_->per_rpc_retry_buffer_size_) {
    std::ignore = current_attempt_->Commit();
  }
}

std::string RetryInterceptor::Call::DebugTag() {
  return absl::StrFormat("%s call:%p", Activity::current()->DebugTag(), this);
}

////////////////////////////////////////////////////////////////////////////////
// RetryInterceptor::Attempt

RetryInterceptor::Attempt::Attempt(RefCountedPtr<Call> call)
    : call_(std::move(call)), reader_(call_->request_buffer()) {
  GRPC_TRACE_LOG(retry, INFO) << DebugTag() << " retry attempt created";
}

RetryInterceptor::Attempt::~Attempt() { call_->RemoveAttempt(this); }

auto RetryInterceptor::Attempt::ServerToClientGotInitialMetadata(
    ServerMetadataHandle md) {
  GRPC_TRACE_LOG(retry, INFO)
      << DebugTag() << " get server initial metadata " << md->DebugString();
  const bool committed = Commit();
  return If(
      committed,
      [&]() {
        call_->call_handler()->SpawnPushServerInitialMetadata(std::move(md));
        return Seq(ForEach(MessagesFrom(initiator_),
                           [call = call_](MessageHandle message) {
                             GRPC_TRACE_LOG(retry, INFO)
                                 << call->DebugTag() << " got server message "
                                 << message->DebugString();
                             call->call_handler()->SpawnPushMessage(
                                 std::move(message));
                             return Success{};
                           }),
                   initiator_.PullServerTrailingMetadata(),
                   [call = call_](ServerMetadataHandle md) {
                     GRPC_TRACE_LOG(retry, INFO)
                         << call->DebugTag()
                         << " got server trailing metadata: "
                         << md->DebugString();
                     call->call_handler()->SpawnPushServerTrailingMetadata(
                         std::move(md));
                     return absl::OkStatus();
                   });
      },
      [&]() { return []() { return absl::CancelledError(); }; });
}

auto RetryInterceptor::Attempt::ServerToClientGotTrailersOnlyResponse() {
  GRPC_TRACE_LOG(retry, INFO) << DebugTag() << " got trailers only response";
  return Seq(
      initiator_.PullServerTrailingMetadata(),
      [self = Ref()](ServerMetadataHandle md) {
        GRPC_TRACE_LOG(retry, INFO)
            << self->DebugTag()
            << " got server trailing metadata: " << md->DebugString();
        auto delay = self->call_->ShouldRetry(
            *md,
            [self = self.get()]() -> std::string { return self->DebugTag(); });
        return If(
            delay.has_value(),
            [self, delay]() {
              return Map(Sleep(*delay), [call = self->call_](absl::Status) {
                call->StartAttempt();
                return absl::OkStatus();
              });
            },
            [self, md = std::move(md)]() mutable {
              if (!self->Commit()) return absl::CancelledError();
              self->call_->call_handler()->SpawnPushServerTrailingMetadata(
                  std::move(md));
              return absl::OkStatus();
            });
      });
}

auto RetryInterceptor::Attempt::ServerToClient() {
  return TrySeq(
      initiator_.PullServerInitialMetadata(),
      [self = Ref()](std::optional<ServerMetadataHandle> metadata) {
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

bool RetryInterceptor::Attempt::Commit(SourceLocation whence) {
  if (committed_) return true;
  GRPC_TRACE_LOG(retry, INFO) << DebugTag() << " commit attempt from "
                              << whence.file() << ":" << whence.line();
  if (!call_->IsCurrentAttempt(this)) return false;
  committed_ = true;
  call_->request_buffer()->Commit(reader());
  return true;
}

auto RetryInterceptor::Attempt::ClientToServer() {
  return TrySeq(
      reader_.PullClientInitialMetadata(),
      [self = Ref()](ClientMetadataHandle metadata) {
        int num_attempts_completed = self->call_->num_attempts_completed();
        if (GPR_UNLIKELY(num_attempts_completed > 0)) {
          metadata->Set(GrpcPreviousRpcAttemptsMetadata(),
                        num_attempts_completed);
        } else {
          metadata->Remove(GrpcPreviousRpcAttemptsMetadata());
        }
        self->initiator_ = self->call_->interceptor()->MakeChildCall(
            std::move(metadata), self->call_->call_handler()->arena()->Ref());
        self->call_->call_handler()->AddChildCall(self->initiator_);
        self->initiator_.SpawnGuarded(
            "server_to_client", [self]() { return self->ServerToClient(); });
        return ForEach(MessagesFrom(&self->reader_),
                       [self](MessageHandle message) {
                         self->initiator_.SpawnPushMessage(std::move(message));
                         return Success{};
                       });
      });
}

void RetryInterceptor::Attempt::Start() {
  call_->call_handler()->SpawnGuardedUntilCallCompletes(
      "buffer_to_server", [self = Ref()]() { return self->ClientToServer(); });
}

void RetryInterceptor::Attempt::Cancel() { initiator_.SpawnCancel(); }

std::string RetryInterceptor::Attempt::DebugTag() const {
  return absl::StrFormat("%s attempt:%p", call_->DebugTag(), this);
}

}  // namespace grpc_core

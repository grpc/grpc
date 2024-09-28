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

#include "src/core/lib/promise/for_each.h"
#include "src/core/lib/promise/map.h"

namespace grpc_core {

////////////////////////////////////////////////////////////////////////////////
// RetryInterceptor

void RetryInterceptor::InterceptCall(
    UnstartedCallHandler unstarted_call_handler) {
  auto call_handler = unstarted_call_handler.StartCall();
  auto* arena = call_handler.arena();
  auto call = arena->MakeRefCounted<Call>(std::move(call_handler));
  call->StartAttempt();
  call->Start();
}

////////////////////////////////////////////////////////////////////////////////
// RetryInterceptor::Call

RetryInterceptor::Call::Call(RefCountedPtr<RetryInterceptor> interceptor,
                             CallHandler call_handler)
    : call_handler_(std::move(call_handler)),
      interceptor_(std::move(interceptor)) {}

void RetryInterceptor::Call::Start() {
  call_handler_.SpawnGuarded("client_to_buffer", [self = Ref()]() {
    return Map(TrySeq(
                   self->call_handler_.PullClientInitialMetadata(),
                   [self](ClientMetadataHandle metadata) mutable {
                     return self->request_buffer_.PushClientInitialMetadata(
                         std::move(metadata));
                   },
                   [self](size_t buffered) {
                     self->MaybeCommit(buffered);
                     return ForEach(OutgoingMessages(self->call_handler_),
                                    [self](MessageHandle message) {
                                      return TrySeq(
                                          self->request_buffer_.PushMessage(
                                              std::move(message)),
                                          [self](size_t buffered) {
                                            self->MaybeCommit(buffered);
                                            return absl::OkStatus();
                                          });
                                    });
                   }),
               [self](absl::Status status) {
                 if (status.ok()) {
                   self->request_buffer_.FinishSends();
                 } else {
                   self->request_buffer_.Cancel(status);
                 }
                 return status;
               });
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
  if (buffered >= interceptor_->MaxBuffered()) {
    current_attempt_->Commit();
  }
}

////////////////////////////////////////////////////////////////////////////////
// RetryInterceptor::Attempt

RetryInterceptor::Attempt::Attempt(RefCountedPtr<Call> call)
    : reader_(call->request_buffer()), call_(std::move(call)) {}

auto RetryInterceptor::Attempt::ServerToClient() {
  return TrySeq(
      initiator_.PullServerInitialMetadata(),
      [self = Ref()](absl::optional<ServerMetadataHandle> metadata) {
        const bool has_md = metadata.has_value();
        return If(
            has_md,
            [self, md = std::move(metadata)]() mutable {
              self->Commit();
              return Seq(
                  ForEach(OutgoingMessages(self->initiator_),
                          [self](MessageHandle message) {
                            // should be spawn
                            return self->call_->call_handler()->PushMessage(
                                std::move(message));
                          }),
                  self->initiator_.PullServerTrailingMetadata(),
                  [self](ServerMetadataHandle md) {
                    // should be spawn
                    self->call_->call_handler()->PushServerTrailingMetadata(
                        std::move(md));
                    return absl::OkStatus();
                  });
            },
            [self]() {
              return Seq(
                  self->initiator_.PullServerTrailingMetadata(),
                  [self](ServerMetadataHandle md) {
                    if (self->call_->interceptor()->ShouldRetry(*md)) {
                      self->call_->StartAttempt();
                    } else {
                      self->Commit();
                      // should be spawn
                      self->call_->call_handler()->PushServerTrailingMetadata(
                          std::move(md));
                    }
                    return absl::OkStatus();
                  });
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
          return Loop(OutgoingMessages(self->reader_),
                      [self](MessageHandle message) {
                        return self->initiator_.PushMessage(std::move(message));
                      });
        });
  });
  call_->call_handler()->SpawnGuarded("server_to_client",
                                      [self = Ref()]() { return TrySeq(); });
}

}  // namespace grpc_core

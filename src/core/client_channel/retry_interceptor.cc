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

#include "src/core/lib/promise/map.h"

namespace grpc_core {

////////////////////////////////////////////////////////////////////////////////
// RetryInterceptor

void RetryInterceptor::InterceptCall(
    UnstartedCallHandler unstarted_call_handler) {
  unstarted_call_handler.SpawnGuarded(
      "start", [unstarted_call_handler,
                self = WeakRefAsSubclass<RetryInterceptor>()]() mutable {
        return TrySeq(self->Hijack(unstarted_call_handler),
                      [arena = unstarted_call_handler.arena()](
                          HijackedCall hijacked_call) {
                        arena->MakeRefCounted<Call>(std::move(hijacked_call))
                            ->StartAttempt();
                        return absl::OkStatus();
                      });
      });
}

////////////////////////////////////////////////////////////////////////////////
// RetryInterceptor::Call

RetryInterceptor::Call::Call(HijackedCall hijacked_call)
    : hijacked_call_(std::move(hijacked_call)) {}

void RetryInterceptor::Call::StartAttempt() {
  auto call_initiator = hijacked_call_.MakeCall();
  auto* arena = call_initiator.arena();
  arena->MakeRefCounted<Attempt>(Ref())->Start(std::move(call_initiator));
}

////////////////////////////////////////////////////////////////////////////////
// RetryInterceptor::Attempt

RetryInterceptor::Attempt::Attempt(RefCountedPtr<Call> call)
    : reader_(call->request_buffer()), call_(std::move(call)) {}

void RetryInterceptor::Attempt::Start(CallInitiator call_initiator) {
  call_initiator.SpawnGuarded("client_to_server", [self = Ref()]() {
    return TrySeq(self->reader_.PullClientInitialMetadata(),
                  [](ClientMetadataHandle client_initial_metadata) {

                  });
  });
}

}  // namespace grpc_core

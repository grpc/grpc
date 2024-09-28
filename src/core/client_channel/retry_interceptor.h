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

#ifndef RETRY_INTERCEPTOR_H
#define RETRY_INTERCEPTOR_H

#include "src/core/call/request_buffer.h"
#include "src/core/lib/transport/interception_chain.h"

namespace grpc_core {

class RetryInterceptor : public Interceptor {
 public:
 protected:
  void InterceptCall(UnstartedCallHandler unstarted_call_handler) override;

 private:
  class Call : public RefCounted<Call, NonPolymorphicRefCount, UnrefCallDtor> {
   public:
    explicit Call(HijackedCall hijacked_call);

    void StartAttempt();

    RequestBuffer* request_buffer() { return &request_buffer_; }

   private:
    RequestBuffer request_buffer_;
    HijackedCall hijacked_call_;
  };

  class Attempt
      : public RefCounted<Attempt, NonPolymorphicRefCount, UnrefCallDtor> {
   public:
    explicit Attempt(RefCountedPtr<Call> call);

    void Start(CallInitiator call_initiator);

   private:
    RequestBuffer::Reader reader_;
    RefCountedPtr<Call> call_;
  };
};

}  // namespace grpc_core

#endif

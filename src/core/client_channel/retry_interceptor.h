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
#include "src/core/filter/filter_args.h"
#include "src/core/lib/transport/interception_chain.h"

namespace grpc_core {

class RetryInterceptor : public Interceptor {
 public:
  static RefCountedPtr<RetryInterceptor> Create(const ChannelArgs&,
                                                const FilterArgs&) {
    return MakeRefCounted<RetryInterceptor>();
  }

  void Orphaned() override {}

 protected:
  void InterceptCall(UnstartedCallHandler unstarted_call_handler) override;

 private:
  class Attempt;

  class Call : public RefCounted<Call, NonPolymorphicRefCount, UnrefCallDtor> {
   public:
    Call(RefCountedPtr<RetryInterceptor> interceptor, CallHandler call_handler);

    void StartAttempt();
    void Start();

    RequestBuffer* request_buffer() { return &request_buffer_; }
    CallHandler* call_handler() { return &call_handler_; }
    RetryInterceptor* interceptor() { return interceptor_.get(); }
    // if nullopt --> commit & don't retry
    // if duration --> retry after duration
    absl::optional<Duration> ShouldRetry(const ServerMetadata& md);

   private:
    void MaybeCommit(size_t buffered);
    auto ClientToBuffer();

    RequestBuffer request_buffer_;
    CallHandler call_handler_;
    RefCountedPtr<RetryInterceptor> interceptor_;
    RefCountedPtr<Attempt> current_attempt_;
  };

  class Attempt
      : public RefCounted<Attempt, NonPolymorphicRefCount, UnrefCallDtor> {
   public:
    explicit Attempt(RefCountedPtr<Call> call);

    void Start();
    void Cancel();
    void Commit();
    RequestBuffer::Reader* reader() { return &reader_; }

   private:
    auto ServerToClient();
    auto ServerToClientGotInitialMetadata(ServerMetadataHandle md);
    auto ServerToClientGotTrailersOnlyResponse();

    RequestBuffer::Reader reader_;
    RefCountedPtr<Call> call_;
    CallInitiator initiator_;
  };

  size_t MaxBuffered() const;
};

}  // namespace grpc_core

#endif

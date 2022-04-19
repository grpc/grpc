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

#ifndef GRPC_TEST_CPP_END2END_CONNECTION_DELAY_INJECTOR_H
#define GRPC_TEST_CPP_END2END_CONNECTION_DELAY_INJECTOR_H

#include <memory>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/tcp_client.h"
#include "src/core/lib/iomgr/timer.h"

namespace grpc {
namespace testing {

// Allows injecting connection-establishment delays into C-core.
// Typical usage:
//
//  // At grpc_init() time.
//  ConnectionAttemptInjector::Init();
//
//  // When an injection is desired.
//  ConnectionDelayInjector delay_injector(grpc_core::Duration::Seconds(10));
//  delay_injector.Start();
//
// The injection is global, so there must be only one ConnectionAttemptInjector
// object at any one time.
class ConnectionAttemptInjector {
 public:
  // Global initializer.  Replaces the iomgr TCP client vtable.
  // Must be called exactly once before any TCP connections are established.
  static void Init();

  virtual ~ConnectionAttemptInjector();

  // Must be called after instantiation.
  void Start();

  // Invoked for every TCP connection attempt.
  // Implementations must eventually either invoke the closure
  // themselves or delegate to the iomgr implementation by calling
  // AttemptConnection().  QueuedAttempt may be used to queue an attempt
  // for asynchronous processing.
  virtual void HandleConnection(grpc_closure* closure, grpc_endpoint** ep,
                                grpc_pollset_set* interested_parties,
                                const grpc_channel_args* channel_args,
                                const grpc_resolved_address* addr,
                                grpc_core::Timestamp deadline) = 0;

 protected:
  // Represents a queued attempt.
  // The caller must invoke either Resume() or Fail() before destroying.
  class QueuedAttempt {
   public:
    QueuedAttempt(grpc_closure* closure, grpc_endpoint** ep,
                  grpc_pollset_set* interested_parties,
                  const grpc_channel_args* channel_args,
                  const grpc_resolved_address* addr,
                  grpc_core::Timestamp deadline)
        : closure_(closure),
          endpoint_(ep),
          interested_parties_(interested_parties),
          channel_args_(grpc_channel_args_copy(channel_args)),
          deadline_(deadline) {
      memcpy(&address_, addr, sizeof(address_));
    }

    ~QueuedAttempt() {
      GPR_ASSERT(closure_ == nullptr);
      grpc_channel_args_destroy(channel_args_);
    }

    // Caller must invoke this from a thread with an ExecCtx.
    void Resume() {
      GPR_ASSERT(closure_ != nullptr);
      AttemptConnection(closure_, endpoint_, interested_parties_, channel_args_,
                        &address_, deadline_);
      closure_ = nullptr;
    }

    // Caller must invoke this from a thread with an ExecCtx.
    void Fail(grpc_error_handle error) {
      GPR_ASSERT(closure_ != nullptr);
      grpc_core::ExecCtx::Run(DEBUG_LOCATION, closure_, error);
      closure_ = nullptr;
    }

   private:
    grpc_closure* closure_;
    grpc_endpoint** endpoint_;
    grpc_pollset_set* interested_parties_;
    const grpc_channel_args* channel_args_;
    grpc_resolved_address address_;
    grpc_core::Timestamp deadline_;
  };

  // Injects a delay before continuing a connection attempt.
  class InjectedDelay {
   public:
    virtual ~InjectedDelay() = default;

    InjectedDelay(grpc_core::Duration duration, grpc_closure* closure,
                  grpc_endpoint** ep, grpc_pollset_set* interested_parties,
                  const grpc_channel_args* channel_args,
                  const grpc_resolved_address* addr,
                  grpc_core::Timestamp deadline);

   private:
    // Subclasses can override to perform an action when the attempt resumes.
    virtual void BeforeResumingAction() {}

    static void TimerCallback(void* arg, grpc_error_handle /*error*/);

    QueuedAttempt attempt_;
    grpc_timer timer_;
    grpc_closure timer_callback_;
  };

  static void AttemptConnection(grpc_closure* closure, grpc_endpoint** ep,
                                grpc_pollset_set* interested_parties,
                                const grpc_channel_args* channel_args,
                                const grpc_resolved_address* addr,
                                grpc_core::Timestamp deadline);
};

// A concrete implementation that injects a fixed delay.
class ConnectionDelayInjector : public ConnectionAttemptInjector {
 public:
  explicit ConnectionDelayInjector(grpc_core::Duration duration)
      : duration_(duration) {}

  void HandleConnection(grpc_closure* closure, grpc_endpoint** ep,
                        grpc_pollset_set* interested_parties,
                        const grpc_channel_args* channel_args,
                        const grpc_resolved_address* addr,
                        grpc_core::Timestamp deadline) override;

 private:
  grpc_core::Duration duration_;
};

}  // namespace testing
}  // namespace grpc

#endif  // GRPC_TEST_CPP_END2END_CONNECTION_DELAY_INJECTOR_H

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
//  ConnectionDelayInjector delay_injector(grpc_core::Duration::Seconds(10));
//
// When ConnectionDelayInjector (or any other subclass of
// ConnectionAttemptInjector) is instantiated, it replaces the iomgr
// TCP client vtable, and it sets it back to the original value when it
// is destroyed.
//
// The injection is global, so there must be only one ConnectionAttemptInjector
// object at any one time.
class ConnectionAttemptInjector {
 public:
  ConnectionAttemptInjector();
  virtual ~ConnectionAttemptInjector();

  // Invoked for every TCP connection attempt.
  // Implementations must eventually either invoke the closure
  // themselves or delegate to the iomgr implementation by calling
  // AttemptConnection().
  virtual void HandleConnection(grpc_closure* closure, grpc_endpoint** ep,
                                grpc_pollset_set* interested_parties,
                                const grpc_channel_args* channel_args,
                                const grpc_resolved_address* addr,
                                grpc_core::Timestamp deadline) = 0;

 protected:
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
  class InjectedDelay;

  grpc_core::Duration duration_;
};

}  // namespace testing
}  // namespace grpc

#endif  // GRPC_TEST_CPP_END2END_CONNECTION_DELAY_INJECTOR_H

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

#include "src/core/lib/gprpp/time.h"

namespace grpc {
namespace testing {

// Allows injecting connection-establishment delays into C-core.
// Typical usage:
//
//  ConnectionDelayInjector delay_injector;
//  auto scoped_delay =
//      delay_injector.SetDelay(grpc_core::Duration::Seconds(10));
//
// When ConnectionDelayInjector is instantiated, it replaces the iomgr
// TCP client vtable, and it sets it back to the original value when it
// is destroyed.
//
// When SetDelay() is called, it sets the global delay, which will
// automatically be unset when the result goes out of scope.
//
// The injection is global, so there must be only one ConnectionDelayInjector
// object at any one time, and there must be only one scoped delay in effect
// at any one time.
class ConnectionDelayInjector {
 public:
  class InjectedDelay {
   public:
    ~InjectedDelay();
  };

  ConnectionDelayInjector();
  ~ConnectionDelayInjector();

  std::unique_ptr<InjectedDelay> SetDelay(grpc_core::Duration duration);
};

}  // namespace testing
}  // namespace grpc

#endif  // GRPC_TEST_CPP_END2END_CONNECTION_DELAY_INJECTOR_H

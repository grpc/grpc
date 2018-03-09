/*
 *
 * Copyright 2018 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef GRPCPP_IMPL_CODEGEN_CONNECTIVITY_H
#define GRPCPP_IMPL_CODEGEN_CONNECTIVITY_H

#include <grpc/impl/codegen/connectivity_state.h>

namespace grpc {
class ConnectivityState {
 public:
  enum State {
    IDLE = 0,
    CONNECTING = 1,
    READY = 2,
    TRANSIENT_FAILURE = 3,
    SHUTDOWN = 4
  };

  ConnectivityState() : state_(IDLE) {}

  ConnectivityState(State state) : state_(state) {
    static_assert(IDLE == static_cast<State>(GRPC_CHANNEL_IDLE),
                  "gRPC Core/C++ connectivity state value mismatch");
    static_assert(CONNECTING == static_cast<State>(GRPC_CHANNEL_CONNECTING),
                  "gRPC Core/C++ connectivity state value mismatch");
    static_assert(READY == static_cast<State>(GRPC_CHANNEL_READY),
                  "gRPC Core/C++ connectivity state value mismatch");
    static_assert(
        TRANSIENT_FAILURE == static_cast<State>(GRPC_CHANNEL_TRANSIENT_FAILURE),
        "gRPC Core/C++ connectivity state value mismatch");
    static_assert(SHUTDOWN == static_cast<State>(GRPC_CHANNEL_SHUTDOWN),
                  "gRPC Core/C++ connectivity state value mismatch");
  }
  ConnectivityState(grpc_connectivity_state state)
      : state_(static_cast<State>(state)) {}

  ConnectivityState(const ConnectivityState& state) : state_(state.state_) {}

  ConnectivityState(ConnectivityState&& state) : state_(state.state_) {}

  ConnectivityState& operator=(const ConnectivityState& state) {
    state_ = state.state_;
    return *this;
  }

  ConnectivityState& operator=(ConnectivityState&& state) {
    state_ = state.state_;
    return *this;
  }

  /// Provide a conversion to State to support comparison against a
  /// specific connectivity state
  operator State() const { return state_; }

  /// This conversion operator is needed to maintain API compatibility
  /// with older release versions of the C++ API that returned
  /// grpc_connectivity_state from Channel rather than grpc::ConnectivityState
  operator grpc_connectivity_state() const {
    return static_cast<grpc_connectivity_state>(state_);
  }

  /// Having 2 ambiguous enum conversions is ambiguous for gtest which needs
  /// to print it as an integral type, so provide a general integral conversion
  template <class T>
  operator T() const {
    static_assert(std::is_integral<T>::value,
                  "No implicit conversion to non-integer type");
    return static_cast<T>(state_);
  }

 private:
  State state_;
};

}  // namespace grpc

#endif  // GRPCPP_IMPL_CODEGEN_CONNECTIVITY_H

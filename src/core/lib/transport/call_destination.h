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

#ifndef GRPC_SRC_CORE_LIB_TRANSPORT_CALL_DESTINATION_H
#define GRPC_SRC_CORE_LIB_TRANSPORT_CALL_DESTINATION_H

#include <grpc/support/port_platform.h>

#include <memory>

#include "call_filters.h"
#include "metadata.h"

#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/transport/call_spine.h"

namespace grpc_core {

// CallDestination is responsible for starting an UnstartedCallHandler
// and then processing operations on the resulting CallHandler.
//
// Examples of CallDestinations include:
// - a client transport
// - the server API
// - a load-balanced call in the client channel
// - a hijacking filter (see DelegatingCallDestination below)
//
// FIXME: do we want this to be ref-counted?  that might not be
// desirable for the hijacking filter case, where we want the filter stack
// to own the filter rather than having every call take its own ref to every
// hijacking filter.
class CallDestination : public Orphanable {
 public:
  virtual void StartCall(UnstartedCallHandler unstarted_call_handler) = 0;
};

// A delegating CallDestination for use as a hijacking filter.
// Implementations may look at the unprocessed initial metadata
// and decide to do one of two things:
//
// 1. It can be a no-op.  In this case, it will simply pass the
//    unstarted_call_handler to the wrapped CallDestination.
//
// 2. It can hijack the call by doing the following:
//    - Start unstarted_call_handler and take ownership of the
//      resulting handler.
//    - Create a new CallInitiator/UnstartedCallHandler pair, and pass
//      that new UnstartedCallHandler down to the wrapped CallDestination.
//    - The implementation is then responsible for forwarding between
//      the started handler and the new initiator.  Note that in
//      simple cases, this can be done via ForwardCall().
class DelegatingCallDestination : public CallDestination {
 protected:
  explicit DelegatingCallDestination(
      OrphanablePtr<CallDestination> wrapped_destination)
      : wrapped_destination_(std::move(wrapped_destination)) {}

  CallDestination* wrapped_destination() const {
    return wrapped_destination_.get();
  }

 private:
  OrphanablePtr<CallDestination> wrapped_destination_;
};

class InterceptionChain : public RefCounted<InterceptionChain> {
 public:
  class Builder {
   public:
    template <typename T>
    absl::void_t<typename T::Call> Add() {
      AddEntity<T>();
    };

    template <typename T>
    absl::enable_if_t<std::is_base_of<CallDestination, T>::value> Add() {
      AddEntity<T>();
    };

    absl::StatusOr<RefCountedPtr<InterceptionChain>> Build();

   private:
    struct Filter {};

    struct Interceptor {
      std::vector<Filter> filters;
    };

    struct Entity {
      size_t size;
      size_t alignment;
      absl::Status (*init)(void* data, const ChannelArgs& args);
      void (*destroy)(void* data);
    };

    template <typename T>
    void AddEntity() {
      Entity entity;
      entity.size = sizeof(T);
      entity.alignment = alignof(T);
      entity.init = [](void* data, const ChannelArgs& args) {
        auto val = T::Create(args);
        if (!val.ok()) return val.status();
        new (data) T(std::move(val).value());
        return absl::OkStatus();
      };
      entity.destroy = [](void* data) { static_cast<T*>(data)->~T(); };
      entities.push_back(entity);
    }

    std::vector<Interceptor> interceptors;
    std::vector<Filter> building_filters;
    std::vector<Entity> entities;
  };

  CallInitiator MakeCall(ClientMetadataHandle metadata);
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_TRANSPORT_CALL_DESTINATION_H

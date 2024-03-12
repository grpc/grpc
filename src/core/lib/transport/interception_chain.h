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

#ifndef GRPC_SRC_CORE_LIB_TRANSPORT_INTERCEPTION_CHAIN_H
#define GRPC_SRC_CORE_LIB_TRANSPORT_INTERCEPTION_CHAIN_H

#include <grpc/support/port_platform.h>

#include <vector>

#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/transport/call_destination.h"
#include "src/core/lib/transport/call_filters.h"
#include "src/core/lib/transport/call_spine.h"
#include "src/core/lib/transport/metadata.h"

namespace grpc_core {

class InterceptionChain;

// A delegating CallDestination for use as a hijacking filter.
// Implementations may look at the unprocessed initial metadata
// and decide to do one of two things:
//
// 1. It can hijack the call. Returns a HijackedCall object that can
//    be used to start new calls with the same metadata.
//
// 2. It can consume the call by calling `Consume`.
class Interceptor : public CallDestination {
 protected:
  class HijackedCall {
   public:
    CallInitiator MakeCall();
    CallInitiator MakeLastCall() {
      return MakeCallWithMetadata(std::move(metadata_));
    }

    CallHandler& call_handler() { return call_handler_; }

   private:
    friend class Interceptor;
    HijackedCall(ClientMetadataHandle metadata, CallDestination* destination,
                 CallHandler call_handler)
        : metadata_(std::move(metadata)),
          destination_(destination),
          call_handler_(std::move(call_handler)) {}

    CallInitiator MakeCallWithMetadata(ClientMetadataHandle metadata);

    ClientMetadataHandle metadata_;
    CallDestination* destination_;
    CallHandler call_handler_;
  };

  auto Hijack(UnstartedCallHandler unstarted_call_handler) {
    auto call_handler = Consume(std::move(unstarted_call_handler));
    return Map(call_handler.PullClientInitialMetadata(),
               [call_handler,
                this](ValueOrFailure<ClientMetadataHandle> metadata) mutable
               -> ValueOrFailure<HijackedCall> {
                 if (!metadata.ok()) return Failure{};
                 return HijackedCall(std::move(metadata.value()),
                                     wrapped_destination_,
                                     std::move(call_handler));
               });
  }

  CallHandler Consume(UnstartedCallHandler unstarted_call_handler) {
    return unstarted_call_handler.StartCall(filter_stack_);
  }

 private:
  friend class InterceptionChain;

  CallDestination* wrapped_destination_;
  RefCountedPtr<CallFilters::Stack> filter_stack_;
};

class InterceptionChain final : public RefCounted<InterceptionChain>,
                                public CallDestination {
 public:
  class Builder {
   public:
    explicit Builder(std::shared_ptr<CallDestination> final_destination)
        : final_destination_(std::move(final_destination)) {
      GPR_ASSERT(final_destination_ != nullptr);
    }

    // Add a filter with a `Call` class as an inner member.
    // Call class must be one compatible with the filters described in
    // call_filters.h.
    template <typename T>
    absl::enable_if_t<sizeof(typename T::Call), Builder&> Add() {
      building_filters_.push_back({
          Footprint::For<T>(),
          [](void* filter, const ChannelArgs& args) {
            auto f = T::Create(args, {});
            if (!f.ok()) return f.status();
            new (filter) T(std::move(f.value()));
            return absl::OkStatus();
          },
          [](CallFilters::StackBuilder& stack_builder, void* filter) {
            stack_builder.Add(static_cast<T*>(filter));
          },
      });
      return *this;
    };

    // Add a filter that is an interceptor - one that can hijack calls.
    template <typename T>
    absl::enable_if_t<std::is_base_of<Interceptor, T>::value, Builder&> Add() {
      interceptors_.emplace_back(
          Footprint::For<T>(), std::move(building_filters_),
          [](void* interceptor, const ChannelArgs& args) {
            auto i = T::Create(args, {});
            if (!i.ok()) return i.status();
            new (interceptor) T(std::move(i.value()));
            return static_cast<Interceptor*>(interceptor);
          });
      building_filters_.clear();
      return *this;
    };

    absl::StatusOr<RefCountedPtr<InterceptionChain>> Build(
        const ChannelArgs& args);

   private:
    struct Footprint {
      template <typename T>
      static Footprint For() {
        return {
            sizeof(T),
            alignof(T),
            [](void* p) { static_cast<T*>(p)->~T(); },
        };
      }

      size_t size;
      size_t alignment;
      void (*destroy)(void* p);
      size_t offset = 0;
      void* OffsetPtr(void* base) const {
        return static_cast<char*>(base) + offset;
      }
    };

    struct FilterDef {
      Footprint footprint;
      absl::Status (*init)(void* filter, const ChannelArgs& args);
      void (*add_to_stack_builder)(CallFilters::StackBuilder& stack_builder,
                                   void* filter);
    };

    struct InterceptorDef {
      InterceptorDef(Footprint footprint, std::vector<FilterDef> filters,
                     absl::StatusOr<Interceptor*> (*init)(
                         void* interceptor, const ChannelArgs& args))
          : footprint(footprint), filters(std::move(filters)), init(init) {}
      Footprint footprint;
      std::vector<FilterDef> filters;
      absl::StatusOr<Interceptor*> (*init)(void* interceptor,
                                           const ChannelArgs& args);
    };

    std::vector<InterceptorDef> interceptors_;
    std::vector<FilterDef> building_filters_;
    std::shared_ptr<CallDestination> final_destination_;
  };

  void StartCall(UnstartedCallHandler unstarted_call_handler) override {
    GPR_DEBUG_ASSERT(GetContext<Activity>() == unstarted_call_handler.party());
    chain_->first_destination->StartCall(std::move(unstarted_call_handler));
  }

 private:
  struct Destructor {
    void (*destroy)(void* p);
    size_t offset;
  };

  struct Chain : public RefCounted<Chain> {
    Chain(CallDestination* first_destination,
          std::vector<Destructor> destructors, void* chain_data,
          std::shared_ptr<CallDestination> final_destination)
        : first_destination(first_destination),
          destructors(std::move(destructors)),
          chain_data(chain_data),
          final_destination(std::move(final_destination)) {
      GPR_ASSERT(this->final_destination != nullptr);
      if (this->first_destination == nullptr) {
        this->first_destination = this->final_destination.get();
      }
    }
    ~Chain();
    CallDestination* first_destination;
    std::vector<Destructor> destructors;
    void* chain_data;
    std::shared_ptr<CallDestination> final_destination;
  };

 public:
  explicit InterceptionChain(RefCountedPtr<Chain> chain)
      : chain_(std::move(chain)) {}

 private:
  const RefCountedPtr<Chain> chain_;
};

}  // namespace grpc_core

#endif

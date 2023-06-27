//
// Copyright 2015 gRPC authors.
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
//

#ifndef GRPC_SRC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_ENDPOINT_LIST_H
#define GRPC_SRC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_ENDPOINT_LIST_H

#include <grpc/support/port_platform.h>

#include <stdlib.h>

#include <memory>
#include <utility>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/types/optional.h"

#include <grpc/impl/connectivity_state.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/work_serializer.h"
#include "src/core/lib/load_balancing/lb_policy.h"
#include "src/core/lib/load_balancing/subchannel_interface.h"
#include "src/core/lib/resolver/server_address.h"

namespace grpc_core {

// A list of endpoints for use in a petiole LB policy.  Each endpoint may
// have one or more addresses, which will be passed down to a pick_first
// child policy.
//
// To use this, a petiole policy must define its own subclass of both
// EndpointList and EndpointList::Endpoint, like so:
/*
class MyEndpointList : public EndpointList {
 public:
  MyEndpointList(RefCountedPtr<MyLbPolicy> lb_policy,
                 const ServerAddressList& addresses, const ChannelArgs& args)
      : EndpointList(std::move(lb_policy),
                     GRPC_TRACE_FLAG_ENABLED(grpc_my_tracer)
                         ? "MyEndpointList"
                         : nullptr) {
    Init(addresses, args,
         [&](RefCountedPtr<MyEndpointList> endpoint_list,
             const ServerAddress& address, const ChannelArgs& args) {
           return MakeOrphanable<MyEndpoint>(
               std::move(endpoint_list), address, args,
               policy<MyLbPolicy>()->work_serializer());
         });
  }

 private:
  class MyEndpoint : public Endpoint {
   public:
    MyEndpoint(RefCountedPtr<MyEndpointList> endpoint_list,
               const ServerAddress& address, const ChannelArgs& args,
               std::shared_ptr<WorkSerializer> work_serializer)
        : Endpoint(std::move(endpoint_list)) {
      Init(address, args, std::move(work_serializer));
    }

   private:
    void OnStateUpdate(
        absl::optional<grpc_connectivity_state> old_state,
        grpc_connectivity_state new_state,
        const absl::Status& status) override {
      // ...handle connectivity state change...
    }
  };

  LoadBalancingPolicy::ChannelControlHelper* channel_control_helper()
      const override {
    return policy<MyLbPolicy>()->channel_control_helper();
  }
};
*/
// FIXME: Consider wrapping this in an LB policy subclass for petiole
// policies to inherit from
class EndpointList : public InternallyRefCounted<EndpointList> {
 public:
  // An individual endpoint.
  class Endpoint : public InternallyRefCounted<Endpoint> {
   public:
    ~Endpoint() override { endpoint_list_.reset(DEBUG_LOCATION, "Endpoint"); }

    void Orphan() override;

    void ResetBackoffLocked();
    void ExitIdleLocked();

    absl::optional<grpc_connectivity_state> connectivity_state() const {
      return connectivity_state_;
    }
    RefCountedPtr<LoadBalancingPolicy::SubchannelPicker> picker() const {
      return picker_;
    }

   protected:
    // We use two-phase initialization here to ensure that the vtable is
    // initialized before we need to use it.  Subclass must invoke Init()
    // from inside its ctor.
    explicit Endpoint(RefCountedPtr<EndpointList> endpoint_list)
        : endpoint_list_(std::move(endpoint_list)) {}

    void Init(const ServerAddress& address, const ChannelArgs& args,
              std::shared_ptr<WorkSerializer> work_serializer);

    // Templated for convenience, to provide a short-hand for
    // down-casting in the caller.
    template <typename T>
    T* endpoint_list() const {
      return static_cast<T*>(endpoint_list_.get());
    }

    // Templated for convenience, to provide a short-hand for down-casting
    // in the caller.
    template <typename T>
    T* policy() const {
      return endpoint_list_->policy<T>();
    }

    // Returns the index of this endpoint within the EndpointList.
    // Intended for trace logging.
    size_t Index() const;

   private:
    class Helper;

    // Called when the child policy reports a connectivity state update.
    virtual void OnStateUpdate(
        absl::optional<grpc_connectivity_state> old_state,
        grpc_connectivity_state new_state, const absl::Status& status) = 0;

    // Called to create a subchannel.  Subclasses may override.
    virtual RefCountedPtr<SubchannelInterface> CreateSubchannel(
        const grpc_resolved_address& address,
        const ChannelArgs& per_address_args, const ChannelArgs& args);

    RefCountedPtr<EndpointList> endpoint_list_;

    OrphanablePtr<LoadBalancingPolicy> child_policy_;
    absl::optional<grpc_connectivity_state> connectivity_state_;
    RefCountedPtr<LoadBalancingPolicy::SubchannelPicker> picker_;
  };

  ~EndpointList() override { policy_.reset(DEBUG_LOCATION, "EndpointList"); }

  void Orphan() override {
    endpoints_.clear();
    Unref();
  }

  size_t size() const { return endpoints_.size(); }

  const std::vector<OrphanablePtr<Endpoint>>& endpoints() const {
    return endpoints_;
  }

  void ResetBackoffLocked();

 protected:
  // We use two-phase initialization here to ensure that the vtable is
  // initialized before we need to use it.  Subclass must invoke Init()
  // from inside its ctor.
  EndpointList(RefCountedPtr<LoadBalancingPolicy> policy, const char* tracer)
      : policy_(std::move(policy)), tracer_(tracer) {}

  void Init(const ServerAddressList& addresses, const ChannelArgs& args,
            absl::AnyInvocable<OrphanablePtr<Endpoint>(
                RefCountedPtr<EndpointList>, const ServerAddress&,
                const ChannelArgs&)>
                create_endpoint);

  // Templated for convenience, to provide a short-hand for down-casting
  // in the caller.
  template <typename T>
  T* policy() const {
    return static_cast<T*>(policy_.get());
  }

  // Returns true if all endpoints have seen their initial connectivity
  // state notification.
  bool AllEndpointsSeenInitialState() const;

 private:
  // Returns the parent policy's helper.  Needed because the accessor
  // method is protected on LoadBalancingPolicy.
  virtual LoadBalancingPolicy::ChannelControlHelper* channel_control_helper()
      const = 0;

  RefCountedPtr<LoadBalancingPolicy> policy_;
  const char* tracer_;
  std::vector<OrphanablePtr<Endpoint>> endpoints_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_ENDPOINT_LIST_H

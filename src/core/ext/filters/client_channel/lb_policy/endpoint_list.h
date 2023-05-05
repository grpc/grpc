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

#include <grpc/support/port_platform.h>

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/types/optional.h"

#include <grpc/impl/connectivity_state.h>
#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/load_balancing/lb_policy.h"
#include "src/core/lib/resolver/server_address.h"

#ifndef GRPC_SRC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_ENDPOINT_LIST_H
#define GRPC_SRC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_ENDPOINT_LIST_H

namespace grpc_core {

class EndpointList : public InternallyRefCounted<EndpointList> {
 public:
  ~EndpointList() override { policy_.reset(DEBUG_LOCATION, "EndpointList"); }

  void Orphan() override {
    endpoints_.clear();
    Unref();
  }

  size_t size() const { return endpoints_.size(); }

  void ResetBackoffLocked();

 protected:
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
    Endpoint(RefCountedPtr<EndpointList> endpoint_list,
             const ServerAddress& address, const ChannelArgs& args,
             std::shared_ptr<WorkSerializer> work_serializer);

    template<typename T>
    T* endpoint_list() const { return static_cast<T*>(endpoint_list_.get()); }

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
        ServerAddress address, const ChannelArgs& args);

    RefCountedPtr<EndpointList> endpoint_list_;
    OrphanablePtr<LoadBalancingPolicy> child_policy_;
    absl::optional<grpc_connectivity_state> connectivity_state_;
    RefCountedPtr<LoadBalancingPolicy::SubchannelPicker> picker_;
  };

  EndpointList(RefCountedPtr<LoadBalancingPolicy> policy, const char* tracer)
      : policy_(std::move(policy)), tracer_(tracer) {}

  void Init(const ServerAddressList& addresses, const ChannelArgs& args,
            absl::AnyInvocable<OrphanablePtr<Endpoint>(
                RefCountedPtr<EndpointList>, const ServerAddress&,
                const ChannelArgs&)> create_endpoint);

  template<typename T>
  T* policy() const { return static_cast<T*>(policy_.get()); }

  const std::vector<OrphanablePtr<Endpoint>>& endpoints() const {
    return endpoints_;
  }

  // Returns true if all endpoints have seen their initial connectivity
  // state notification.
  bool AllEndpointsSeenInitialState() const;

 private:
  virtual LoadBalancingPolicy::ChannelControlHelper* channel_control_helper()
      const = 0;

  RefCountedPtr<LoadBalancingPolicy> policy_;
  const char* tracer_;
  std::vector<OrphanablePtr<Endpoint>> endpoints_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_ENDPOINT_LIST_H

/*
 *
 * Copyright 2019 gRPC authors.
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

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_SUBCHANNEL_INTERFACE_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_SUBCHANNEL_INTERFACE_H

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/client_channel/subchannel.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"

namespace grpc_core {

class SubchannelInterface : public RefCounted<SubchannelInterface> {
 public:
  class ConnectivityStateWatcher {
   public:
    virtual ~ConnectivityStateWatcher() = default;

    // Will be invoked whenever the subchannel's connectivity state
    // changes.  There will be only one invocation of this method on a
    // given watcher instance at any given time.
    //
    // When the state changes to READY, connected_subchannel will
    // contain a ref to the connected subchannel.  When it changes from
    // READY to some other state, the implementation must release its
    // ref to the connected subchannel.
    virtual void OnConnectivityStateChange(
        grpc_connectivity_state new_state,
        RefCountedPtr<ConnectedSubchannel> connected_subchannel)  // NOLINT
        GRPC_ABSTRACT;

    GRPC_ABSTRACT_BASE_CLASS
  };

  virtual ~SubchannelInterface() = default;

  // Returns the current connectivity state of the subchannel.
  virtual grpc_connectivity_state GetConnectivityState(
      RefCountedPtr<ConnectedSubchannel>* connected_subchannel) GRPC_ABSTRACT;

  // Starts watching the subchannel's connectivity state.
  // The first callback to the watcher will be delivered when the
  // subchannel's connectivity state becomes a value other than
  // initial_state, which may happen immediately.
  // Subsequent callbacks will be delivered as the subchannel's state
  // changes.
  // The watcher will be destroyed either when the subchannel is
  // destroyed or when CancelConnectivityStateWatch() is called.
  // There can be only one watcher of a given subchannel.  It is not
  // valid to call this method a second time without first cancelling
  // the previous watcher using CancelConnectivityStateWatch().
  virtual void WatchConnectivityState(
      grpc_connectivity_state initial_state,
      UniquePtr<ConnectivityStateWatcher> watcher) GRPC_ABSTRACT;

  // Cancels a connectivity state watch.
  // If the watcher has already been destroyed, this is a no-op.
  virtual void CancelConnectivityStateWatch(
      ConnectivityStateWatcher* watcher) GRPC_ABSTRACT;

  // Attempt to connect to the backend.  Has no effect if already connected.
  virtual void AttemptToConnect() GRPC_ABSTRACT;

  // TODO(roth): These methods should be removed from this interface to
  // bettter hide grpc-specific functionality from the LB policy API.
  virtual channelz::SubchannelNode* channelz_node() GRPC_ABSTRACT;
  virtual void ResetBackoff() GRPC_ABSTRACT;

  GRPC_ABSTRACT_BASE_CLASS
};

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_SUBCHANNEL_INTERFACE_H */

//
// Copyright 2022 gRPC authors.
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

#ifndef GRPC_CORE_EXT_XDS_XDS_TRANSPORT_H
#define GRPC_CORE_EXT_XDS_XDS_TRANSPORT_H

#include <grpc/support/port_platform.h>

#include <functional>
#include <memory>
#include <string>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"

#include "src/core/ext/xds/xds_bootstrap.h"
#include "src/core/lib/gprpp/orphanable.h"

namespace grpc_core {

// A factory for creating new XdsTransport instances.
class XdsTransportFactory : public InternallyRefCounted<XdsTransportFactory> {
 public:
  // Represents a transport for xDS communication (e.g., a gRPC channel).
  class XdsTransport : public InternallyRefCounted<XdsTransport> {
   public:
    // Represents a bidi streaming RPC call.
    class StreamingCall : public InternallyRefCounted<StreamingCall> {
     public:
      // An interface for handling events on a streaming call.
      class EventHandler {
       public:
        virtual ~EventHandler() = default;

        // Called when a SendMessage() operation completes.
        virtual void OnRequestSent(bool ok) = 0;
        // Called when a message is received on the stream.
        virtual void OnRecvMessage(absl::string_view payload) = 0;
        // Called when status is received on the stream.
        virtual void OnStatusReceived(absl::Status status) = 0;
      };

      // Sends a message on the stream.  When the message has been sent,
      // the EventHandler::OnRequestSent() method will be called.
      // Only one message will be in flight at a time; subsequent
      // messages will not be sent until this one is done.
      virtual void SendMessage(std::string payload) = 0;
    };

    // Create a streaming call on this transport for the specified method.
    // Events on the stream will be reported to event_handler.
    virtual OrphanablePtr<StreamingCall> CreateStreamingCall(
        const char* method,
        std::unique_ptr<StreamingCall::EventHandler> event_handler) = 0;

    // Resets connection backoff for the transport.
    virtual void ResetBackoff() = 0;
  };

  // An interface for reporting connectivity state events for a transport.
  //
  // A transport can be in one of three states:
  // - CONNECTING: transport is attempting to establish a connection
  // - READY: transport is connected and ready to start streams
  // - TRANSIENT_FAILURE: connection attempt has failed
  //
  // A transport is assumed to start in state CONNECTING.  Expected
  // transitions:
  // - CONNECTING -> READY (when connection is successfully established)
  // - CONNECTING -> TRANSIENT_FAILURE (when the connection attempt fails)
  // - TRANSIENT_FAILURE -> READY (when a connection attempt had failed
  //                               but a subsequent attempt has succeeded)
  // - READY -> CONNECTING (when an established connection fails)
  //
  // Note that a transport should not transition from TRANSIENT_FAILURE to
  // CONNECTING; once the transport has failed a connection attempt, it
  // should remain in TRANSIENT_FAILURE until a subsequent connection
  // attempt succeeds.
  class ConnectivityStateReporter {
   public:
    virtual ~ConnectivityStateReporter() = default;

    // Invoked when the transport enters state CONNECTING.
    virtual void ReportConnecting() = 0;
    // Invoked when the transport enters state READY.
    virtual void ReportReady() = 0;
    // Invoked when the transport enters state TRANSIENT_FAILURE.
    virtual void ReportTransientFailure(absl::Status status) = 0;
  };

  // Creates a new transport for the specified server.
  // The transport will use connectivity_state_reporter to report its
  // connectivity state.
  // *status will be set if there is an error creating the channel,
  // although the returned channel must still accept calls (which may fail).
  virtual OrphanablePtr<XdsTransport> Create(
      const XdsBootstrap::XdsServer& server,
      std::unique_ptr<ConnectivityStateReporter> connectivity_state_reporter,
      absl::Status* status) = 0;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_XDS_XDS_TRANSPORT_H

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

#ifndef GRPC_SRC_CORE_EXT_XDS_XDS_TRANSPORT_H
#define GRPC_SRC_CORE_EXT_XDS_XDS_TRANSPORT_H

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

  // Creates a new transport for the specified server.
  // The on_connectivity_failure callback will be invoked whenever there is
  // a connectivity failure on the transport.
  // *status will be set if there is an error creating the channel,
  // although the returned channel must still accept calls (which may fail).
  virtual OrphanablePtr<XdsTransport> Create(
      const XdsBootstrap::XdsServer& server,
      std::function<void(absl::Status)> on_connectivity_failure,
      absl::Status* status) = 0;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_XDS_XDS_TRANSPORT_H

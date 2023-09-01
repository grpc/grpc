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

#ifndef GRPC_TEST_CORE_XDS_XDS_TRANSPORT_FAKE_H
#define GRPC_TEST_CORE_XDS_XDS_TRANSPORT_FAKE_H

#include <grpc/support/port_platform.h>

#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "absl/types/optional.h"

#include "src/core/ext/xds/xds_bootstrap.h"
#include "src/core/ext/xds/xds_transport.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/sync.h"

namespace grpc_core {

class FakeXdsTransportFactory : public XdsTransportFactory {
 private:
  class FakeXdsTransport;

 public:
  static constexpr char kAdsMethod[] =
      "/envoy.service.discovery.v3.AggregatedDiscoveryService/"
      "StreamAggregatedResources";
  static constexpr char kLrsMethod[] =
      "/envoy.service.load_stats.v3.LoadReportingService/StreamLoadStats";

  class FakeStreamingCall : public XdsTransport::StreamingCall {
   public:
    FakeStreamingCall(
        RefCountedPtr<FakeXdsTransport> transport, const char* method,
        std::unique_ptr<StreamingCall::EventHandler> event_handler)
        : transport_(std::move(transport)),
          method_(method),
          event_handler_(MakeRefCounted<RefCountedEventHandler>(
              std::move(event_handler))) {}

    ~FakeStreamingCall() override;

    void Orphan() override;

    using StreamingCall::Ref;  // Make it public.

    bool HaveMessageFromClient();
    absl::optional<std::string> WaitForMessageFromClient(
        absl::Duration timeout);

    // If FakeXdsTransportFactory::SetAutoCompleteMessagesFromClient()
    // was called to set the value to false before the creation of the
    // transport that underlies this stream, then this must be called
    // to invoke EventHandler::OnRequestSent() for every message read
    // via WaitForMessageFromClient().
    void CompleteSendMessageFromClient(bool ok = true);

    void SendMessageToClient(absl::string_view payload);
    void MaybeSendStatusToClient(absl::Status status);

    bool Orphaned();

   private:
    class RefCountedEventHandler : public RefCounted<RefCountedEventHandler> {
     public:
      explicit RefCountedEventHandler(
          std::unique_ptr<StreamingCall::EventHandler> event_handler)
          : event_handler_(std::move(event_handler)) {}

      void OnRequestSent(bool ok) { event_handler_->OnRequestSent(ok); }
      void OnRecvMessage(absl::string_view payload) {
        event_handler_->OnRecvMessage(payload);
      }
      void OnStatusReceived(absl::Status status) {
        event_handler_->OnStatusReceived(std::move(status));
      }

     private:
      std::unique_ptr<StreamingCall::EventHandler> event_handler_;
    };

    void SendMessage(std::string payload) override;

    void CompleteSendMessageFromClientLocked(bool ok)
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(&mu_);

    RefCountedPtr<FakeXdsTransport> transport_;
    const char* method_;

    Mutex mu_;
    CondVar cv_;
    RefCountedPtr<RefCountedEventHandler> event_handler_ ABSL_GUARDED_BY(&mu_);
    std::deque<std::string> from_client_messages_ ABSL_GUARDED_BY(&mu_);
    bool status_sent_ ABSL_GUARDED_BY(&mu_) = false;
    bool orphaned_ ABSL_GUARDED_BY(&mu_) = false;
  };

  FakeXdsTransportFactory() = default;

  using XdsTransportFactory::Ref;  // Make it public.

  void TriggerConnectionFailure(const XdsBootstrap::XdsServer& server,
                                absl::Status status);

  // By default, FakeStreamingCall will automatically invoke
  // EventHandler::OnRequestSent() upon reading a request from the client.
  // If this is set to false, that behavior will be inhibited, and
  // EventHandler::OnRequestSent() will not be called until the test
  // expicitly calls FakeStreamingCall::CompleteSendMessageFromClient().
  //
  // This value affects all transports created after this call is
  // complete.  Any transport that already exists prior to this call
  // will not be affected.
  void SetAutoCompleteMessagesFromClient(bool value);

  // By default, FakeStreamingCall will automatically crash on
  // destruction if there are messages from the client that have not
  // been drained from the queue.  If this is set to false, that
  // behavior will be inhibited.
  //
  // This value affects all transports created after this call is
  // complete.  Any transport that already exists prior to this call
  // will not be affected.
  void SetAbortOnUndrainedMessages(bool value);

  RefCountedPtr<FakeStreamingCall> WaitForStream(
      const XdsBootstrap::XdsServer& server, const char* method,
      absl::Duration timeout);

  void Orphan() override { Unref(); }

 private:
  class FakeXdsTransport : public XdsTransport {
   public:
    FakeXdsTransport(RefCountedPtr<FakeXdsTransportFactory> factory,
                     const XdsBootstrap::XdsServer& server,
                     std::function<void(absl::Status)> on_connectivity_failure,
                     bool auto_complete_messages_from_client,
                     bool abort_on_undrained_messages)
        : factory_(std::move(factory)),
          server_(server),
          auto_complete_messages_from_client_(
              auto_complete_messages_from_client),
          abort_on_undrained_messages_(abort_on_undrained_messages),
          on_connectivity_failure_(
              MakeRefCounted<RefCountedOnConnectivityFailure>(
                  std::move(on_connectivity_failure))) {}

    void Orphan() override;

    bool auto_complete_messages_from_client() const {
      return auto_complete_messages_from_client_;
    }

    bool abort_on_undrained_messages() const {
      return abort_on_undrained_messages_;
    }

    using XdsTransport::Ref;  // Make it public.

    void TriggerConnectionFailure(absl::Status status);

    RefCountedPtr<FakeStreamingCall> WaitForStream(const char* method,
                                                   absl::Duration timeout);

    void RemoveStream(const char* method, FakeStreamingCall* call);

   private:
    class RefCountedOnConnectivityFailure
        : public RefCounted<RefCountedOnConnectivityFailure> {
     public:
      explicit RefCountedOnConnectivityFailure(
          std::function<void(absl::Status)> on_connectivity_failure)
          : on_connectivity_failure_(std::move(on_connectivity_failure)) {}

      void Run(absl::Status status) {
        on_connectivity_failure_(std::move(status));
      }

     private:
      std::function<void(absl::Status)> on_connectivity_failure_;
    };

    OrphanablePtr<StreamingCall> CreateStreamingCall(
        const char* method,
        std::unique_ptr<StreamingCall::EventHandler> event_handler) override;

    void ResetBackoff() override {}

    RefCountedPtr<FakeXdsTransportFactory> factory_;
    const XdsBootstrap::XdsServer& server_;
    const bool auto_complete_messages_from_client_;
    const bool abort_on_undrained_messages_;

    Mutex mu_;
    CondVar cv_;
    RefCountedPtr<RefCountedOnConnectivityFailure> on_connectivity_failure_
        ABSL_GUARDED_BY(&mu_);
    std::map<std::string /*method*/, RefCountedPtr<FakeStreamingCall>>
        active_calls_ ABSL_GUARDED_BY(&mu_);
  };

  OrphanablePtr<XdsTransport> Create(
      const XdsBootstrap::XdsServer& server,
      std::function<void(absl::Status)> on_connectivity_failure,
      absl::Status* status) override;

  RefCountedPtr<FakeXdsTransport> GetTransport(
      const XdsBootstrap::XdsServer& server);

  Mutex mu_;
  std::map<const XdsBootstrap::XdsServer*, RefCountedPtr<FakeXdsTransport>>
      transport_map_ ABSL_GUARDED_BY(&mu_);
  bool auto_complete_messages_from_client_ ABSL_GUARDED_BY(&mu_) = true;
  bool abort_on_undrained_messages_ ABSL_GUARDED_BY(&mu_) = true;
};

}  // namespace grpc_core

#endif  // GRPC_TEST_CORE_XDS_XDS_TRANSPORT_FAKE_H

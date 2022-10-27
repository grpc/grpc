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

#ifndef GRPC_CORE_EXT_XDS_XDS_TRANSPORT_FAKE_H
#define GRPC_CORE_EXT_XDS_XDS_TRANSPORT_FAKE_H

#include <grpc/support/port_platform.h>

#include <deque>
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
  static constexpr char kAdsV2Method[] =
      "/envoy.service.discovery.v2.AggregatedDiscoveryService/"
      "StreamAggregatedResources";

  class FakeStreamingCall : public XdsTransport::StreamingCall {
   public:
    FakeStreamingCall(
        RefCountedPtr<FakeXdsTransport> transport, const char* method,
        std::unique_ptr<StreamingCall::EventHandler> event_handler)
        : transport_(std::move(transport)),
          method_(method),
          event_handler_(MakeRefCounted<RefCountedEventHandler>(
              std::move(event_handler))) {}

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
  };

  FakeXdsTransportFactory() = default;

  using XdsTransportFactory::Ref;  // Make it public.

  void ReportConnecting(const XdsBootstrap::XdsServer& server);
  void ReportReady(const XdsBootstrap::XdsServer& server);
  void ReportTransientFailure(const XdsBootstrap::XdsServer& server,
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

  // By default, FakeTransport will immediately report to the XdsClient
  // that it is connected as soon as it is created.  If this is set to
  // false, that behavior will be inhibited, and the test must invoke
  // ReportReady() to explicitly report to the XdsClient that
  // connectivity has been established.
  //
  // This value affects all transports created after this call is
  // complete.  Any transport that already exists prior to this call
  // will not be affected.
  void SetAutoReportTransportReady(bool value);

  RefCountedPtr<FakeStreamingCall> WaitForStream(
      const XdsBootstrap::XdsServer& server, const char* method,
      absl::Duration timeout);

  void Orphan() override { Unref(); }

 private:
  class FakeXdsTransport : public XdsTransport {
   public:
    FakeXdsTransport(
        std::unique_ptr<ConnectivityStateReporter> connectivity_state_reporter,
        bool auto_complete_messages_from_client,
        bool auto_report_transport_ready);

    void Orphan() override;

    bool auto_complete_messages_from_client() const {
      return auto_complete_messages_from_client_;
    }

    using XdsTransport::Ref;  // Make it public.

    void ReportConnecting();
    void ReportReady();
    void ReportTransientFailure(absl::Status status);

    RefCountedPtr<FakeStreamingCall> WaitForStream(const char* method,
                                                   absl::Duration timeout);

    void RemoveStream(const char* method, FakeStreamingCall* call);

   private:
    OrphanablePtr<StreamingCall> CreateStreamingCall(
        const char* method,
        std::unique_ptr<StreamingCall::EventHandler> event_handler) override;

    void ResetBackoff() override {}

    const bool auto_complete_messages_from_client_;

    Mutex mu_;
    CondVar cv_;
    std::shared_ptr<ConnectivityStateReporter> connectivity_state_reporter_
        ABSL_GUARDED_BY(&mu_);
    std::map<std::string /*method*/, RefCountedPtr<FakeStreamingCall>>
        active_calls_ ABSL_GUARDED_BY(&mu_);
  };

  OrphanablePtr<XdsTransport> Create(
      const XdsBootstrap::XdsServer& server,
      std::unique_ptr<ConnectivityStateReporter> connectivity_state_reporter,
      absl::Status* status) override;

  RefCountedPtr<FakeXdsTransport> GetTransport(
      const XdsBootstrap::XdsServer& server);

  Mutex mu_;
  std::map<const XdsBootstrap::XdsServer*, RefCountedPtr<FakeXdsTransport>>
      transport_map_ ABSL_GUARDED_BY(&mu_);
  bool auto_complete_messages_from_client_ ABSL_GUARDED_BY(&mu_) = true;
  bool auto_report_transport_ready_ ABSL_GUARDED_BY(&mu_) = true;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_XDS_XDS_TRANSPORT_FAKE_H

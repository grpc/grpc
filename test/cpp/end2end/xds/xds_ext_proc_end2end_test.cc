//
// Copyright 2026 gRPC authors.
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

#include <google/protobuf/wrappers.pb.h>
#include <grpc/support/string_util.h>

#include <atomic>
#include <map>
#include <memory>
#include <optional>
#include <queue>
#include <string>
#include <utility>
#include <vector>

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/common/mutation_rules/v3/mutation_rules.pb.h"
#include "envoy/extensions/filters/http/ext_proc/v3/ext_proc.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/grpc_service/call_credentials/access_token/v3/access_token_credentials.pb.h"
#include "envoy/extensions/grpc_service/channel_credentials/google_default/v3/google_default_credentials.pb.h"
#include "envoy/extensions/grpc_service/channel_credentials/insecure/v3/insecure_credentials.pb.h"
#include "envoy/service/ext_proc/v3/external_processor.grpc.pb.h"
#include "src/core/config/config_vars.h"
#include "src/core/lib/experiments/config.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/util/sync.h"
#include "test/core/test_util/fake_stats_plugin.h"
#include "test/core/test_util/scoped_env_var.h"
#include "test/core/test_util/test_config.h"
#include "test/cpp/end2end/xds/xds_end2end_test_lib.h"
#include "test/cpp/end2end/xds/xds_utils.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

namespace grpc {
namespace testing {
namespace {

using ::envoy::extensions::filters::http::ext_proc::v3::ExternalProcessor;
using ::envoy::extensions::filters::network::http_connection_manager::v3::
    HttpFilter;

MATCHER_P2(GrpcStatusIs, code, message_matcher, "") {
  return ::testing::ExplainMatchResult(code, arg.error_code(),
                                       result_listener) &&
         ::testing::ExplainMatchResult(message_matcher, arg.error_message(),
                                       result_listener);
}

constexpr absl::string_view kFilterInstanceName = "ext_proc_instance";

constexpr char kRequestHeadersMutatedHeaderKey[] =
    "x-extproc-request-headers-mutated";
constexpr char kResponseHeadersMutatedHeaderKey[] =
    "x-extproc-response-headers-mutated";
constexpr char kResponseTrailersMutatedHeaderKey[] =
    "x-extproc-response-trailers-mutated";
constexpr char kHeaderMutatedValue[] = "yes";
constexpr char kRequestBodyMutatedSuffix[] = "-request-body-mutated";
constexpr char kResponseBodyMutatedSuffix[] = "-response-body-mutated";
constexpr char kImmediateResponseHeaderKey[] =
    "x-extproc-immediate-response-added";
constexpr char kMessage1[] = "message1";
constexpr char kMessage2[] = "message2";
constexpr char kMutatedSuffix[] = "-mutated";
constexpr char kMessage1Mutated[] = "message1-mutated";

std::string GetExtProcAttribute(
    const ::envoy::service::ext_proc::v3::ProcessingRequest& request,
    absl::string_view attribute_name) {
  auto it = request.attributes().find("envoy.filters.http.ext_proc");
  if (it == request.attributes().end()) return "";
  const auto& fields = it->second.fields();
  auto field_it = fields.find(std::string(attribute_name));
  if (field_it == fields.end()) return "";
  return field_it->second.string_value();
}

// Response construction helper functions

::envoy::service::ext_proc::v3::ProcessingResponse
MakeRequestHeadersMutationResponse(
    const std::vector<std::pair<std::string, std::string>>& set_headers,
    const std::vector<std::string>& remove_headers = {},
    bool request_drain = false) {
  ::envoy::service::ext_proc::v3::ProcessingResponse response;
  if (request_drain) {
    response.set_request_drain(true);
  }
  auto* mutation = response.mutable_request_headers()
                       ->mutable_response()
                       ->mutable_header_mutation();
  for (const auto& [key, value] : set_headers) {
    auto* header = mutation->add_set_headers();
    header->mutable_header()->set_key(key);
    header->mutable_header()->set_value(value);
  }
  for (const auto& key : remove_headers) {
    mutation->add_remove_headers(key);
  }
  return response;
}

::envoy::service::ext_proc::v3::ProcessingResponse
MakeResponseHeadersMutationResponse(
    const std::vector<std::pair<std::string, std::string>>& set_headers,
    const std::vector<std::string>& remove_headers = {},
    bool request_drain = false) {
  ::envoy::service::ext_proc::v3::ProcessingResponse response;
  if (request_drain) {
    response.set_request_drain(true);
  }
  auto* mutation = response.mutable_response_headers()
                       ->mutable_response()
                       ->mutable_header_mutation();
  for (const auto& [key, value] : set_headers) {
    auto* header = mutation->add_set_headers();
    header->mutable_header()->set_key(key);
    header->mutable_header()->set_value(value);
  }
  for (const auto& key : remove_headers) {
    mutation->add_remove_headers(key);
  }
  return response;
}

::envoy::service::ext_proc::v3::ProcessingResponse
MakeRequestBodyMutationResponse(absl::string_view body,
                                bool end_of_stream = false,
                                bool request_drain = false) {
  ::envoy::service::ext_proc::v3::ProcessingResponse response;
  if (request_drain) {
    response.set_request_drain(true);
  }
  auto* body_mutation = response.mutable_request_body()
                            ->mutable_response()
                            ->mutable_body_mutation();
  body_mutation->mutable_streamed_response()->set_body(std::string(body));
  body_mutation->mutable_streamed_response()->set_end_of_stream(end_of_stream);
  return response;
}

::envoy::service::ext_proc::v3::ProcessingResponse
MakeResponseBodyMutationResponse(absl::string_view body,
                                 bool end_of_stream = false,
                                 bool request_drain = false) {
  ::envoy::service::ext_proc::v3::ProcessingResponse response;
  if (request_drain) {
    response.set_request_drain(true);
  }
  auto* body_mutation = response.mutable_response_body()
                            ->mutable_response()
                            ->mutable_body_mutation();
  body_mutation->mutable_streamed_response()->set_body(std::string(body));
  body_mutation->mutable_streamed_response()->set_end_of_stream(end_of_stream);
  return response;
}

::envoy::service::ext_proc::v3::ProcessingResponse
MakeResponseTrailersMutationResponse(
    const std::vector<std::pair<std::string, std::string>>& set_headers,
    const std::vector<std::string>& remove_headers = {},
    bool request_drain = false) {
  ::envoy::service::ext_proc::v3::ProcessingResponse response;
  if (request_drain) {
    response.set_request_drain(true);
  }
  auto* mutation =
      response.mutable_response_trailers()->mutable_header_mutation();
  for (const auto& [key, value] : set_headers) {
    auto* header = mutation->add_set_headers();
    header->mutable_header()->set_key(key);
    header->mutable_header()->set_value(value);
  }
  for (const auto& key : remove_headers) {
    mutation->add_remove_headers(key);
  }
  return response;
}

::envoy::service::ext_proc::v3::ProcessingResponse MakeImmediateResponse(
    grpc::StatusCode code, absl::string_view details = "",
    const std::vector<std::pair<std::string, std::string>>& set_headers = {}) {
  ::envoy::service::ext_proc::v3::ProcessingResponse response;
  auto* immediate = response.mutable_immediate_response();
  immediate->mutable_grpc_status()->set_status(code);
  if (!details.empty()) {
    immediate->set_details(std::string(details));
  }
  auto* mutation = immediate->mutable_headers();
  for (const auto& [key, value] : set_headers) {
    auto* header = mutation->add_set_headers();
    header->mutable_header()->set_key(key);
    header->mutable_header()->set_value(value);
  }
  return response;
}

// A stream-based fake external processor service that provides fine-grained,
// sequential control over incoming ext_proc stream requests and outgoing
// responses/statuses for test assertions.
class FakeExtProcService final
    : public ::envoy::service::ext_proc::v3::ExternalProcessor::Service {
 public:
  // Represents a single bidirectional stream between the client ext_proc filter
  // and this service. It acts as a synchronized communication channel between
  // the test thread and the serving thread.
  class Stream final {
   public:
    // Action commanded by the test thread for the serving thread to perform on
    // an active stream.
    enum class Action : uint8_t {
      // No pending action; stream is idle or the previous action completed.
      kNone,
      // Send a ProcessingResponse message back to the client.
      kSendResponse,
      // Finish the stream with a trailing grpc::Status without writing a
      // response message.
      kSendStatus,
      // Write a ProcessingResponse message and immediately finish the stream
      // with a trailing grpc::Status.
      kSendResponseAndStatus,
      // Terminate the stream read loop and close the stream.
      kClose,
    };

    Stream() = default;

    // Returns the next request received from the client, or std::nullopt
    // if no request received within timeout or stream finished.
    std::optional<::envoy::service::ext_proc::v3::ProcessingRequest>
    GetNextRequest(absl::Duration timeout = absl::Seconds(10)) {
      grpc_core::MutexLock lock(&mu_);
      const absl::Time deadline =
          absl::Now() + timeout * grpc_test_slowdown_factor();
      while (requests_.empty() && !is_closed_) {
        if (cv_.WaitWithDeadline(&mu_, deadline)) {
          return std::nullopt;
        }
      }
      if (requests_.empty()) {
        return std::nullopt;
      }
      auto req = std::move(requests_.front());
      requests_.pop();
      return req;
    }

    // Sends a response on the stream.
    void SendResponse(
        ::envoy::service::ext_proc::v3::ProcessingResponse response) {
      grpc_core::MutexLock lock(&mu_);
      response_to_send_ = std::move(response);
      action_ = Action::kSendResponse;
      cv_.SignalAll();
      while (action_ != Action::kNone && !is_closed_) {
        cv_.Wait(&mu_);
      }
    }

    // Closes the stream with the specified status.
    void SendStatus(absl::Status status) {
      grpc_core::MutexLock lock(&mu_);
      status_to_send_ = status;
      action_ = Action::kSendStatus;
      cv_.SignalAll();
      while (!is_closed_) {
        cv_.Wait(&mu_);
      }
    }

    // Sends a response and immediately closes the stream with status.
    void SendResponseAndStatus(
        ::envoy::service::ext_proc::v3::ProcessingResponse response,
        absl::Status status) {
      grpc_core::MutexLock lock(&mu_);
      response_to_send_ = std::move(response);
      status_to_send_ = status;
      action_ = Action::kSendResponseAndStatus;
      cv_.SignalAll();
      while (!is_closed_) {
        cv_.Wait(&mu_);
      }
    }

    void PushRequest(::envoy::service::ext_proc::v3::ProcessingRequest req) {
      grpc_core::MutexLock lock(&mu_);
      requests_.push(std::move(req));
      cv_.SignalAll();
    }

    void MarkClosed() {
      grpc_core::MutexLock lock(&mu_);
      is_closed_ = true;
      cv_.SignalAll();
    }

    bool is_closed() {
      grpc_core::MutexLock lock(&mu_);
      return is_closed_;
    }

    Action WaitForAction() {
      grpc_core::MutexLock lock(&mu_);
      while (action_ == Action::kNone && !is_closed_) {
        cv_.Wait(&mu_);
      }
      return action_;
    }

    ::envoy::service::ext_proc::v3::ProcessingResponse GetResponseToSend() {
      grpc_core::MutexLock lock(&mu_);
      return response_to_send_;
    }

    absl::Status GetStatusToSend() {
      grpc_core::MutexLock lock(&mu_);
      return status_to_send_.value_or(absl::OkStatus());
    }

    void CompleteAction() {
      grpc_core::MutexLock lock(&mu_);
      action_ = Action::kNone;
      cv_.SignalAll();
    }

   private:
    grpc_core::Mutex mu_;
    grpc_core::CondVar cv_;
    std::queue<::envoy::service::ext_proc::v3::ProcessingRequest> requests_
        ABSL_GUARDED_BY(mu_);
    Action action_ ABSL_GUARDED_BY(mu_) = Action::kNone;
    ::envoy::service::ext_proc::v3::ProcessingResponse response_to_send_
        ABSL_GUARDED_BY(mu_);
    std::optional<absl::Status> status_to_send_ ABSL_GUARDED_BY(mu_);
    bool is_closed_ ABSL_GUARDED_BY(mu_) = false;
  };

  // Returns the next incoming stream, or nullptr if no stream starts within
  // timeout.
  std::shared_ptr<Stream> GetStream(
      absl::Duration timeout = absl::Seconds(10)) {
    grpc_core::MutexLock lock(&mu_);
    const absl::Time deadline =
        absl::Now() + timeout * grpc_test_slowdown_factor();
    while (streams_.empty() && !is_shutdown_) {
      if (cv_.WaitWithDeadline(&mu_, deadline)) {
        return nullptr;
      }
    }
    if (streams_.empty()) {
      return nullptr;
    }
    auto stream = std::move(streams_.front());
    streams_.pop();
    return stream;
  }

  size_t stream_count() {
    grpc_core::MutexLock lock(&mu_);
    return total_stream_count_;
  }

  void Shutdown() {
    grpc_core::MutexLock lock(&mu_);
    is_shutdown_ = true;
    for (auto& s : active_streams_) {
      s->MarkClosed();
    }
    cv_.SignalAll();
  }

  grpc::Status Process(
      grpc::ServerContext* /*context*/,
      grpc::ServerReaderWriter<
          ::envoy::service::ext_proc::v3::ProcessingResponse,
          ::envoy::service::ext_proc::v3::ProcessingRequest>* stream) override {
    auto stream_obj = std::make_shared<Stream>();
    {
      grpc_core::MutexLock lock(&mu_);
      if (is_shutdown_) {
        return grpc::Status(grpc::StatusCode::UNAVAILABLE, "Server shutdown");
      }
      ++total_stream_count_;
      streams_.push(stream_obj);
      active_streams_.push_back(stream_obj);
      cv_.SignalAll();
    }

    ::envoy::service::ext_proc::v3::ProcessingRequest request;
    while (stream->Read(&request)) {
      stream_obj->PushRequest(std::move(request));
      auto action = stream_obj->WaitForAction();
      if (action == Stream::Action::kSendStatus) {
        auto status = stream_obj->GetStatusToSend();
        stream_obj->MarkClosed();
        return grpc::Status(static_cast<grpc::StatusCode>(status.code()),
                            std::string(status.message()));
      }
      if (action == Stream::Action::kSendResponseAndStatus) {
        stream->Write(stream_obj->GetResponseToSend());
        auto status = stream_obj->GetStatusToSend();
        stream_obj->MarkClosed();
        return grpc::Status(static_cast<grpc::StatusCode>(status.code()),
                            std::string(status.message()));
      }
      if (action == Stream::Action::kSendResponse) {
        if (!stream->Write(stream_obj->GetResponseToSend())) {
          break;
        }
        stream_obj->CompleteAction();
      } else if (action == Stream::Action::kClose || stream_obj->is_closed()) {
        break;
      }
    }
    stream_obj->MarkClosed();
    return grpc::Status::OK;
  }

 private:
  grpc_core::Mutex mu_;
  grpc_core::CondVar cv_;
  // FIFO queue of newly arrived streams waiting to be consumed by the test
  // thread via GetStream(). Once popped by GetStream(), the stream is no longer
  // in this queue.
  std::queue<std::shared_ptr<Stream>> streams_ ABSL_GUARDED_BY(mu_);
  // Lifetime registry of all streams created during the test. Used during
  // Shutdown() to mark all active and previously consumed streams as closed,
  // preventing serving threads from hanging on blocked reads or actions.
  std::vector<std::shared_ptr<Stream>> active_streams_ ABSL_GUARDED_BY(mu_);
  size_t total_stream_count_ ABSL_GUARDED_BY(mu_) = 0;
  bool is_shutdown_ ABSL_GUARDED_BY(mu_) = false;
};

class CustomBidiStreamServiceImpl : public TestServiceImpl {
 public:
  Status BidiStream(
      ServerContext* /*context*/,
      ServerReaderWriter<EchoResponse, EchoRequest>* stream) override {
    EchoRequest request1;
    if (!stream->Read(&request1)) return Status::OK;
    stream->SendInitialMetadata();
    EchoRequest request2;
    if (!stream->Read(&request2)) return Status::OK;
    EchoResponse response;
    response.set_message(request1.message());
    stream->Write(response);
    return Status::OK;
  }
};

//
// Test fixture
//

class XdsExtProcEnd2endTest : public XdsEnd2endTest {
 public:
  class ExtProcFilterConfigBuilder {
   public:
    ExtProcFilterConfigBuilder() {
      auto* processing_mode = ext_proc_.mutable_processing_mode();
      processing_mode->set_request_header_mode(
          envoy::extensions::filters::http::ext_proc::v3::ProcessingMode::SKIP);
      processing_mode->set_response_header_mode(
          envoy::extensions::filters::http::ext_proc::v3::ProcessingMode::SKIP);
      processing_mode->set_response_trailer_mode(
          envoy::extensions::filters::http::ext_proc::v3::ProcessingMode::SKIP);
      auto* timeout = ext_proc_.mutable_grpc_service()->mutable_timeout();
      timeout->set_seconds(1);  // 1s
      timeout->set_nanos(0);
    }

    ExtProcFilterConfigBuilder& SetTargetUri(const std::string& target_uri) {
      auto* google_grpc =
          ext_proc_.mutable_grpc_service()->mutable_google_grpc();
      google_grpc->set_target_uri(target_uri);
      return *this;
    }

    ExtProcFilterConfigBuilder& SetInsecureChannelCredentials() {
      auto* google_grpc =
          ext_proc_.mutable_grpc_service()->mutable_google_grpc();
      google_grpc->clear_channel_credentials_plugin();
      google_grpc->add_channel_credentials_plugin()->PackFrom(
          envoy::extensions::grpc_service::channel_credentials::insecure::v3::
              InsecureCredentials());
      return *this;
    }

    ExtProcFilterConfigBuilder& SetGoogleDefaultChannelCredentials() {
      auto* google_grpc =
          ext_proc_.mutable_grpc_service()->mutable_google_grpc();
      google_grpc->clear_channel_credentials_plugin();
      google_grpc->add_channel_credentials_plugin()->PackFrom(
          envoy::extensions::grpc_service::channel_credentials::google_default::
              v3::GoogleDefaultCredentials());
      return *this;
    }

    ExtProcFilterConfigBuilder& SetAccessTokenCallCredentials(
        const std::string& token) {
      auto* google_grpc =
          ext_proc_.mutable_grpc_service()->mutable_google_grpc();
      google_grpc->clear_call_credentials_plugin();
      envoy::extensions::grpc_service::call_credentials::access_token::v3::
          AccessTokenCredentials call_creds;
      call_creds.set_token(token);
      google_grpc->add_call_credentials_plugin()->PackFrom(call_creds);
      return *this;
    }

    ExtProcFilterConfigBuilder& SetFailureModeAllow(bool allow) {
      ext_proc_.set_failure_mode_allow(allow);
      return *this;
    }

    ExtProcFilterConfigBuilder& SetRequestHeaderMode() {
      ext_proc_.mutable_processing_mode()->set_request_header_mode(
          envoy::extensions::filters::http::ext_proc::v3::ProcessingMode::SEND);
      return *this;
    }

    ExtProcFilterConfigBuilder& SetResponseHeaderMode() {
      ext_proc_.mutable_processing_mode()->set_response_header_mode(
          envoy::extensions::filters::http::ext_proc::v3::ProcessingMode::SEND);
      return *this;
    }

    ExtProcFilterConfigBuilder& SetRequestBodyMode() {
      ext_proc_.mutable_processing_mode()->set_request_body_mode(
          envoy::extensions::filters::http::ext_proc::v3::ProcessingMode::GRPC);
      return *this;
    }

    ExtProcFilterConfigBuilder& SetResponseBodyMode() {
      ext_proc_.mutable_processing_mode()->set_response_body_mode(
          envoy::extensions::filters::http::ext_proc::v3::ProcessingMode::GRPC);
      return *this;
    }

    ExtProcFilterConfigBuilder& SetResponseTrailerMode() {
      ext_proc_.mutable_processing_mode()->set_response_trailer_mode(
          envoy::extensions::filters::http::ext_proc::v3::ProcessingMode::SEND);
      return *this;
    }

    ExtProcFilterConfigBuilder& AddRequestAttribute(
        const std::string& attribute) {
      ext_proc_.add_request_attributes(attribute);
      return *this;
    }

    ExtProcFilterConfigBuilder& AddResponseAttribute(
        const std::string& attribute) {
      ext_proc_.add_response_attributes(attribute);
      return *this;
    }

    ExtProcFilterConfigBuilder& SetMutationRules(
        const envoy::config::common::mutation_rules::v3::HeaderMutationRules&
            rules) {
      *ext_proc_.mutable_mutation_rules() = rules;
      return *this;
    }

    ExtProcFilterConfigBuilder& SetForwardingRules(
        const envoy::extensions::filters::http::ext_proc::v3::
            HeaderForwardingRules& rules) {
      *ext_proc_.mutable_forward_rules() = rules;
      return *this;
    }

    ExtProcFilterConfigBuilder& SetDisableImmediateResponse(bool disable) {
      ext_proc_.set_disable_immediate_response(disable);
      return *this;
    }

    ExtProcFilterConfigBuilder& SetObservabilityMode(bool observability_mode) {
      ext_proc_.set_observability_mode(observability_mode);
      return *this;
    }

    ExtProcFilterConfigBuilder& SetDeferredCloseTimeout(
        grpc_core::Duration timeout) {
      SetProtoDuration(timeout, ext_proc_.mutable_deferred_close_timeout());
      return *this;
    }

    envoy::extensions::filters::http::ext_proc::v3::ExternalProcessor Build() {
      return ext_proc_;
    }

   private:
    envoy::extensions::filters::http::ext_proc::v3::ExternalProcessor ext_proc_;
  };

  // A class for running a bidirectional streaming RPC asynchronously using the
  // callback API.
  class AsyncBidiStream
      : public grpc::ClientBidiReactor<EchoRequest, EchoResponse> {
   public:
    // Represents the lifecycle state of an individual asynchronous stream
    // operation (write or read).
    //
    // State transitions:
    //   kIdle -> kInFlight: Initiated when StartWrite() or StartReadMessage()
    //   is called.
    //                       Precondition: stream must not be done (if done,
    //                       transitions directly to kFailed).
    //   kInFlight -> kSuccess: Transitioned in OnWriteDone(true) or
    //   OnReadDone(true) callback.
    //   kInFlight -> kFailed: Transitioned in OnWriteDone(false) or
    //   OnReadDone(false) callback.
    //   kSuccess/kFailed -> kInFlight: When the next operation (StartWrite /
    //   StartReadMessage) begins.
    enum class OpState : uint8_t {
      // No active operation in progress.
      kIdle,
      // Operation was started and waiting for the completion callback
      // (OnWriteDone / OnReadDone).
      kInFlight,
      // Operation completed successfully (callback received ok == true).
      kSuccess,
      // Operation failed (callback received ok == false, or stream already
      // done).
      kFailed,
    };

    // Represents the receipt state of initial metadata for the RPC.
    //
    // State transitions:
    //   kPending -> kSuccess: Transitioned in OnReadInitialMetadataDone(true)
    //   callback.
    //   kPending -> kFailed: Transitioned in OnReadInitialMetadataDone(false)
    //   callback.
    enum class MetadataState : uint8_t {
      // Initial metadata has not yet been received from the server.
      kPending,
      // Initial metadata received successfully (ok == true).
      kSuccess,
      // Initial metadata failed / was not received cleanly (ok == false).
      kFailed,
    };

    AsyncBidiStream() = default;

    ~AsyncBidiStream() override {
      grpc_core::MutexLock lock(&mu_);
      const absl::Time deadline =
          absl::Now() + absl::Seconds(10) * grpc_test_slowdown_factor();
      while (!status_.has_value() && (write_state_ == OpState::kInFlight ||
                                      read_state_ == OpState::kInFlight)) {
        if (cv_.WaitWithDeadline(&mu_, deadline)) {
          break;
        }
      }
    }

    void Start(grpc::testing::EchoTestService::Stub* stub,
               const RpcOptions& rpc_options = RpcOptions()) {
      EchoRequest request;
      rpc_options.SetupRpc(&context_, &request);
      stub->async()->BidiStream(&context_, this);
      StartCall();
    }

    void StartWrite(const EchoRequest& request) {
      grpc_core::MutexLock lock(&mu_);
      write_msg_ = request;
      if (status_.has_value()) {
        write_state_ = OpState::kFailed;
        cv_.SignalAll();
        return;
      }
      write_state_ = OpState::kInFlight;
      ClientBidiReactor::StartWrite(&write_msg_);
    }

    bool WaitForWriteDone(absl::Duration timeout = absl::Seconds(10)) {
      grpc_core::MutexLock lock(&mu_);
      const absl::Time deadline =
          absl::Now() + timeout * grpc_test_slowdown_factor();
      while (write_state_ != OpState::kSuccess &&
             write_state_ != OpState::kFailed && !status_.has_value()) {
        if (cv_.WaitWithDeadline(&mu_, deadline)) {
          return false;
        }
      }
      return write_state_ == OpState::kSuccess;
    }

    void StartWritesDone() {
      grpc_core::MutexLock lock(&mu_);
      if (status_.has_value()) return;
      ClientBidiReactor::StartWritesDone();
    }

    void StartReadMessage() {
      grpc_core::MutexLock lock(&mu_);
      read_msg_.Clear();
      if (status_.has_value()) {
        read_state_ = OpState::kFailed;
        cv_.SignalAll();
        return;
      }
      read_state_ = OpState::kInFlight;
      StartRead(&read_msg_);
    }

    bool WaitForReadDone(EchoResponse* response,
                         absl::Duration timeout = absl::Seconds(10)) {
      grpc_core::MutexLock lock(&mu_);
      const absl::Time deadline =
          absl::Now() + timeout * grpc_test_slowdown_factor();
      while (read_state_ != OpState::kSuccess &&
             read_state_ != OpState::kFailed && !status_.has_value()) {
        if (cv_.WaitWithDeadline(&mu_, deadline)) {
          return false;
        }
      }
      if (read_state_ == OpState::kSuccess) {
        *response = read_msg_;
        return true;
      }
      return false;
    }

    bool ReadMessage(EchoResponse* response,
                     absl::Duration timeout = absl::Seconds(10)) {
      StartReadMessage();
      return WaitForReadDone(response, timeout);
    }

    Status Finish(absl::Duration timeout = absl::Seconds(10)) {
      grpc_core::MutexLock lock(&mu_);
      const absl::Time deadline =
          absl::Now() + timeout * grpc_test_slowdown_factor();
      while (!status_.has_value()) {
        if (cv_.WaitWithDeadline(&mu_, deadline)) {
          return Status(StatusCode::DEADLINE_EXCEEDED, "Finish timeout");
        }
      }
      return *status_;
    }

    bool WaitForInitialMetadata(absl::Duration timeout = absl::Seconds(10)) {
      grpc_core::MutexLock lock(&mu_);
      const absl::Time deadline =
          absl::Now() + timeout * grpc_test_slowdown_factor();
      while (initial_metadata_state_ == MetadataState::kPending &&
             !status_.has_value()) {
        if (cv_.WaitWithDeadline(&mu_, deadline)) {
          return false;
        }
      }
      return initial_metadata_state_ == MetadataState::kSuccess;
    }

    std::multimap<std::string, std::string> GetServerInitialMetadata() {
      std::multimap<std::string, std::string> output;
      for (const auto& [key, value] : context_.GetServerInitialMetadata()) {
        std::string header(key.data(), key.size());
        absl::AsciiStrToLower(&header);
        output.emplace(header, std::string(value.data(), value.size()));
      }
      return output;
    }

    std::multimap<std::string, std::string> GetServerTrailingMetadata() {
      std::multimap<std::string, std::string> output;
      for (const auto& [key, value] : context_.GetServerTrailingMetadata()) {
        std::string header(key.data(), key.size());
        absl::AsciiStrToLower(&header);
        output.emplace(header, std::string(value.data(), value.size()));
      }
      return output;
    }

    void OnReadInitialMetadataDone(bool ok) override {
      grpc_core::MutexLock lock(&mu_);
      initial_metadata_state_ =
          ok ? MetadataState::kSuccess : MetadataState::kFailed;
      cv_.SignalAll();
    }

    void OnWriteDone(bool ok) override {
      grpc_core::MutexLock lock(&mu_);
      write_state_ = ok ? OpState::kSuccess : OpState::kFailed;
      cv_.SignalAll();
    }

    void OnReadDone(bool ok) override {
      grpc_core::MutexLock lock(&mu_);
      read_state_ = ok ? OpState::kSuccess : OpState::kFailed;
      cv_.SignalAll();
    }

    void OnDone(const Status& s) override {
      grpc_core::MutexLock lock(&mu_);
      status_ = s;
      if (write_state_ == OpState::kInFlight) {
        write_state_ = OpState::kFailed;
      }
      if (read_state_ == OpState::kInFlight) {
        read_state_ = OpState::kFailed;
      }
      cv_.SignalAll();
    }

   private:
    ClientContext context_;
    EchoRequest write_msg_;
    EchoResponse read_msg_;
    grpc_core::Mutex mu_;
    grpc_core::CondVar cv_;
    MetadataState initial_metadata_state_ ABSL_GUARDED_BY(mu_) =
        MetadataState::kPending;
    OpState write_state_ ABSL_GUARDED_BY(mu_) = OpState::kIdle;
    OpState read_state_ ABSL_GUARDED_BY(mu_) = OpState::kIdle;
    std::optional<Status> status_ ABSL_GUARDED_BY(mu_);
  };

  class ExtProcServerThread : public ServerThread {
   public:
    ExtProcServerThread(XdsEnd2endTest* test_obj,
                        std::shared_ptr<FakeExtProcService> service)
        : ServerThread(test_obj, /*use_xds_enabled_server=*/false,
                       grpc::InsecureServerCredentials()),
          service_(std::move(service)) {}

    FakeExtProcService* ext_proc_service() { return service_.get(); }

   private:
    const char* Type() override { return "ExtProc"; }

    void RegisterAllServices(ServerBuilder* builder) override {
      builder->RegisterService(service_.get());
    }

    void StartAllServices() override {}
    void ShutdownAllServices() override { service_->Shutdown(); }

    std::shared_ptr<FakeExtProcService> service_;
  };

  class CustomBackendServerThread : public ServerThread {
   public:
    CustomBackendServerThread(
        XdsExtProcEnd2endTest* test_obj,
        std::shared_ptr<CustomBidiStreamServiceImpl> service)
        : ServerThread(test_obj,
                       /*use_xds_enabled_server=*/
                       test_obj->GetParam().filter_on_server(),
                       /*credentials=*/nullptr),
          service_(std::move(service)) {}

   private:
    const char* Type() override { return "CustomBackend"; }

    void RegisterAllServices(ServerBuilder* builder) override {
      builder->RegisterService(service_.get());
    }

    void StartAllServices() override {}
    void ShutdownAllServices() override { StopListeningAndSendGoaways(); }

    std::shared_ptr<CustomBidiStreamServiceImpl> service_;
  };

  void ResetStubWithUniqueArg() {
    ChannelArguments args;
    static std::atomic<int> g_counter{0};
    args.SetInt(
        "g_unique_test_channel_arg_" +
            std::to_string(g_counter.fetch_add(1, std::memory_order_relaxed)),
        1);
    ResetStub(0, &args);
  }

  void SetUp() override {
    if (GetParam().filter_on_server() &&
        !grpc_core::IsXdsServerFilterChainPerRouteEnabled()) {
      GTEST_SKIP()
          << "test requires xds_server_filter_chain_per_route experiment";
    }
    env_var_.emplace(GetParam().filter_on_server()
                         ? "GRPC_EXPERIMENTAL_XDS_EXT_PROC_ON_SERVER"
                         : "GRPC_EXPERIMENTAL_XDS_EXT_PROC_ON_CLIENT");
    InitClient(MakeBootstrapBuilder().SetTrustedXdsServer(),
               /*lb_expected_authority=*/"",
               /*xds_resource_does_not_exist_timeout_ms=*/0,
               /*balancer_authority_override=*/"", /*args=*/nullptr);
    ext_proc_service_ = std::make_shared<FakeExtProcService>();
    ext_proc_server_ =
        std::make_unique<ExtProcServerThread>(this, ext_proc_service_);
    ext_proc_server_->Start();
  }

  void TearDown() override {
    ext_proc_server_->Shutdown();
    XdsEnd2endTest::TearDown();
  }

  void CreateAndStartBackends(
      size_t num_backends = 1,
      std::shared_ptr<ServerCredentials> credentials = nullptr) {
    XdsEnd2endTest::CreateAndStartBackends(
        num_backends, /*xds_enabled=*/GetParam().filter_on_server(),
        std::move(credentials));
    if (GetParam().filter_on_server()) {
      for (size_t i = 0; i < num_backends; ++i) {
        EXPECT_THAT(backends_[i]->GetNextStatus(),
                    ::testing::Optional(absl::OkStatus()));
      }
    }
  }

  Listener BuildListenerWithExtProcFilter(
      const ExternalProcessor& ext_proc) const {
    Listener listener;
    std::unique_ptr<HcmAccessor> hcm_accessor;
    if (GetParam().filter_on_server()) {
      listener = default_server_listener_;
      hcm_accessor = std::make_unique<ServerHcmAccessor>();
    } else {
      listener = default_listener_;
      hcm_accessor = std::make_unique<ClientHcmAccessor>();
    }
    HttpConnectionManager hcm = hcm_accessor->Unpack(listener);
    HttpFilter* filter0 = hcm.mutable_http_filters(0);
    *hcm.add_http_filters() = *filter0;
    filter0->set_name(kFilterInstanceName);
    filter0->mutable_typed_config()->PackFrom(ext_proc);
    hcm_accessor->Pack(hcm, &listener);
    return listener;
  }

  void SetListenerAndRouteConfiguration(
      BalancerServerThread* balancer, const Listener& listener,
      RouteConfiguration route_config = RouteConfiguration(),
      int backend_port = 0) {
    if (GetParam().filter_on_server()) {
      RouteConfiguration server_route_config = default_server_route_config_;
      if (!route_config.virtual_hosts().empty() &&
          !route_config.virtual_hosts(0).routes().empty()) {
        const auto& per_filter_config =
            route_config.virtual_hosts(0).routes(0).typed_per_filter_config();
        *server_route_config.mutable_virtual_hosts(0)
             ->mutable_routes(0)
             ->mutable_typed_per_filter_config() = per_filter_config;
      }
      if (backend_port == 0 && !backends_.empty()) {
        backend_port = backends_[0]->port();
      }
      if (backend_port != 0) {
        SetServerListenerNameAndRouteConfiguration(
            balancer, listener, backend_port, server_route_config);
      }
    } else {
      if (route_config.virtual_hosts().empty()) {
        route_config = default_route_config_;
      }
      XdsEnd2endTest::SetListenerAndRouteConfiguration(balancer, listener,
                                                       route_config);
    }
  }

  std::optional<grpc_core::testing::ScopedExperimentalEnvVar> env_var_;
  std::shared_ptr<FakeExtProcService> ext_proc_service_;
  std::unique_ptr<ExtProcServerThread> ext_proc_server_;
};

INSTANTIATE_TEST_SUITE_P(
    XdsTest, XdsExtProcEnd2endTest,
    ::testing::Values(
        XdsTestType(),
        XdsTestType().set_filter_config_setup(
            XdsTestType::kHttpFilterConfigInRoute),
        XdsTestType().set_filter_on_server(),
        XdsTestType().set_filter_on_server().set_filter_config_setup(
            XdsTestType::kHttpFilterConfigInRoute)),
    &XdsTestType::Name);

//
// Tests
//

TEST_P(XdsExtProcEnd2endTest, ProcessingModeAllDisabledSuccess) {
  CreateAndStartBackends(1);
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetObservabilityMode(false)
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  RpcOptions rpc_options;
  rpc_options.set_echo_metadata_initially(true);
  rpc_options.set_echo_metadata(true);
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get(), rpc_options);
  Status status = rpc.GetStatus();
  EXPECT_TRUE(status.ok()) << "RPC failed: " << status.error_message();
  auto server_initial_metadata = rpc.GetServerInitialMetadata();
  auto server_trailing_metadata = rpc.GetServerTrailingMetadata();
  EXPECT_EQ(ext_proc_service_->stream_count(), 0);
  EXPECT_EQ(ext_proc_service_->GetStream(absl::ZeroDuration()), nullptr);
  auto it = server_initial_metadata.find(kRequestHeadersMutatedHeaderKey);
  EXPECT_EQ(it, server_initial_metadata.end());
  it = server_initial_metadata.find(kResponseHeadersMutatedHeaderKey);
  EXPECT_EQ(it, server_initial_metadata.end());
  it = server_trailing_metadata.find(kResponseTrailersMutatedHeaderKey);
  EXPECT_EQ(it, server_trailing_metadata.end());
  EXPECT_EQ(rpc.response().message(), kRequestMessage);
}

TEST_P(XdsExtProcEnd2endTest, ProcessingModeAllEnabledSuccess) {
  CreateAndStartBackends(1);
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetObservabilityMode(false)
                             .SetRequestHeaderMode()
                             .SetResponseHeaderMode()
                             .SetResponseTrailerMode()
                             .SetRequestBodyMode()
                             .SetResponseBodyMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  RpcOptions rpc_options;
  rpc_options.set_echo_metadata_initially(true);
  rpc_options.set_echo_metadata(true);
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  bool saw_request_headers = false;
  bool saw_response_headers = false;
  bool saw_response_trailers = false;
  bool saw_response_body = false;
  int request_body_count = 0;
  while (!saw_response_trailers) {
    auto req = ext_proc_stream->GetNextRequest();
    ASSERT_TRUE(req.has_value());
    if (req->has_request_headers()) {
      saw_request_headers = true;
      ext_proc_stream->SendResponse(MakeRequestHeadersMutationResponse(
          {{kRequestHeadersMutatedHeaderKey, kHeaderMutatedValue}}));
    } else if (req->has_request_body()) {
      ++request_body_count;
      EchoRequest echo_req;
      if (echo_req.ParseFromString(req->request_body().body())) {
        echo_req.set_message(
            absl::StrCat(echo_req.message(), kRequestBodyMutatedSuffix));
        std::string mutated;
        ASSERT_TRUE(echo_req.SerializeToString(&mutated));
        ext_proc_stream->SendResponse(MakeRequestBodyMutationResponse(
            mutated, req->request_body().end_of_stream()));
      } else {
        ext_proc_stream->SendResponse(MakeRequestBodyMutationResponse(
            req->request_body().body(), req->request_body().end_of_stream()));
      }
    } else if (req->has_response_headers()) {
      saw_response_headers = true;
      ext_proc_stream->SendResponse(MakeResponseHeadersMutationResponse(
          {{kResponseHeadersMutatedHeaderKey, kHeaderMutatedValue}}));
    } else if (req->has_response_body()) {
      saw_response_body = true;
      EchoResponse echo_resp;
      if (echo_resp.ParseFromString(req->response_body().body())) {
        echo_resp.set_message(
            absl::StrCat(echo_resp.message(), kResponseBodyMutatedSuffix));
        std::string mutated;
        ASSERT_TRUE(echo_resp.SerializeToString(&mutated));
        ext_proc_stream->SendResponse(MakeResponseBodyMutationResponse(
            mutated, req->response_body().end_of_stream()));
      } else {
        ext_proc_stream->SendResponse(MakeResponseBodyMutationResponse(
            req->response_body().body(), req->response_body().end_of_stream()));
      }
    } else if (req->has_response_trailers()) {
      saw_response_trailers = true;
      ext_proc_stream->SendResponse(MakeResponseTrailersMutationResponse(
          {{kResponseTrailersMutatedHeaderKey, kHeaderMutatedValue}}));
    } else {
      FAIL() << "Unexpected request type: " << req->DebugString();
    }
  }
  EXPECT_TRUE(saw_request_headers);
  EXPECT_GT(request_body_count, 0);
  EXPECT_TRUE(saw_response_headers);
  EXPECT_TRUE(saw_response_body);
  EXPECT_TRUE(saw_response_trailers);
  Status status = rpc.GetStatus();
  EXPECT_TRUE(status.ok()) << "RPC failed: " << status.error_message();
  auto server_initial_metadata = rpc.GetServerInitialMetadata();
  auto server_trailing_metadata = rpc.GetServerTrailingMetadata();
  auto it = server_initial_metadata.find(kRequestHeadersMutatedHeaderKey);
  ASSERT_NE(it, server_initial_metadata.end());
  EXPECT_EQ(it->second, kHeaderMutatedValue);
  it = server_initial_metadata.find(kResponseHeadersMutatedHeaderKey);
  ASSERT_NE(it, server_initial_metadata.end());
  EXPECT_EQ(it->second, kHeaderMutatedValue);
  it = server_trailing_metadata.find(kResponseTrailersMutatedHeaderKey);
  ASSERT_NE(it, server_trailing_metadata.end());
  EXPECT_EQ(it->second, kHeaderMutatedValue);
  std::string expected_message = kRequestMessage;
  absl::StrAppend(&expected_message, kRequestBodyMutatedSuffix);
  absl::StrAppend(&expected_message, kResponseBodyMutatedSuffix);
  EXPECT_EQ(rpc.response().message(), expected_message);
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

TEST_P(XdsExtProcEnd2endTest,
       ProcessingModeAllEnabledWithObservabilityModeSuccess) {
  CreateAndStartBackends(1);
  ResetStubWithUniqueArg();
  auto ext_proc_config =
      ExtProcFilterConfigBuilder()
          .SetTargetUri(ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetObservabilityMode(true)
          .SetDeferredCloseTimeout(grpc_core::Duration::Seconds(1))
          .SetRequestHeaderMode()
          .SetResponseHeaderMode()
          .SetResponseTrailerMode()
          .SetRequestBodyMode()
          .SetResponseBodyMode()
          .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  AsyncBidiStream stream;
  RpcOptions rpc_options;
  rpc_options.set_echo_metadata_initially(true);
  rpc_options.set_echo_metadata(true);
  stream.Start(stub_.get(), rpc_options);
  EchoRequest request;
  request.set_message(kRequestMessage);
  stream.StartWrite(request);
  EXPECT_TRUE(stream.WaitForWriteDone());
  stream.StartWritesDone();
  stream.StartReadMessage();
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  bool saw_request_headers = false;
  bool saw_response_headers = false;
  bool saw_response_trailers = false;
  bool saw_response_body = false;
  int request_body_count = 0;
  while (!saw_response_trailers) {
    auto req = ext_proc_stream->GetNextRequest();
    ASSERT_TRUE(req.has_value());
    if (req->has_request_headers()) {
      saw_request_headers = true;
      ext_proc_stream->SendResponse(MakeRequestHeadersMutationResponse(
          {{kRequestHeadersMutatedHeaderKey, kHeaderMutatedValue}}));
    } else if (req->has_request_body()) {
      ++request_body_count;
      EchoRequest echo_req;
      if (echo_req.ParseFromString(req->request_body().body())) {
        echo_req.set_message(
            absl::StrCat(echo_req.message(), kRequestBodyMutatedSuffix));
        std::string mutated;
        ASSERT_TRUE(echo_req.SerializeToString(&mutated));
        ext_proc_stream->SendResponse(MakeRequestBodyMutationResponse(
            mutated, req->request_body().end_of_stream()));
      } else {
        ext_proc_stream->SendResponse(MakeRequestBodyMutationResponse(
            req->request_body().body(), req->request_body().end_of_stream()));
      }
    } else if (req->has_response_headers()) {
      saw_response_headers = true;
      ext_proc_stream->SendResponse(MakeResponseHeadersMutationResponse(
          {{kResponseHeadersMutatedHeaderKey, kHeaderMutatedValue}}));
    } else if (req->has_response_body()) {
      saw_response_body = true;
      EchoResponse echo_resp;
      if (echo_resp.ParseFromString(req->response_body().body())) {
        echo_resp.set_message(
            absl::StrCat(echo_resp.message(), kResponseBodyMutatedSuffix));
        std::string mutated;
        ASSERT_TRUE(echo_resp.SerializeToString(&mutated));
        ext_proc_stream->SendResponse(MakeResponseBodyMutationResponse(
            mutated, req->response_body().end_of_stream()));
      } else {
        ext_proc_stream->SendResponse(MakeResponseBodyMutationResponse(
            req->response_body().body(), req->response_body().end_of_stream()));
      }
    } else if (req->has_response_trailers()) {
      saw_response_trailers = true;
      ext_proc_stream->SendResponse(MakeResponseTrailersMutationResponse(
          {{kResponseTrailersMutatedHeaderKey, kHeaderMutatedValue}}));
    } else {
      FAIL() << "Unexpected request type: " << req->DebugString();
    }
  }
  EXPECT_TRUE(saw_request_headers);
  EXPECT_GT(request_body_count, 0);
  EXPECT_TRUE(saw_response_headers);
  EXPECT_TRUE(saw_response_body);
  EXPECT_TRUE(saw_response_trailers);
  EchoResponse response;
  EXPECT_TRUE(stream.WaitForReadDone(&response));
  Status status = stream.Finish();
  EXPECT_TRUE(status.ok()) << "RPC failed: " << status.error_message();
  auto server_initial_metadata = stream.GetServerInitialMetadata();
  auto server_trailing_metadata = stream.GetServerTrailingMetadata();
  // In observability mode, mutations should NOT be applied.
  auto it = server_initial_metadata.find(kRequestHeadersMutatedHeaderKey);
  EXPECT_EQ(it, server_initial_metadata.end());
  it = server_initial_metadata.find(kResponseHeadersMutatedHeaderKey);
  EXPECT_EQ(it, server_initial_metadata.end());
  it = server_trailing_metadata.find(kResponseTrailersMutatedHeaderKey);
  EXPECT_EQ(it, server_trailing_metadata.end());
  EXPECT_EQ(response.message(), kRequestMessage);
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

TEST_P(XdsExtProcEnd2endTest, TrailersOnlyProcessingModeAllEnabled) {
  CreateAndStartBackends(1);
  ResetStubWithUniqueArg();
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetObservabilityMode(false)
                             .SetRequestHeaderMode()
                             .SetResponseHeaderMode()
                             .SetResponseTrailerMode()
                             .SetRequestBodyMode()
                             .SetResponseBodyMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  RpcOptions rpc_options;
  rpc_options.set_echo_metadata_initially(true);
  rpc_options.set_echo_metadata(true);
  rpc_options.set_server_fail(true);
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  bool saw_request_headers = false;
  int request_body_count = 0;
  while (true) {
    auto req = ext_proc_stream->GetNextRequest(absl::Milliseconds(500));
    if (!req.has_value()) break;
    if (req->has_request_headers()) {
      saw_request_headers = true;
      ext_proc_stream->SendResponse(MakeRequestHeadersMutationResponse(
          {{kRequestHeadersMutatedHeaderKey, kHeaderMutatedValue}}));
    } else if (req->has_request_body()) {
      ++request_body_count;
      ext_proc_stream->SendResponse(MakeRequestBodyMutationResponse(
          req->request_body().body(), req->request_body().end_of_stream()));
    } else if (req->has_response_headers()) {
      ext_proc_stream->SendResponse(MakeResponseHeadersMutationResponse(
          {{kResponseHeadersMutatedHeaderKey, kHeaderMutatedValue}}));
    } else {
      FAIL() << "Unexpected request type: " << req->DebugString();
    }
  }
  EXPECT_TRUE(saw_request_headers);
  EXPECT_GT(request_body_count, 0);
  Status status = rpc.GetStatus();
  EXPECT_EQ(status.error_code(), StatusCode::FAILED_PRECONDITION)
      << "Actual error message: " << status.error_message();
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

TEST_P(XdsExtProcEnd2endTest,
       TrailersOnlyProcessingModeAllEnabledWithObservabilityMode) {
  CreateAndStartBackends(1);
  ResetStubWithUniqueArg();
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetObservabilityMode(true)
                             .SetRequestHeaderMode()
                             .SetResponseHeaderMode()
                             .SetResponseTrailerMode()
                             .SetRequestBodyMode()
                             .SetResponseBodyMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  RpcOptions rpc_options;
  rpc_options.set_echo_metadata_initially(true);
  rpc_options.set_echo_metadata(true);
  rpc_options.set_server_fail(true);
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  bool saw_request_headers = false;
  int request_body_count = 0;
  while (true) {
    auto req = ext_proc_stream->GetNextRequest(absl::Milliseconds(500));
    if (!req.has_value()) break;
    if (req->has_request_headers()) {
      saw_request_headers = true;
      ext_proc_stream->SendResponse(MakeRequestHeadersMutationResponse(
          {{kRequestHeadersMutatedHeaderKey, kHeaderMutatedValue}}));
    } else if (req->has_request_body()) {
      ++request_body_count;
      ext_proc_stream->SendResponse(MakeRequestBodyMutationResponse(
          req->request_body().body(), req->request_body().end_of_stream()));
    } else if (req->has_response_headers()) {
      ext_proc_stream->SendResponse(MakeResponseHeadersMutationResponse(
          {{kResponseHeadersMutatedHeaderKey, kHeaderMutatedValue}}));
    } else {
      FAIL() << "Unexpected request type: " << req->DebugString();
    }
  }
  EXPECT_TRUE(saw_request_headers);
  EXPECT_GT(request_body_count, 0);
  Status status = rpc.GetStatus();
  EXPECT_EQ(status.error_code(), StatusCode::FAILED_PRECONDITION)
      << "Actual error message: " << status.error_message();
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

TEST_P(XdsExtProcEnd2endTest, RequestHeadersContinueAndReplaceFails) {
  CreateAndStartBackends(1);
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetRequestHeaderMode()
                             .SetResponseHeaderMode()
                             .SetResponseTrailerMode()
                             .SetRequestBodyMode()
                             .SetResponseBodyMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  RpcOptions rpc_options;
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(req.has_value());
  EXPECT_TRUE(req->has_request_headers());
  ::envoy::service::ext_proc::v3::ProcessingResponse response;
  response.mutable_request_headers()->mutable_response()->set_status(
      ::envoy::service::ext_proc::v3::CommonResponse::CONTINUE_AND_REPLACE);
  ext_proc_stream->SendResponse(response);
  Status status = rpc.GetStatus();
  EXPECT_THAT(status,
              GrpcStatusIs(StatusCode::INTERNAL,
                           ::testing::HasSubstr(
                               "CONTINUE_AND_REPLACE is not supported")));
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

TEST_P(XdsExtProcEnd2endTest,
       RequestHeadersExtProcConnectionErrorFailureModeFalse) {
  int port = grpc_pick_unused_port_or_die();
  std::string target = absl::StrCat("localhost:", port);
  CreateAndStartBackends(1);
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(target)
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(false)
                             .SetRequestHeaderMode()
                             .SetResponseHeaderMode()
                             .SetResponseTrailerMode()
                             .SetRequestBodyMode()
                             .SetResponseBodyMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::UNAVAILABLE,
                      MakeConnectionFailureRegex(
                          "failed to connect to all addresses; last error: ",
                          /*resolution_note=*/""));
}

TEST_P(XdsExtProcEnd2endTest,
       RequestHeadersExtProcConnectionErrorFailureModeTrue) {
  int port = grpc_pick_unused_port_or_die();
  std::string target = absl::StrCat("localhost:", port);
  CreateAndStartBackends(1);
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(target)
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(true)
                             .SetRequestHeaderMode()
                             .SetResponseHeaderMode()
                             .SetResponseTrailerMode()
                             .SetRequestBodyMode()
                             .SetResponseBodyMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  CheckRpcSendOk(DEBUG_LOCATION);
}

TEST_P(XdsExtProcEnd2endTest, RequestHeadersInvalidHeaderMutationFails) {
  CreateAndStartBackends(1);
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetRequestHeaderMode()
                             .SetResponseHeaderMode()
                             .SetResponseTrailerMode()
                             .SetRequestBodyMode()
                             .SetResponseBodyMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  RpcOptions rpc_options;
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(req.has_value());
  EXPECT_TRUE(req->has_request_headers());
  ext_proc_stream->SendResponse(
      MakeRequestHeadersMutationResponse({{"host", "invalid-host"}}));
  Status status = rpc.GetStatus();
  EXPECT_THAT(
      status,
      GrpcStatusIs(
          StatusCode::INTERNAL,
          ::testing::MatchesRegex(
              "Failed to parse XdsHeaderValueOption: \\[field:header\\.key "
              "error:header \"host\" not allowed\\]")));
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

TEST_P(XdsExtProcEnd2endTest,
       RequestHeadersObservabilityExtProcConnectionErrorFailureModeFalse) {
  int port = grpc_pick_unused_port_or_die();
  std::string target = absl::StrCat("localhost:", port);
  CreateAndStartBackends(1);
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(target)
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(false)
                             .SetObservabilityMode(true)
                             .SetRequestHeaderMode()
                             .SetResponseHeaderMode()
                             .SetResponseTrailerMode()
                             .SetRequestBodyMode()
                             .SetResponseBodyMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::UNAVAILABLE,
                      MakeConnectionFailureRegex(
                          "failed to connect to all addresses; last error: ",
                          /*resolution_note=*/""),
                      RpcOptions().set_skip_cancelled_check(true));
}

TEST_P(XdsExtProcEnd2endTest,
       RequestHeadersObservabilityExtProcConnectionErrorFailureModeTrue) {
  int port = grpc_pick_unused_port_or_die();
  std::string target = absl::StrCat("localhost:", port);
  CreateAndStartBackends(1);
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(target)
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(true)
                             .SetObservabilityMode(true)
                             .SetRequestHeaderMode()
                             .SetResponseHeaderMode()
                             .SetResponseTrailerMode()
                             .SetRequestBodyMode()
                             .SetResponseBodyMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  CheckRpcSendOk(DEBUG_LOCATION);
}

TEST_P(XdsExtProcEnd2endTest, RequestHeadersRequestAttributesSent) {
  CreateAndStartBackends(1);
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetRequestHeaderMode()
                             .SetResponseHeaderMode()
                             .SetResponseTrailerMode()
                             .SetRequestBodyMode()
                             .SetResponseBodyMode()
                             .AddRequestAttribute("request.path")
                             .AddRequestAttribute("request.method")
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  RpcOptions rpc_options;
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(req.has_value());
  EXPECT_TRUE(req->has_request_headers());
  EXPECT_EQ(GetExtProcAttribute(*req, "request.path"),
            "/grpc.testing.EchoTestService/Echo");
  EXPECT_EQ(GetExtProcAttribute(*req, "request.method"), "POST");
  ext_proc_stream->SendResponse(MakeRequestHeadersMutationResponse({}));
  bool saw_response_trailers = false;
  while (!saw_response_trailers) {
    auto next_req = ext_proc_stream->GetNextRequest();
    ASSERT_TRUE(next_req.has_value());
    if (next_req->has_request_body()) {
      ext_proc_stream->SendResponse(MakeRequestBodyMutationResponse(
          next_req->request_body().body(),
          next_req->request_body().end_of_stream()));
    } else if (next_req->has_response_headers()) {
      ext_proc_stream->SendResponse(MakeResponseHeadersMutationResponse({}));
    } else if (next_req->has_response_body()) {
      ext_proc_stream->SendResponse(MakeResponseBodyMutationResponse(
          next_req->response_body().body(),
          next_req->response_body().end_of_stream()));
    } else if (next_req->has_response_trailers()) {
      saw_response_trailers = true;
      ext_proc_stream->SendResponse(MakeResponseTrailersMutationResponse({}));
    } else {
      FAIL() << "Unexpected request type: " << next_req->DebugString();
    }
  }
  Status status = rpc.GetStatus();
  EXPECT_TRUE(status.ok()) << status.error_message();
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

TEST_P(XdsExtProcEnd2endTest,
       RequestAttributesSentInRequestBodyWhenRequestHeaderIsSkip) {
  CreateAndStartBackends(1);
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetRequestBodyMode()
                             .AddRequestAttribute("request.path")
                             .AddRequestAttribute("request.method")
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  RpcOptions rpc_options;
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  std::string path_received;
  std::string method_received;
  while (true) {
    auto req = ext_proc_stream->GetNextRequest(absl::Milliseconds(500));
    if (!req.has_value()) break;
    if (req->has_request_body()) {
      if (path_received.empty()) {
        path_received = GetExtProcAttribute(*req, "request.path");
      }
      if (method_received.empty()) {
        method_received = GetExtProcAttribute(*req, "request.method");
      }
      ext_proc_stream->SendResponse(MakeRequestBodyMutationResponse(
          req->request_body().body(), req->request_body().end_of_stream()));
    } else {
      FAIL() << "Unexpected request type: " << req->DebugString();
    }
  }
  Status status = rpc.GetStatus();
  EXPECT_TRUE(status.ok()) << status.error_message();
  EXPECT_EQ(path_received, "/grpc.testing.EchoTestService/Echo");
  EXPECT_EQ(method_received, "POST");
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

TEST_P(XdsExtProcEnd2endTest, RequestBodyContinueAndReplace) {
  CreateAndStartBackends(1);
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(true)
                             .SetRequestHeaderMode()
                             .SetRequestBodyMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  RpcOptions rpc_options;
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(req.has_value());
  EXPECT_TRUE(req->has_request_headers());
  ext_proc_stream->SendResponse(MakeRequestHeadersMutationResponse({}));
  auto next_req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(next_req.has_value());
  EXPECT_TRUE(next_req->has_request_body());
  ::envoy::service::ext_proc::v3::ProcessingResponse response;
  auto* common_response = response.mutable_request_body()->mutable_response();
  common_response->set_status(
      ::envoy::service::ext_proc::v3::CommonResponse::CONTINUE_AND_REPLACE);
  common_response->mutable_body_mutation();
  ext_proc_stream->SendResponse(response);
  Status status = rpc.GetStatus();
  EXPECT_THAT(status,
              GrpcStatusIs(StatusCode::INTERNAL,
                           ::testing::HasSubstr(
                               "CONTINUE_AND_REPLACE is not supported")));
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

TEST_P(XdsExtProcEnd2endTest,
       RequestBodyExtProcConnectionErrorFailureModeFalse) {
  CreateAndStartBackends(1);
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(false)
                             .SetRequestHeaderMode()
                             .SetResponseHeaderMode()
                             .SetResponseTrailerMode()
                             .SetRequestBodyMode()
                             .SetResponseBodyMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  RpcOptions rpc_options;
  rpc_options.set_skip_cancelled_check(true);
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(req.has_value());
  EXPECT_TRUE(req->has_request_headers());
  ext_proc_stream->SendResponse(MakeRequestHeadersMutationResponse({}));
  auto next_req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(next_req.has_value());
  EXPECT_TRUE(next_req->has_request_body());
  ext_proc_stream->SendStatus(absl::ResourceExhaustedError(
      "Call closed by ext_proc server on request body"));
  Status status = rpc.GetStatus();
  EXPECT_THAT(
      status,
      GrpcStatusIs(StatusCode::RESOURCE_EXHAUSTED,
                   ::testing::HasSubstr(
                       "Call closed by ext_proc server on request body")));
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

TEST_P(XdsExtProcEnd2endTest,
       RequestBodyExtProcConnectionErrorFailureModeTrue) {
  CreateAndStartBackends(1);
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(true)
                             .SetRequestHeaderMode()
                             .SetResponseHeaderMode()
                             .SetResponseTrailerMode()
                             .SetRequestBodyMode()
                             .SetResponseBodyMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  RpcOptions rpc_options;
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(req.has_value());
  EXPECT_TRUE(req->has_request_headers());
  ext_proc_stream->SendResponse(MakeRequestHeadersMutationResponse({}));
  auto next_req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(next_req.has_value());
  EXPECT_TRUE(next_req->has_request_body());
  ext_proc_stream->SendStatus(absl::ResourceExhaustedError(
      "Call closed by ext_proc server on request body"));
  Status status = rpc.GetStatus();
  EXPECT_THAT(
      status,
      GrpcStatusIs(StatusCode::RESOURCE_EXHAUSTED,
                   ::testing::HasSubstr(
                       "Call closed by ext_proc server on request body")));
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

TEST_P(XdsExtProcEnd2endTest, RequestBodyGrpcMessageCompressed) {
  CreateAndStartBackends(1);
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(true)
                             .SetRequestHeaderMode()
                             .SetRequestBodyMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  RpcOptions rpc_options;
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(req.has_value());
  EXPECT_TRUE(req->has_request_headers());
  ext_proc_stream->SendResponse(MakeRequestHeadersMutationResponse({}));
  auto next_req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(next_req.has_value());
  EXPECT_TRUE(next_req->has_request_body());
  ::envoy::service::ext_proc::v3::ProcessingResponse response;
  auto* common_response = response.mutable_request_body()->mutable_response();
  auto* body_mutation = common_response->mutable_body_mutation();
  auto* streamed_response = body_mutation->mutable_streamed_response();
  streamed_response->set_grpc_message_compressed(true);
  ext_proc_stream->SendResponse(response);
  Status status = rpc.GetStatus();
  EXPECT_THAT(status,
              GrpcStatusIs(StatusCode::INTERNAL,
                           ::testing::HasSubstr(
                               "grpc_message_compressed is not supported")));
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

TEST_P(XdsExtProcEnd2endTest,
       RequestBodyObservabilityExtProcConnectionErrorFailureModeFalse) {
  CreateAndStartBackends(1);
  ResetStubWithUniqueArg();
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetObservabilityMode(true)
                             .SetFailureModeAllow(false)
                             .SetRequestHeaderMode()
                             .SetResponseHeaderMode()
                             .SetResponseTrailerMode()
                             .SetRequestBodyMode()
                             .SetResponseBodyMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  AsyncBidiStream stream;
  RpcOptions rpc_options;
  stream.Start(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(req.has_value());
  EXPECT_TRUE(req->has_request_headers());
  ext_proc_stream->SendResponse(MakeRequestHeadersMutationResponse({}));
  EchoRequest request;
  request.set_message(kMessage1);
  stream.StartWrite(request);
  EXPECT_TRUE(stream.WaitForWriteDone());
  auto next_req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(next_req.has_value());
  EXPECT_TRUE(next_req->has_request_body());
  ext_proc_stream->SendStatus(absl::ResourceExhaustedError(
      "Call closed by ext_proc server on request body"));
  EchoResponse response;
  stream.ReadMessage(&response);
  Status status = stream.Finish();
  EXPECT_THAT(
      status,
      GrpcStatusIs(StatusCode::RESOURCE_EXHAUSTED,
                   ::testing::HasSubstr(
                       "Call closed by ext_proc server on request body")));
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

TEST_P(XdsExtProcEnd2endTest,
       RequestBodyObservabilityExtProcConnectionErrorFailureModeTrue) {
  CreateAndStartBackends(1);
  ResetStubWithUniqueArg();
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetObservabilityMode(true)
                             .SetFailureModeAllow(true)
                             .SetRequestHeaderMode()
                             .SetResponseHeaderMode()
                             .SetResponseTrailerMode()
                             .SetRequestBodyMode()
                             .SetResponseBodyMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  AsyncBidiStream stream;
  RpcOptions rpc_options;
  stream.Start(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(req.has_value());
  EXPECT_TRUE(req->has_request_headers());
  ext_proc_stream->SendResponse(MakeRequestHeadersMutationResponse({}));
  EchoRequest request;
  request.set_message(kMessage1);
  stream.StartWrite(request);
  EXPECT_TRUE(stream.WaitForWriteDone());
  auto next_req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(next_req.has_value());
  EXPECT_TRUE(next_req->has_request_body());
  ext_proc_stream->SendStatus(absl::ResourceExhaustedError(
      "Call closed by ext_proc server on request body"));
  EchoResponse response;
  EXPECT_TRUE(stream.ReadMessage(&response));
  EXPECT_EQ(response.message(), kMessage1);
  stream.StartWritesDone();
  Status status = stream.Finish();
  EXPECT_TRUE(status.ok()) << "Expected OK, got: " << status.error_message();
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

TEST_P(XdsExtProcEnd2endTest, BidiStreamEarlyHalfCloseWithMessageFailure) {
  CreateAndStartBackends(1);
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetRequestHeaderMode()
                             .SetRequestBodyMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  AsyncBidiStream stream;
  RpcOptions rpc_options;
  stream.Start(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(req.has_value());
  EXPECT_TRUE(req->has_request_headers());
  ext_proc_stream->SendResponse(MakeRequestHeadersMutationResponse({}));
  EchoRequest request;
  request.set_message(kMessage1);
  stream.StartWrite(request);
  auto next_req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(next_req.has_value());
  EXPECT_TRUE(next_req->has_request_body());
  const auto& body_req = next_req->request_body();
  EXPECT_FALSE(body_req.end_of_stream());
  EXPECT_FALSE(body_req.end_of_stream_without_message());
  grpc::testing::EchoRequest echo_request;
  std::string mutated_body = body_req.body();
  if (echo_request.ParseFromString(body_req.body())) {
    echo_request.set_message(
        absl::StrCat(echo_request.message(), kMutatedSuffix));
    GRPC_CHECK(echo_request.SerializeToString(&mutated_body));
  }
  ::envoy::service::ext_proc::v3::ProcessingResponse proc_response;
  auto* common_response =
      proc_response.mutable_request_body()->mutable_response();
  auto* streamed_response =
      common_response->mutable_body_mutation()->mutable_streamed_response();
  streamed_response->set_body(mutated_body);
  streamed_response->set_end_of_stream(true);
  ext_proc_stream->SendResponse(proc_response);
  EXPECT_TRUE(stream.WaitForWriteDone());
  EchoResponse response;
  EXPECT_TRUE(stream.ReadMessage(&response));
  EXPECT_EQ(response.message(), kMessage1Mutated);
  request.set_message(kMessage2);
  stream.StartWrite(request);
  (void)stream.WaitForWriteDone();
  EXPECT_FALSE(stream.ReadMessage(&response));
  Status status = stream.Finish();
  EXPECT_THAT(status,
              GrpcStatusIs(StatusCode::INTERNAL,
                           ::testing::HasSubstr(
                               "Client sends closed by external processor")));
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

TEST_P(XdsExtProcEnd2endTest, BidiStreamEarlyHalfCloseWithoutMessageFailure) {
  CreateAndStartBackends(1);
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetRequestHeaderMode()
                             .SetRequestBodyMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  AsyncBidiStream stream;
  RpcOptions rpc_options;
  stream.Start(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(req.has_value());
  EXPECT_TRUE(req->has_request_headers());
  ext_proc_stream->SendResponse(MakeRequestHeadersMutationResponse({}));
  EchoRequest request;
  request.set_message(kMessage1);
  stream.StartWrite(request);
  auto next_req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(next_req.has_value());
  EXPECT_TRUE(next_req->has_request_body());
  const auto& body_req = next_req->request_body();
  EXPECT_FALSE(body_req.end_of_stream());
  EXPECT_FALSE(body_req.end_of_stream_without_message());
  ::envoy::service::ext_proc::v3::ProcessingResponse proc_response;
  auto* common_response =
      proc_response.mutable_request_body()->mutable_response();
  auto* streamed_response =
      common_response->mutable_body_mutation()->mutable_streamed_response();
  streamed_response->set_end_of_stream(true);
  streamed_response->set_end_of_stream_without_message(true);
  ext_proc_stream->SendResponse(proc_response);
  (void)stream.WaitForWriteDone();
  EchoResponse response;
  request.set_message(kMessage2);
  stream.StartWrite(request);
  (void)stream.WaitForWriteDone();
  EXPECT_FALSE(stream.ReadMessage(&response));
  Status status = stream.Finish();
  EXPECT_THAT(status,
              GrpcStatusIs(StatusCode::INTERNAL,
                           ::testing::HasSubstr(
                               "Client sends closed by external processor")));
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

TEST_P(XdsExtProcEnd2endTest, BidiStreamNormalHalfCloseSuccess) {
  CreateAndStartBackends(1);
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetRequestHeaderMode()
                             .SetRequestBodyMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  AsyncBidiStream stream;
  RpcOptions rpc_options;
  stream.Start(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(req.has_value());
  EXPECT_TRUE(req->has_request_headers());
  ext_proc_stream->SendResponse(MakeRequestHeadersMutationResponse({}));
  EchoRequest request;
  EchoResponse response;
  for (int i = 1; i <= 3; ++i) {
    request.set_message(absl::StrCat("message", i));
    stream.StartWrite(request);
    auto next_req = ext_proc_stream->GetNextRequest();
    ASSERT_TRUE(next_req.has_value());
    EXPECT_TRUE(next_req->has_request_body());
    const auto& body_req = next_req->request_body();
    EXPECT_FALSE(body_req.end_of_stream());
    EXPECT_FALSE(body_req.end_of_stream_without_message());
    ext_proc_stream->SendResponse(MakeRequestBodyMutationResponse(
        body_req.body(), body_req.end_of_stream()));
    EXPECT_TRUE(stream.WaitForWriteDone());
    EXPECT_TRUE(stream.ReadMessage(&response));
    EXPECT_EQ(response.message(), absl::StrCat("message", i));
  }
  stream.StartWritesDone();
  auto eos_req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(eos_req.has_value());
  EXPECT_TRUE(eos_req->has_request_body());
  const auto& eos_body_req = eos_req->request_body();
  EXPECT_TRUE(eos_body_req.end_of_stream());
  EXPECT_TRUE(eos_body_req.end_of_stream_without_message());
  ::envoy::service::ext_proc::v3::ProcessingResponse proc_response;
  auto* common_response =
      proc_response.mutable_request_body()->mutable_response();
  common_response->mutable_body_mutation()
      ->mutable_streamed_response()
      ->set_end_of_stream_without_message(true);
  ext_proc_stream->SendResponse(proc_response);
  EXPECT_FALSE(stream.ReadMessage(&response));
  Status status = stream.Finish();
  EXPECT_TRUE(status.ok()) << status.error_message();
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

//
// Response Headers tests
//

TEST_P(XdsExtProcEnd2endTest, ResponseHeadersContinueAndReplaceFails) {
  CreateAndStartBackends(1);
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetRequestHeaderMode()
                             .SetResponseHeaderMode()
                             .SetResponseTrailerMode()
                             .SetRequestBodyMode()
                             .SetResponseBodyMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  RpcOptions rpc_options;
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(req.has_value());
  EXPECT_TRUE(req->has_request_headers());
  ext_proc_stream->SendResponse(MakeRequestHeadersMutationResponse({}));
  auto next_req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(next_req.has_value());
  EXPECT_TRUE(next_req->has_request_body());
  ext_proc_stream->SendResponse(MakeRequestBodyMutationResponse(
      next_req->request_body().body(),
      next_req->request_body().end_of_stream()));
  auto resp_headers_req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(resp_headers_req.has_value());
  EXPECT_TRUE(resp_headers_req->has_response_headers());
  ::envoy::service::ext_proc::v3::ProcessingResponse response;
  response.mutable_response_headers()->mutable_response()->set_status(
      ::envoy::service::ext_proc::v3::CommonResponse::CONTINUE_AND_REPLACE);
  ext_proc_stream->SendResponse(response);
  Status status = rpc.GetStatus();
  EXPECT_THAT(status,
              GrpcStatusIs(StatusCode::INTERNAL,
                           ::testing::HasSubstr(
                               "CONTINUE_AND_REPLACE is not supported")));
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

TEST_P(XdsExtProcEnd2endTest,
       ResponseHeadersExtProcConnectionErrorFailureModeFalse) {
  int port = grpc_pick_unused_port_or_die();
  std::string target = absl::StrCat("localhost:", port);
  CreateAndStartBackends(1);
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(target)
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(false)
                             .SetResponseHeaderMode()
                             .SetResponseTrailerMode()
                             .SetResponseBodyMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  RpcOptions rpc_options;
  rpc_options.set_skip_cancelled_check(true);
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get(), rpc_options);
  Status status = rpc.GetStatus();
  EXPECT_THAT(status,
              GrpcStatusIs(StatusCode::UNAVAILABLE,
                           ::testing::MatchesRegex(MakeConnectionFailureRegex(
                               "failed to connect to all addresses; "
                               "last error: ",
                               ""))));
}

TEST_P(XdsExtProcEnd2endTest,
       ResponseHeadersExtProcConnectionErrorFailureModeTrue) {
  int port = grpc_pick_unused_port_or_die();
  std::string target = absl::StrCat("localhost:", port);
  CreateAndStartBackends(1);
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(target)
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(true)
                             .SetResponseHeaderMode()
                             .SetResponseTrailerMode()
                             .SetResponseBodyMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  CheckRpcSendOk(DEBUG_LOCATION);
}

TEST_P(XdsExtProcEnd2endTest, ResponseHeadersInvalidHeaderMutationFails) {
  CreateAndStartBackends(1);
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetRequestHeaderMode()
                             .SetResponseHeaderMode()
                             .SetResponseTrailerMode()
                             .SetRequestBodyMode()
                             .SetResponseBodyMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  RpcOptions rpc_options;
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(req.has_value());
  EXPECT_TRUE(req->has_request_headers());
  ext_proc_stream->SendResponse(MakeRequestHeadersMutationResponse({}));
  auto next_req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(next_req.has_value());
  EXPECT_TRUE(next_req->has_request_body());
  ext_proc_stream->SendResponse(MakeRequestBodyMutationResponse(
      next_req->request_body().body(),
      next_req->request_body().end_of_stream()));
  auto resp_headers_req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(resp_headers_req.has_value());
  EXPECT_TRUE(resp_headers_req->has_response_headers());
  ::envoy::service::ext_proc::v3::ProcessingResponse response;
  auto* mutation = response.mutable_response_headers()
                       ->mutable_response()
                       ->mutable_header_mutation();
  auto* header = mutation->add_set_headers();
  header->mutable_header()->set_key("host");
  header->mutable_header()->set_value("invalid-host");
  ext_proc_stream->SendResponse(response);
  Status status = rpc.GetStatus();
  EXPECT_THAT(status,
              GrpcStatusIs(
                  StatusCode::INTERNAL,
                  ::testing::HasSubstr(
                      "Failed to parse XdsHeaderValueOption: [field:header.key "
                      "error:header \"host\" not allowed]")));
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

TEST_P(XdsExtProcEnd2endTest,
       ResponseHeadersObservabilityExtProcConnectionErrorFailureModeFalse) {
  int port = grpc_pick_unused_port_or_die();
  std::string target = absl::StrCat("localhost:", port);
  CreateAndStartBackends(1);
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(target)
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(false)
                             .SetObservabilityMode(true)
                             .SetResponseHeaderMode()
                             .SetResponseTrailerMode()
                             .SetResponseBodyMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::UNAVAILABLE,
                      MakeConnectionFailureRegex(
                          "failed to connect to all addresses; last error: ",
                          /*resolution_note=*/""),
                      RpcOptions().set_skip_cancelled_check(true));
}

TEST_P(XdsExtProcEnd2endTest,
       ResponseHeadersObservabilityExtProcConnectionErrorFailureModeTrue) {
  int port = grpc_pick_unused_port_or_die();
  std::string target = absl::StrCat("localhost:", port);
  CreateAndStartBackends(1);
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(target)
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(true)
                             .SetObservabilityMode(true)
                             .SetResponseHeaderMode()
                             .SetResponseTrailerMode()
                             .SetResponseBodyMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  CheckRpcSendOk(DEBUG_LOCATION);
}

//
// Response Body tests
//

TEST_P(XdsExtProcEnd2endTest, ResponseBodyContinueAndReplace) {
  CreateAndStartBackends(1);
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(true)
                             .SetResponseHeaderMode()
                             .SetResponseTrailerMode()
                             .SetResponseBodyMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  RpcOptions rpc_options;
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto resp_headers_req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(resp_headers_req.has_value());
  EXPECT_TRUE(resp_headers_req->has_response_headers());
  ext_proc_stream->SendResponse(MakeResponseHeadersMutationResponse({}));
  auto resp_body_req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(resp_body_req.has_value());
  EXPECT_TRUE(resp_body_req->has_response_body());
  ::envoy::service::ext_proc::v3::ProcessingResponse response;
  auto* common_response = response.mutable_response_body()->mutable_response();
  common_response->set_status(
      ::envoy::service::ext_proc::v3::
          CommonResponse_ResponseStatus_CONTINUE_AND_REPLACE);
  common_response->mutable_body_mutation();
  ext_proc_stream->SendResponse(response);
  Status status = rpc.GetStatus();
  EXPECT_THAT(status,
              GrpcStatusIs(StatusCode::INTERNAL,
                           ::testing::HasSubstr(
                               "CONTINUE_AND_REPLACE is not supported")));
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

TEST_P(XdsExtProcEnd2endTest,
       ResponseBodyExtProcConnectionErrorFailureModeFalse) {
  CreateAndStartBackends(1);
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(false)
                             .SetResponseHeaderMode()
                             .SetResponseTrailerMode()
                             .SetResponseBodyMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  RpcOptions rpc_options;
  rpc_options.set_skip_cancelled_check(true);
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto resp_headers_req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(resp_headers_req.has_value());
  EXPECT_TRUE(resp_headers_req->has_response_headers());
  ext_proc_stream->SendResponse(MakeResponseHeadersMutationResponse({}));
  auto resp_body_req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(resp_body_req.has_value());
  EXPECT_TRUE(resp_body_req->has_response_body());
  ext_proc_stream->SendStatus(absl::ResourceExhaustedError(
      "Call closed by ext_proc server on response body"));
  Status status = rpc.GetStatus();
  EXPECT_THAT(
      status,
      GrpcStatusIs(StatusCode::RESOURCE_EXHAUSTED,
                   ::testing::HasSubstr(
                       "Call closed by ext_proc server on response body")));
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

TEST_P(XdsExtProcEnd2endTest,
       ResponseBodyExtProcConnectionErrorFailureModeTrue) {
  CreateAndStartBackends(1);
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(true)
                             .SetResponseHeaderMode()
                             .SetResponseTrailerMode()
                             .SetResponseBodyMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  RpcOptions rpc_options;
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto resp_headers_req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(resp_headers_req.has_value());
  EXPECT_TRUE(resp_headers_req->has_response_headers());
  ext_proc_stream->SendResponse(MakeResponseHeadersMutationResponse({}));
  auto resp_body_req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(resp_body_req.has_value());
  EXPECT_TRUE(resp_body_req->has_response_body());
  ext_proc_stream->SendStatus(absl::ResourceExhaustedError(
      "Call closed by ext_proc server on response body"));
  Status status = rpc.GetStatus();
  EXPECT_THAT(
      status,
      GrpcStatusIs(StatusCode::RESOURCE_EXHAUSTED,
                   ::testing::HasSubstr(
                       "Call closed by ext_proc server on response body")));
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

TEST_P(XdsExtProcEnd2endTest, ResponseBodyGrpcMessageCompressed) {
  CreateAndStartBackends(1);
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(true)
                             .SetResponseHeaderMode()
                             .SetResponseTrailerMode()
                             .SetResponseBodyMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  RpcOptions rpc_options;
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto resp_headers_req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(resp_headers_req.has_value());
  EXPECT_TRUE(resp_headers_req->has_response_headers());
  ext_proc_stream->SendResponse(MakeResponseHeadersMutationResponse({}));
  auto resp_body_req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(resp_body_req.has_value());
  EXPECT_TRUE(resp_body_req->has_response_body());
  ::envoy::service::ext_proc::v3::ProcessingResponse response;
  auto* common_response = response.mutable_response_body()->mutable_response();
  auto* body_mutation = common_response->mutable_body_mutation();
  auto* streamed_response = body_mutation->mutable_streamed_response();
  streamed_response->set_grpc_message_compressed(true);
  ext_proc_stream->SendResponse(response);
  Status status = rpc.GetStatus();
  EXPECT_THAT(status,
              GrpcStatusIs(StatusCode::INTERNAL,
                           ::testing::HasSubstr(
                               "grpc_message_compressed is not supported")));
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

TEST_P(XdsExtProcEnd2endTest, ResponseBodyObservabilityStreamErrorAllowCall) {
  CreateAndStartBackends(1);
  ResetStubWithUniqueArg();
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetObservabilityMode(true)
                             .SetFailureModeAllow(true)
                             .SetRequestHeaderMode()
                             .SetResponseHeaderMode()
                             .SetResponseTrailerMode()
                             .SetResponseBodyMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  AsyncBidiStream stream;
  RpcOptions rpc_options;
  stream.Start(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(req.has_value());
  EXPECT_TRUE(req->has_request_headers());
  ext_proc_stream->SendResponse(MakeRequestHeadersMutationResponse({}));
  EchoRequest request;
  request.set_message(kMessage1);
  stream.StartWrite(request);
  EXPECT_TRUE(stream.WaitForWriteDone());
  auto resp_headers_req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(resp_headers_req.has_value());
  EXPECT_TRUE(resp_headers_req->has_response_headers());
  ext_proc_stream->SendResponseAndStatus(
      MakeResponseHeadersMutationResponse({}),
      absl::ResourceExhaustedError(
          "Call closed by ext_proc server after response headers"));
  EchoResponse response;
  EXPECT_TRUE(stream.ReadMessage(&response));
  EXPECT_EQ(response.message(), kMessage1);
  stream.StartWritesDone();
  Status status = stream.Finish();
  EXPECT_TRUE(status.ok()) << status.error_message();
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

TEST_P(XdsExtProcEnd2endTest, ResponseBodyObservabilityStreamErrorFailCall) {
  CreateAndStartBackends(1);
  ResetStubWithUniqueArg();
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetObservabilityMode(true)
                             .SetFailureModeAllow(false)
                             .SetRequestHeaderMode()
                             .SetResponseHeaderMode()
                             .SetResponseTrailerMode()
                             .SetResponseBodyMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  AsyncBidiStream stream;
  RpcOptions rpc_options;
  stream.Start(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(req.has_value());
  EXPECT_TRUE(req->has_request_headers());
  ext_proc_stream->SendResponse(MakeRequestHeadersMutationResponse({}));
  EchoRequest request;
  request.set_message(kMessage1);
  stream.StartWrite(request);
  EXPECT_TRUE(stream.WaitForWriteDone());
  auto resp_headers_req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(resp_headers_req.has_value());
  EXPECT_TRUE(resp_headers_req->has_response_headers());
  ext_proc_stream->SendResponseAndStatus(
      MakeResponseHeadersMutationResponse({}),
      absl::ResourceExhaustedError(
          "Call closed by ext_proc server after response headers"));
  EchoResponse response;
  stream.ReadMessage(&response);
  Status status = stream.Finish();
  EXPECT_THAT(
      status,
      GrpcStatusIs(
          StatusCode::RESOURCE_EXHAUSTED,
          ::testing::HasSubstr(
              "Call closed by ext_proc server after response headers")));
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

//
// Response Trailers tests
//

TEST_P(XdsExtProcEnd2endTest,
       ResponseTrailersExtProcConnectionErrorFailureModeFalse) {
  int port = grpc_pick_unused_port_or_die();
  std::string target = absl::StrCat("localhost:", port);
  CreateAndStartBackends(1);
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(target)
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(false)
                             .SetResponseTrailerMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  RpcOptions rpc_options;
  rpc_options.set_skip_cancelled_check(true);
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get(), rpc_options);
  Status status = rpc.GetStatus();
  EXPECT_THAT(status,
              GrpcStatusIs(StatusCode::UNAVAILABLE,
                           ::testing::MatchesRegex(MakeConnectionFailureRegex(
                               "failed to connect to all addresses; "
                               "last error: ",
                               ""))));
}

TEST_P(XdsExtProcEnd2endTest,
       ResponseTrailersExtProcConnectionErrorFailureModeTrue) {
  int port = grpc_pick_unused_port_or_die();
  std::string target = absl::StrCat("localhost:", port);
  CreateAndStartBackends(1);
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(target)
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(true)
                             .SetResponseTrailerMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  CheckRpcSendOk(DEBUG_LOCATION);
}

TEST_P(XdsExtProcEnd2endTest, ResponseTrailersInvalidHeaderMutationFails) {
  CreateAndStartBackends(1);
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetRequestHeaderMode()
                             .SetResponseHeaderMode()
                             .SetResponseTrailerMode()
                             .SetRequestBodyMode()
                             .SetResponseBodyMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  RpcOptions rpc_options;
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  while (true) {
    auto req = ext_proc_stream->GetNextRequest();
    ASSERT_TRUE(req.has_value());
    if (req->has_request_headers()) {
      ext_proc_stream->SendResponse(MakeRequestHeadersMutationResponse({}));
    } else if (req->has_request_body()) {
      ext_proc_stream->SendResponse(MakeRequestBodyMutationResponse(
          req->request_body().body(), req->request_body().end_of_stream()));
    } else if (req->has_response_headers()) {
      ext_proc_stream->SendResponse(MakeResponseHeadersMutationResponse({}));
    } else if (req->has_response_body()) {
      ext_proc_stream->SendResponse(MakeResponseBodyMutationResponse(
          req->response_body().body(), req->response_body().end_of_stream()));
    } else if (req->has_response_trailers()) {
      ext_proc_stream->SendResponse(
          MakeResponseTrailersMutationResponse({{"host", "invalid-host"}}));
      break;
    } else {
      FAIL() << "Unexpected request type: " << req->DebugString();
    }
  }
  Status status = rpc.GetStatus();
  EXPECT_THAT(
      status,
      GrpcStatusIs(
          StatusCode::INTERNAL,
          ::testing::ContainsRegex(
              "Failed to parse XdsHeaderValueOption: \\[field:header\\.key "
              "error:header \"host\" not allowed\\]")));
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

TEST_P(XdsExtProcEnd2endTest,
       ResponseTrailersObservabilityExtProcConnectionErrorFailureModeFalse) {
  int port = grpc_pick_unused_port_or_die();
  std::string target = absl::StrCat("localhost:", port);
  CreateAndStartBackends(1);
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(target)
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(false)
                             .SetObservabilityMode(true)
                             .SetResponseTrailerMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::UNAVAILABLE,
                      MakeConnectionFailureRegex(
                          "failed to connect to all addresses; last error: ",
                          /*resolution_note=*/""),
                      RpcOptions().set_skip_cancelled_check(true));
}

TEST_P(XdsExtProcEnd2endTest,
       ResponseTrailersObservabilityExtProcConnectionErrorFailureModeTrue) {
  int port = grpc_pick_unused_port_or_die();
  std::string target = absl::StrCat("localhost:", port);
  CreateAndStartBackends(1);
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(target)
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(true)
                             .SetObservabilityMode(true)
                             .SetResponseTrailerMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  CheckRpcSendOk(DEBUG_LOCATION);
}

//
// Immediate Response (Disabled) tests
//

TEST_P(XdsExtProcEnd2endTest, DisableImmediateResponseForRequestBody) {
  CreateAndStartBackends(1);
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetDisableImmediateResponse(true)
                             .SetFailureModeAllow(false)
                             .SetRequestHeaderMode()
                             .SetResponseHeaderMode()
                             .SetResponseTrailerMode()
                             .SetRequestBodyMode()
                             .SetResponseBodyMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  RpcOptions rpc_options;
  rpc_options.set_skip_cancelled_check(true);
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(req.has_value());
  EXPECT_TRUE(req->has_request_headers());
  ext_proc_stream->SendResponse(MakeRequestHeadersMutationResponse({}));
  auto req_body = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(req_body.has_value());
  EXPECT_TRUE(req_body->has_request_body());
  ext_proc_stream->SendResponse(MakeImmediateResponse(
      grpc::StatusCode::PERMISSION_DENIED,
      "Access Denied by ExtProc (Request Body)",
      {{kImmediateResponseHeaderKey, kHeaderMutatedValue}}));
  Status status = rpc.GetStatus();
  EXPECT_THAT(
      status,
      GrpcStatusIs(
          StatusCode::INTERNAL,
          ::testing::HasSubstr(
              "unhandled immediate response due to config disabled it")));
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

TEST_P(XdsExtProcEnd2endTest, DisableImmediateResponseForRequestHeaders) {
  CreateAndStartBackends(1);
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetDisableImmediateResponse(true)
                             .SetFailureModeAllow(false)
                             .SetRequestHeaderMode()
                             .SetResponseHeaderMode()
                             .SetResponseTrailerMode()
                             .SetRequestBodyMode()
                             .SetResponseBodyMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  RpcOptions rpc_options;
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(req.has_value());
  EXPECT_TRUE(req->has_request_headers());
  ext_proc_stream->SendResponse(MakeImmediateResponse(
      grpc::StatusCode::PERMISSION_DENIED,
      "Access Denied by ExtProc (Request Headers)",
      {{kImmediateResponseHeaderKey, kHeaderMutatedValue}}));
  Status status = rpc.GetStatus();
  EXPECT_THAT(
      status,
      GrpcStatusIs(
          StatusCode::INTERNAL,
          ::testing::HasSubstr(
              "unhandled immediate response due to config disabled it")));
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

TEST_P(XdsExtProcEnd2endTest, DisableImmediateResponseForResponseBody) {
  CreateAndStartBackends(1);
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetDisableImmediateResponse(true)
                             .SetFailureModeAllow(false)
                             .SetRequestHeaderMode()
                             .SetRequestBodyMode()
                             .SetResponseBodyMode()
                             .SetResponseTrailerMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  RpcOptions rpc_options;
  rpc_options.set_echo_metadata_initially(true);
  rpc_options.set_echo_metadata(true);
  rpc_options.set_skip_cancelled_check(true);
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  while (true) {
    auto req = ext_proc_stream->GetNextRequest();
    ASSERT_TRUE(req.has_value());
    if (req->has_request_headers()) {
      ext_proc_stream->SendResponse(MakeRequestHeadersMutationResponse({}));
    } else if (req->has_request_body()) {
      ext_proc_stream->SendResponse(MakeRequestBodyMutationResponse(
          req->request_body().body(), req->request_body().end_of_stream()));
    } else if (req->has_response_headers()) {
      ext_proc_stream->SendResponse(MakeResponseHeadersMutationResponse({}));
    } else if (req->has_response_body()) {
      ext_proc_stream->SendResponse(MakeImmediateResponse(
          grpc::StatusCode::PERMISSION_DENIED,
          "Access Denied by ExtProc (Response Body)",
          {{kImmediateResponseHeaderKey, kHeaderMutatedValue}}));
      break;
    } else {
      FAIL() << "Unexpected request type: " << req->DebugString();
    }
  }
  Status status = rpc.GetStatus();
  EXPECT_THAT(
      status,
      GrpcStatusIs(
          StatusCode::INTERNAL,
          ::testing::HasSubstr(
              "unhandled immediate response due to config disabled it")));
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

TEST_P(XdsExtProcEnd2endTest, DisableImmediateResponseForResponseHeaders) {
  CreateAndStartBackends(1);
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetDisableImmediateResponse(true)
                             .SetFailureModeAllow(false)
                             .SetRequestHeaderMode()
                             .SetResponseHeaderMode()
                             .SetResponseTrailerMode()
                             .SetRequestBodyMode()
                             .SetResponseBodyMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  RpcOptions rpc_options;
  rpc_options.set_echo_metadata_initially(true);
  rpc_options.set_echo_metadata(true);
  rpc_options.set_skip_cancelled_check(true);
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  while (true) {
    auto req = ext_proc_stream->GetNextRequest();
    ASSERT_TRUE(req.has_value());
    if (req->has_request_headers()) {
      ext_proc_stream->SendResponse(MakeRequestHeadersMutationResponse({}));
    } else if (req->has_request_body()) {
      ext_proc_stream->SendResponse(MakeRequestBodyMutationResponse(
          req->request_body().body(), req->request_body().end_of_stream()));
    } else if (req->has_response_headers()) {
      ext_proc_stream->SendResponse(MakeImmediateResponse(
          grpc::StatusCode::PERMISSION_DENIED,
          "Access Denied by ExtProc (Response Headers)",
          {{kImmediateResponseHeaderKey, kHeaderMutatedValue}}));
      break;
    } else {
      FAIL() << "Unexpected request type: " << req->DebugString();
    }
  }
  Status status = rpc.GetStatus();
  EXPECT_THAT(
      status,
      GrpcStatusIs(
          StatusCode::INTERNAL,
          ::testing::HasSubstr(
              "unhandled immediate response due to config disabled it")));
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

TEST_P(XdsExtProcEnd2endTest, DisableImmediateResponseForResponseTrailers) {
  CreateAndStartBackends(1);
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetDisableImmediateResponse(true)
                             .SetFailureModeAllow(false)
                             .SetRequestHeaderMode()
                             .SetResponseHeaderMode()
                             .SetResponseTrailerMode()
                             .SetRequestBodyMode()
                             .SetResponseBodyMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  RpcOptions rpc_options;
  rpc_options.set_echo_metadata_initially(true);
  rpc_options.set_echo_metadata(true);
  rpc_options.set_skip_cancelled_check(true);
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  while (true) {
    auto req = ext_proc_stream->GetNextRequest();
    ASSERT_TRUE(req.has_value());
    if (req->has_request_headers()) {
      ext_proc_stream->SendResponse(MakeRequestHeadersMutationResponse({}));
    } else if (req->has_request_body()) {
      ext_proc_stream->SendResponse(MakeRequestBodyMutationResponse(
          req->request_body().body(), req->request_body().end_of_stream()));
    } else if (req->has_response_headers()) {
      ext_proc_stream->SendResponse(MakeResponseHeadersMutationResponse({}));
    } else if (req->has_response_body()) {
      ext_proc_stream->SendResponse(MakeResponseBodyMutationResponse(
          req->response_body().body(), req->response_body().end_of_stream()));
    } else if (req->has_response_trailers()) {
      ext_proc_stream->SendResponse(MakeImmediateResponse(
          grpc::StatusCode::PERMISSION_DENIED,
          "Access Denied by ExtProc (Response Trailers)",
          {{kImmediateResponseHeaderKey, kHeaderMutatedValue}}));
      break;
    } else {
      FAIL() << "Unexpected request type: " << req->DebugString();
    }
  }
  Status status = rpc.GetStatus();
  EXPECT_THAT(
      status,
      GrpcStatusIs(
          StatusCode::INTERNAL,
          ::testing::HasSubstr(
              "unhandled immediate response due to config disabled it")));
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

//
// Immediate Response (Enabled) tests
//

TEST_P(XdsExtProcEnd2endTest, ImmediateResponseForRequestBody) {
  CreateAndStartBackends(1);
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetRequestHeaderMode()
                             .SetResponseHeaderMode()
                             .SetResponseTrailerMode()
                             .SetRequestBodyMode()
                             .SetResponseBodyMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  RpcOptions rpc_options;
  rpc_options.set_echo_metadata_initially(true);
  rpc_options.set_echo_metadata(true);
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(req.has_value());
  EXPECT_TRUE(req->has_request_headers());
  ext_proc_stream->SendResponse(MakeRequestHeadersMutationResponse({}));
  auto req_body = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(req_body.has_value());
  EXPECT_TRUE(req_body->has_request_body());
  ext_proc_stream->SendResponse(MakeImmediateResponse(
      grpc::StatusCode::PERMISSION_DENIED,
      "Access Denied by ExtProc (Request Body)",
      {{kImmediateResponseHeaderKey, kHeaderMutatedValue}}));
  Status status = rpc.GetStatus();
  EXPECT_THAT(status,
              GrpcStatusIs(StatusCode::PERMISSION_DENIED,
                           ::testing::HasSubstr(
                               "Access Denied by ExtProc (Request Body)")));
  auto server_trailing_metadata = rpc.GetServerTrailingMetadata();
  auto it = server_trailing_metadata.find(kImmediateResponseHeaderKey);
  EXPECT_NE(it, server_trailing_metadata.end());
  EXPECT_EQ(it->second, kHeaderMutatedValue);
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

TEST_P(XdsExtProcEnd2endTest, ImmediateResponseForRequestHeaders) {
  CreateAndStartBackends(1);
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetRequestHeaderMode()
                             .SetResponseHeaderMode()
                             .SetResponseTrailerMode()
                             .SetRequestBodyMode()
                             .SetResponseBodyMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  RpcOptions rpc_options;
  rpc_options.set_echo_metadata_initially(true);
  rpc_options.set_echo_metadata(true);
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(req.has_value());
  EXPECT_TRUE(req->has_request_headers());
  ext_proc_stream->SendResponse(MakeImmediateResponse(
      grpc::StatusCode::PERMISSION_DENIED,
      "Access Denied by ExtProc (Request Headers)",
      {{kImmediateResponseHeaderKey, kHeaderMutatedValue}}));
  Status status = rpc.GetStatus();
  EXPECT_THAT(status,
              GrpcStatusIs(StatusCode::PERMISSION_DENIED,
                           ::testing::HasSubstr(
                               "Access Denied by ExtProc (Request Headers)")));
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

TEST_P(XdsExtProcEnd2endTest, ImmediateResponseForResponseBody) {
  CreateAndStartBackends(1);
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetRequestHeaderMode()
                             .SetResponseTrailerMode()
                             .SetRequestBodyMode()
                             .SetResponseBodyMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  RpcOptions rpc_options;
  rpc_options.set_echo_metadata_initially(true);
  rpc_options.set_echo_metadata(true);
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  while (true) {
    auto req = ext_proc_stream->GetNextRequest();
    ASSERT_TRUE(req.has_value());
    if (req->has_request_headers()) {
      ext_proc_stream->SendResponse(MakeRequestHeadersMutationResponse({}));
    } else if (req->has_request_body()) {
      ext_proc_stream->SendResponse(MakeRequestBodyMutationResponse(
          req->request_body().body(), req->request_body().end_of_stream()));
    } else if (req->has_response_headers()) {
      ext_proc_stream->SendResponse(MakeResponseHeadersMutationResponse({}));
    } else if (req->has_response_body()) {
      ext_proc_stream->SendResponse(MakeImmediateResponse(
          grpc::StatusCode::PERMISSION_DENIED,
          "Access Denied by ExtProc (Response Body)",
          {{kImmediateResponseHeaderKey, kHeaderMutatedValue}}));
      break;
    } else {
      FAIL() << "Unexpected request type: " << req->DebugString();
    }
  }
  Status status = rpc.GetStatus();
  EXPECT_THAT(status,
              GrpcStatusIs(StatusCode::PERMISSION_DENIED,
                           ::testing::HasSubstr(
                               "Access Denied by ExtProc (Response Body)")));
  // On the gRPC client side, an ImmediateResponse will set the status and
  // trailing metadata. On the gRPC server side, it will cause the server to
  // immediately send trailers with the specified status.
  if (!GetParam().filter_on_server()) {
    auto server_trailing_metadata = rpc.GetServerTrailingMetadata();
    auto it = server_trailing_metadata.find(kImmediateResponseHeaderKey);
    EXPECT_NE(it, server_trailing_metadata.end());
    EXPECT_EQ(it->second, kHeaderMutatedValue);
  }
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

TEST_P(XdsExtProcEnd2endTest, ImmediateResponseForResponseHeaders) {
  CreateAndStartBackends(1);
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetRequestHeaderMode()
                             .SetResponseHeaderMode()
                             .SetResponseTrailerMode()
                             .SetRequestBodyMode()
                             .SetResponseBodyMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  RpcOptions rpc_options;
  rpc_options.set_echo_metadata_initially(true);
  rpc_options.set_echo_metadata(true);
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  while (true) {
    auto req = ext_proc_stream->GetNextRequest();
    ASSERT_TRUE(req.has_value());
    if (req->has_request_headers()) {
      ext_proc_stream->SendResponse(MakeRequestHeadersMutationResponse({}));
    } else if (req->has_request_body()) {
      ext_proc_stream->SendResponse(MakeRequestBodyMutationResponse(
          req->request_body().body(), req->request_body().end_of_stream()));
    } else if (req->has_response_headers()) {
      ext_proc_stream->SendResponse(MakeImmediateResponse(
          grpc::StatusCode::PERMISSION_DENIED,
          "Access Denied by ExtProc (Response Headers)",
          {{kImmediateResponseHeaderKey, kHeaderMutatedValue}}));
      break;
    } else {
      FAIL() << "Unexpected request type: " << req->DebugString();
    }
  }
  Status status = rpc.GetStatus();
  EXPECT_THAT(status,
              GrpcStatusIs(StatusCode::PERMISSION_DENIED,
                           ::testing::HasSubstr(
                               "Access Denied by ExtProc (Response Headers)")));
  // On the gRPC client side, an ImmediateResponse will set the status and
  // trailing metadata. On the gRPC server side, it will cause the server to
  // immediately send trailers with the specified status.
  if (!GetParam().filter_on_server()) {
    auto server_trailing_metadata = rpc.GetServerTrailingMetadata();
    auto it = server_trailing_metadata.find(kImmediateResponseHeaderKey);
    EXPECT_NE(it, server_trailing_metadata.end());
    EXPECT_EQ(it->second, kHeaderMutatedValue);
  }
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

TEST_P(XdsExtProcEnd2endTest, ImmediateResponseForResponseTrailers) {
  CreateAndStartBackends(1);
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetRequestHeaderMode()
                             .SetResponseHeaderMode()
                             .SetResponseTrailerMode()
                             .SetRequestBodyMode()
                             .SetResponseBodyMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  RpcOptions rpc_options;
  rpc_options.set_echo_metadata_initially(true);
  rpc_options.set_echo_metadata(true);
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  while (true) {
    auto req = ext_proc_stream->GetNextRequest();
    ASSERT_TRUE(req.has_value());
    if (req->has_request_headers()) {
      ext_proc_stream->SendResponse(MakeRequestHeadersMutationResponse({}));
    } else if (req->has_request_body()) {
      ext_proc_stream->SendResponse(MakeRequestBodyMutationResponse(
          req->request_body().body(), req->request_body().end_of_stream()));
    } else if (req->has_response_headers()) {
      ext_proc_stream->SendResponse(MakeResponseHeadersMutationResponse({}));
    } else if (req->has_response_body()) {
      ext_proc_stream->SendResponse(MakeResponseBodyMutationResponse(
          req->response_body().body(), req->response_body().end_of_stream()));
    } else if (req->has_response_trailers()) {
      ext_proc_stream->SendResponse(MakeImmediateResponse(
          grpc::StatusCode::PERMISSION_DENIED,
          "Access Denied by ExtProc (Response Trailers)",
          {{kImmediateResponseHeaderKey, kHeaderMutatedValue}}));
      break;
    } else {
      FAIL() << "Unexpected request type: " << req->DebugString();
    }
  }
  Status status = rpc.GetStatus();
  EXPECT_THAT(status, GrpcStatusIs(
                          StatusCode::PERMISSION_DENIED,
                          ::testing::HasSubstr(
                              "Access Denied by ExtProc (Response Trailers)")));
  // On the gRPC client side, an ImmediateResponse will set the status and
  // trailing metadata. On the gRPC server side, it will cause the server to
  // immediately send trailers with the specified status.
  if (!GetParam().filter_on_server()) {
    auto server_trailing_metadata = rpc.GetServerTrailingMetadata();
    auto it = server_trailing_metadata.find(kImmediateResponseHeaderKey);
    EXPECT_NE(it, server_trailing_metadata.end());
    EXPECT_EQ(it->second, kHeaderMutatedValue);
  }
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

//
// Stream Drain tests
//

TEST_P(XdsExtProcEnd2endTest, StreamDrainRequestOnClientBody) {
  CreateAndStartBackends(1);
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(false)
                             .SetRequestHeaderMode()
                             .SetResponseHeaderMode()
                             .SetRequestBodyMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  AsyncBidiStream stream;
  RpcOptions rpc_options;
  stream.Start(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(req.has_value());
  EXPECT_TRUE(req->has_request_headers());
  ext_proc_stream->SendResponse(MakeRequestHeadersMutationResponse({}));
  EchoRequest request;
  request.set_message(kMessage1);
  stream.StartWrite(request);
  auto next_req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(next_req.has_value());
  EXPECT_TRUE(next_req->has_request_body());
  const auto& body_req = next_req->request_body();
  grpc::testing::EchoRequest echo_request;
  std::string mutated_body = body_req.body();
  if (echo_request.ParseFromString(body_req.body())) {
    echo_request.set_message(absl::StrCat(echo_request.message(), "_modified"));
    GRPC_CHECK(echo_request.SerializeToString(&mutated_body));
  }
  ext_proc_stream->SendResponse(MakeRequestBodyMutationResponse(
      mutated_body, false, /*request_drain=*/true));
  EXPECT_TRUE(stream.WaitForWriteDone());
  EchoResponse response;
  EXPECT_TRUE(stream.ReadMessage(&response));
  EXPECT_EQ(response.message(), "message1_modified");
  request.set_message(kMessage2);
  stream.StartWrite(request);
  EXPECT_TRUE(stream.WaitForWriteDone());
  EXPECT_TRUE(stream.ReadMessage(&response));
  EXPECT_EQ(response.message(), kMessage2);
  stream.StartWritesDone();
  Status status = stream.Finish();
  EXPECT_TRUE(status.ok()) << status.error_message();
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

TEST_P(XdsExtProcEnd2endTest, StreamDrainRequestOnRequestHeaders) {
  CreateAndStartBackends(1);
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(false)
                             .SetRequestHeaderMode()
                             .SetResponseHeaderMode()
                             .SetRequestBodyMode()
                             .SetResponseBodyMode()
                             .SetResponseTrailerMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  AsyncBidiStream stream;
  RpcOptions rpc_options;
  stream.Start(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(req.has_value());
  EXPECT_TRUE(req->has_request_headers());
  ext_proc_stream->SendResponse(
      MakeRequestHeadersMutationResponse({}, {}, /*request_drain=*/true));
  EchoRequest request;
  request.set_message(kMessage1);
  stream.StartWrite(request);
  EXPECT_TRUE(stream.WaitForWriteDone());
  EchoResponse response;
  EXPECT_TRUE(stream.ReadMessage(&response));
  EXPECT_EQ(response.message(), kMessage1);
  stream.StartWritesDone();
  Status status = stream.Finish();
  EXPECT_TRUE(status.ok()) << status.error_message();
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

TEST_P(XdsExtProcEnd2endTest, StreamDrainRequestOnResponseHeaders) {
  CreateAndStartBackends(1);
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(false)
                             .SetRequestHeaderMode()
                             .SetResponseHeaderMode()
                             .SetRequestBodyMode()
                             .SetResponseBodyMode()
                             .SetResponseTrailerMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  AsyncBidiStream stream;
  RpcOptions rpc_options;
  stream.Start(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(req.has_value());
  EXPECT_TRUE(req->has_request_headers());
  ext_proc_stream->SendResponse(MakeRequestHeadersMutationResponse({}));
  EchoRequest request;
  request.set_message(kMessage1);
  stream.StartWrite(request);
  auto next_req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(next_req.has_value());
  EXPECT_TRUE(next_req->has_request_body());
  const auto& body_req = next_req->request_body();
  grpc::testing::EchoRequest echo_request;
  std::string mutated_body = body_req.body();
  if (echo_request.ParseFromString(body_req.body())) {
    echo_request.set_message(absl::StrCat(echo_request.message(), "_modified"));
    GRPC_CHECK(echo_request.SerializeToString(&mutated_body));
  }
  ext_proc_stream->SendResponse(
      MakeRequestBodyMutationResponse(mutated_body, false));
  EXPECT_TRUE(stream.WaitForWriteDone());
  auto resp_headers_req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(resp_headers_req.has_value());
  EXPECT_TRUE(resp_headers_req->has_response_headers());
  ext_proc_stream->SendResponse(
      MakeResponseHeadersMutationResponse({}, {}, /*request_drain=*/true));
  EchoResponse response;
  EXPECT_TRUE(stream.ReadMessage(&response));
  EXPECT_EQ(response.message(), "message1_modified");
  stream.StartWritesDone();
  Status status = stream.Finish();
  EXPECT_TRUE(status.ok()) << status.error_message();
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

TEST_P(XdsExtProcEnd2endTest, StreamDrainRequestOnResponseTrailers) {
  CreateAndStartBackends(1);
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(false)
                             .SetRequestHeaderMode()
                             .SetResponseHeaderMode()
                             .SetRequestBodyMode()
                             .SetResponseBodyMode()
                             .SetResponseTrailerMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  AsyncBidiStream stream;
  RpcOptions rpc_options;
  stream.Start(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(req.has_value());
  EXPECT_TRUE(req->has_request_headers());
  ext_proc_stream->SendResponse(MakeRequestHeadersMutationResponse({}));
  EchoRequest request;
  request.set_message(kMessage1);
  stream.StartWrite(request);
  auto next_req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(next_req.has_value());
  EXPECT_TRUE(next_req->has_request_body());
  const auto& body_req = next_req->request_body();
  grpc::testing::EchoRequest echo_request;
  std::string mutated_body = body_req.body();
  if (echo_request.ParseFromString(body_req.body())) {
    echo_request.set_message(absl::StrCat(echo_request.message(), "_modified"));
    GRPC_CHECK(echo_request.SerializeToString(&mutated_body));
  }
  ext_proc_stream->SendResponse(
      MakeRequestBodyMutationResponse(mutated_body, false));
  EXPECT_TRUE(stream.WaitForWriteDone());
  stream.StartReadMessage();
  auto resp_headers_req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(resp_headers_req.has_value());
  EXPECT_TRUE(resp_headers_req->has_response_headers());
  ext_proc_stream->SendResponse(MakeResponseHeadersMutationResponse({}));
  auto resp_body_req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(resp_body_req.has_value());
  EXPECT_TRUE(resp_body_req->has_response_body());
  const auto& resp_body = resp_body_req->response_body();
  grpc::testing::EchoResponse echo_response;
  std::string mutated_resp_body = resp_body.body();
  if (echo_response.ParseFromString(resp_body.body())) {
    echo_response.set_message(
        absl::StrCat(echo_response.message(), "_modified"));
    GRPC_CHECK(echo_response.SerializeToString(&mutated_resp_body));
  }
  ext_proc_stream->SendResponse(
      MakeResponseBodyMutationResponse(mutated_resp_body, false));
  EchoResponse response;
  EXPECT_TRUE(stream.WaitForReadDone(&response));
  EXPECT_EQ(response.message(), "message1_modified_modified");
  stream.StartWritesDone();
  while (true) {
    auto next_opt = ext_proc_stream->GetNextRequest();
    ASSERT_TRUE(next_opt.has_value());
    if (next_opt->has_request_body()) {
      ext_proc_stream->SendResponse(MakeRequestBodyMutationResponse(
          next_opt->request_body().body(),
          next_opt->request_body().end_of_stream()));
    } else if (next_opt->has_response_trailers()) {
      ext_proc_stream->SendResponse(
          MakeResponseTrailersMutationResponse({}, {}, /*request_drain=*/true));
      break;
    } else {
      FAIL() << "Unexpected request type: " << next_opt->DebugString();
    }
  }
  Status status = stream.Finish();
  EXPECT_TRUE(status.ok()) << status.error_message();
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

TEST_P(XdsExtProcEnd2endTest, StreamDrainRequestOnServerBody) {
  CreateAndStartBackends(1);
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(false)
                             .SetRequestHeaderMode()
                             .SetResponseHeaderMode()
                             .SetResponseBodyMode()
                             .SetResponseTrailerMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  AsyncBidiStream stream;
  RpcOptions rpc_options;
  stream.Start(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(req.has_value());
  EXPECT_TRUE(req->has_request_headers());
  ext_proc_stream->SendResponse(MakeRequestHeadersMutationResponse({}));
  EchoRequest request;
  request.set_message(kMessage1);
  stream.StartWrite(request);
  EXPECT_TRUE(stream.WaitForWriteDone());
  stream.StartReadMessage();
  auto resp_headers_req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(resp_headers_req.has_value());
  EXPECT_TRUE(resp_headers_req->has_response_headers());
  ext_proc_stream->SendResponse(MakeResponseHeadersMutationResponse({}));
  auto resp_body_req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(resp_body_req.has_value());
  EXPECT_TRUE(resp_body_req->has_response_body());
  const auto& resp_body = resp_body_req->response_body();
  grpc::testing::EchoResponse echo_response;
  std::string mutated_resp_body = resp_body.body();
  if (echo_response.ParseFromString(resp_body.body())) {
    echo_response.set_message(
        absl::StrCat(echo_response.message(), "_modified"));
    GRPC_CHECK(echo_response.SerializeToString(&mutated_resp_body));
  }
  ext_proc_stream->SendResponse(MakeResponseBodyMutationResponse(
      mutated_resp_body, false, /*request_drain=*/true));
  EchoResponse response;
  EXPECT_TRUE(stream.WaitForReadDone(&response));
  EXPECT_EQ(response.message(), "message1_modified");
  request.set_message(kMessage2);
  stream.StartWrite(request);
  EXPECT_TRUE(stream.WaitForWriteDone());
  stream.StartReadMessage();
  EXPECT_TRUE(stream.WaitForReadDone(&response));
  EXPECT_EQ(response.message(), kMessage2);
  stream.StartWritesDone();
  Status status = stream.Finish();
  EXPECT_TRUE(status.ok()) << status.error_message();
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

//
// Ordering tests
//

TEST_P(XdsExtProcEnd2endTest,
       ClientToServerOrderingHeadersResponseWhenDisabled) {
  CreateAndStartBackends(1);
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(false)
                             .SetRequestBodyMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  AsyncRpc rpc;
  RpcOptions rpc_options;
  rpc.StartRpc(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(req.has_value());
  EXPECT_TRUE(req->has_request_body());
  ext_proc_stream->SendResponse(MakeRequestHeadersMutationResponse({}));
  Status status = rpc.GetStatus();
  EXPECT_THAT(status, GrpcStatusIs(StatusCode::INTERNAL,
                                   ::testing::HasSubstr(
                                       "Received request headers response but "
                                       "request headers are disabled")));
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

TEST_P(XdsExtProcEnd2endTest, ClientToServerOrderingResponseBodyBeforeHeaders) {
  CreateAndStartBackends(1);
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(true)
                             .SetRequestHeaderMode()
                             .SetRequestBodyMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  AsyncRpc rpc;
  RpcOptions rpc_options;
  rpc.StartRpc(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(req.has_value());
  EXPECT_TRUE(req->has_request_headers());
  ext_proc_stream->SendResponse(MakeRequestBodyMutationResponse(""));
  Status status = rpc.GetStatus();
  EXPECT_THAT(status, GrpcStatusIs(StatusCode::INTERNAL,
                                   ::testing::HasSubstr(
                                       "Received request body response before "
                                       "request headers response")));
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

TEST_P(XdsExtProcEnd2endTest,
       ServerToClientOrderingHeadersResponseWhenDisabled) {
  CreateAndStartBackends(1);
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(false)
                             .SetRequestHeaderMode()
                             .SetRequestBodyMode()
                             .SetResponseBodyMode()
                             .SetResponseTrailerMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  AsyncRpc rpc;
  RpcOptions rpc_options;
  rpc.StartRpc(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(req.has_value());
  EXPECT_TRUE(req->has_request_headers());
  ext_proc_stream->SendResponse(MakeRequestHeadersMutationResponse({}));
  auto next_req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(next_req.has_value());
  EXPECT_TRUE(next_req->has_request_body());
  ext_proc_stream->SendResponse(MakeRequestBodyMutationResponse(
      next_req->request_body().body(),
      next_req->request_body().end_of_stream()));
  auto resp_body_req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(resp_body_req.has_value());
  if (resp_body_req->has_request_body()) {
    ext_proc_stream->SendResponse(MakeRequestBodyMutationResponse(
        resp_body_req->request_body().body(),
        resp_body_req->request_body().end_of_stream()));
    resp_body_req = ext_proc_stream->GetNextRequest();
    ASSERT_TRUE(resp_body_req.has_value());
  }
  EXPECT_TRUE(resp_body_req->has_response_body());
  ext_proc_stream->SendResponse(MakeResponseHeadersMutationResponse({}));
  Status status = rpc.GetStatus();
  EXPECT_THAT(status, GrpcStatusIs(StatusCode::INTERNAL,
                                   ::testing::HasSubstr(
                                       "Received response headers response but "
                                       "response headers are disabled")));
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

TEST_P(XdsExtProcEnd2endTest, ServerToClientOrderingResponseBodyBeforeHeaders) {
  CreateAndStartBackends(1);
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(false)
                             .SetRequestHeaderMode()
                             .SetRequestBodyMode()
                             .SetResponseHeaderMode()
                             .SetResponseBodyMode()
                             .SetResponseTrailerMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  AsyncRpc rpc;
  RpcOptions rpc_options;
  rpc.StartRpc(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(req.has_value());
  EXPECT_TRUE(req->has_request_headers());
  ext_proc_stream->SendResponse(MakeRequestHeadersMutationResponse({}));
  auto next_req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(next_req.has_value());
  EXPECT_TRUE(next_req->has_request_body());
  ext_proc_stream->SendResponse(MakeRequestBodyMutationResponse(
      next_req->request_body().body(),
      next_req->request_body().end_of_stream()));
  auto resp_hdr_req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(resp_hdr_req.has_value());
  EXPECT_TRUE(resp_hdr_req->has_response_headers());
  ext_proc_stream->SendResponse(MakeResponseBodyMutationResponse(""));
  Status status = rpc.GetStatus();
  EXPECT_THAT(status, GrpcStatusIs(StatusCode::INTERNAL,
                                   ::testing::HasSubstr(
                                       "Received response body response before "
                                       "response headers response")));
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

TEST_P(XdsExtProcEnd2endTest, ServerToClientOrderingTrailersBeforeHeaders) {
  CreateAndStartBackends(1);
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(false)
                             .SetRequestHeaderMode()
                             .SetRequestBodyMode()
                             .SetResponseHeaderMode()
                             .SetResponseTrailerMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  AsyncRpc rpc;
  RpcOptions rpc_options;
  rpc.StartRpc(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(req.has_value());
  EXPECT_TRUE(req->has_request_headers());
  ext_proc_stream->SendResponse(MakeRequestHeadersMutationResponse({}));
  auto next_req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(next_req.has_value());
  EXPECT_TRUE(next_req->has_request_body());
  ext_proc_stream->SendResponse(MakeRequestBodyMutationResponse(
      next_req->request_body().body(),
      next_req->request_body().end_of_stream()));
  auto resp_hdr_req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(resp_hdr_req.has_value());
  EXPECT_TRUE(resp_hdr_req->has_response_headers());
  ext_proc_stream->SendResponse(MakeResponseTrailersMutationResponse({}));
  Status status = rpc.GetStatus();
  EXPECT_THAT(status,
              GrpcStatusIs(StatusCode::INTERNAL,
                           ::testing::HasSubstr(
                               "Received response trailers response before "
                               "response headers response")));
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

TEST_P(XdsExtProcEnd2endTest,
       ServerToClientOrderingTrailersBeforeResponseBody) {
  CreateAndStartBackends(1);
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(false)
                             .SetRequestHeaderMode()
                             .SetRequestBodyMode()
                             .SetResponseBodyMode()
                             .SetResponseHeaderMode()
                             .SetResponseTrailerMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  AsyncBidiStream stream;
  RpcOptions rpc_options;
  stream.Start(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(req.has_value());
  EXPECT_TRUE(req->has_request_headers());
  ext_proc_stream->SendResponse(MakeRequestHeadersMutationResponse({}));
  EchoRequest request;
  request.set_message(kMessage1);
  stream.StartWrite(request);
  auto next_req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(next_req.has_value());
  EXPECT_TRUE(next_req->has_request_body());
  ext_proc_stream->SendResponse(
      MakeRequestBodyMutationResponse(next_req->request_body().body(), false));
  EXPECT_TRUE(stream.WaitForWriteDone());
  stream.StartReadMessage();
  auto resp_hdr_req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(resp_hdr_req.has_value());
  EXPECT_TRUE(resp_hdr_req->has_response_headers());
  ext_proc_stream->SendResponse(MakeResponseHeadersMutationResponse({}));
  auto resp_body_req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(resp_body_req.has_value());
  EXPECT_TRUE(resp_body_req->has_response_body());
  ext_proc_stream->SendResponse(MakeResponseTrailersMutationResponse({}));
  EchoResponse response;
  EXPECT_FALSE(stream.WaitForReadDone(&response));
  Status status = stream.Finish();
  EXPECT_THAT(status,
              GrpcStatusIs(StatusCode::INTERNAL,
                           ::testing::HasSubstr(
                               "Received response trailers response before "
                               "all outstanding response body responses "
                               "were received")));
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

TEST_P(XdsExtProcEnd2endTest,
       ServerToClientOrderingTrailersResponseWhenDisabled) {
  CreateAndStartBackends(1);
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(false)
                             .SetRequestHeaderMode()
                             .SetRequestBodyMode()
                             .SetResponseHeaderMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  AsyncRpc rpc;
  RpcOptions rpc_options;
  rpc.StartRpc(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(req.has_value());
  EXPECT_TRUE(req->has_request_headers());
  ext_proc_stream->SendResponse(MakeRequestHeadersMutationResponse({}));
  auto next_req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(next_req.has_value());
  EXPECT_TRUE(next_req->has_request_body());
  ext_proc_stream->SendResponse(MakeRequestBodyMutationResponse(
      next_req->request_body().body(),
      next_req->request_body().end_of_stream()));
  auto resp_hdr_req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(resp_hdr_req.has_value());
  EXPECT_TRUE(resp_hdr_req->has_response_headers());
  ext_proc_stream->SendResponse(MakeResponseTrailersMutationResponse({}));
  Status status = rpc.GetStatus();
  EXPECT_THAT(status,
              GrpcStatusIs(StatusCode::INTERNAL,
                           ::testing::HasSubstr(
                               "Received response trailers response but "
                               "response trailers are disabled")));
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

TEST_P(XdsExtProcEnd2endTest, ServerToClientResponseBodyHalfClose) {
  CreateAndStartBackends(1);
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(true)
                             .SetResponseHeaderMode()
                             .SetResponseTrailerMode()
                             .SetResponseBodyMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  AsyncBidiStream stream;
  RpcOptions rpc_options;
  stream.Start(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  EchoRequest request;
  request.set_message(kRequestMessage);
  stream.StartWrite(request);
  EXPECT_TRUE(stream.WaitForWriteDone());
  stream.StartReadMessage();
  auto resp_hdr_req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(resp_hdr_req.has_value());
  EXPECT_TRUE(resp_hdr_req->has_response_headers());
  ext_proc_stream->SendResponse(MakeResponseHeadersMutationResponse({}));
  auto resp_body_req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(resp_body_req.has_value());
  EXPECT_TRUE(resp_body_req->has_response_body());
  ext_proc_stream->SendResponse(
      MakeResponseBodyMutationResponse("", /*end_of_stream=*/true));
  EchoResponse response;
  EXPECT_FALSE(stream.WaitForReadDone(&response));
  Status status = stream.Finish();
  EXPECT_THAT(status,
              GrpcStatusIs(StatusCode::INTERNAL,
                           ::testing::HasSubstr(
                               "end_of_stream / end_of_stream_without_message "
                               "is not supported for response_body")));
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

//
// Stream Clean Close tests
//

TEST_P(XdsExtProcEnd2endTest, StreamCleanCloseRequestBodyFailureModeFalse) {
  CreateAndStartBackends(1);
  ResetStubWithUniqueArg();
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(false)
                             .SetRequestHeaderMode()
                             .SetResponseHeaderMode()
                             .SetRequestBodyMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  AsyncBidiStream stream;
  RpcOptions rpc_options;
  stream.Start(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(req.has_value());
  EXPECT_TRUE(req->has_request_headers());
  ext_proc_stream->SendResponseAndStatus(MakeRequestHeadersMutationResponse({}),
                                         absl::OkStatus());
  EchoRequest request;
  request.set_message(kMessage1);
  stream.StartWrite(request);
  (void)stream.WaitForWriteDone();
  EchoResponse response;
  EXPECT_FALSE(stream.ReadMessage(&response));
  Status status = stream.Finish();
  EXPECT_THAT(status, GrpcStatusIs(StatusCode::INTERNAL,
                                   ::testing::HasSubstr(
                                       "Stream closed cleanly without drain")));
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

TEST_P(XdsExtProcEnd2endTest, StreamCleanCloseRequestBodyFailureModeTrue) {
  CreateAndStartBackends(1);
  ResetStubWithUniqueArg();
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(true)
                             .SetRequestHeaderMode()
                             .SetResponseHeaderMode()
                             .SetRequestBodyMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  AsyncBidiStream stream;
  RpcOptions rpc_options;
  stream.Start(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(req.has_value());
  EXPECT_TRUE(req->has_request_headers());
  ext_proc_stream->SendResponseAndStatus(MakeRequestHeadersMutationResponse({}),
                                         absl::OkStatus());
  absl::SleepFor(absl::Milliseconds(100));
  EchoRequest request;
  request.set_message(kMessage1);
  stream.StartWrite(request);
  EXPECT_TRUE(stream.WaitForWriteDone());
  EchoResponse response;
  EXPECT_TRUE(stream.ReadMessage(&response));
  EXPECT_EQ(response.message(), kMessage1);
  stream.StartWritesDone();
  Status status = stream.Finish();
  EXPECT_TRUE(status.ok()) << status.error_message();
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

TEST_P(XdsExtProcEnd2endTest,
       StreamCleanCloseRequestBodyWithInFlightFailureModeFalse) {
  CreateAndStartBackends(1);
  ResetStubWithUniqueArg();
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(false)
                             .SetRequestHeaderMode()
                             .SetResponseHeaderMode()
                             .SetRequestBodyMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  AsyncBidiStream stream;
  RpcOptions rpc_options;
  stream.Start(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(req.has_value());
  EXPECT_TRUE(req->has_request_headers());
  ext_proc_stream->SendResponse(MakeRequestHeadersMutationResponse({}));
  EchoRequest request;
  request.set_message(kMessage1);
  stream.StartWrite(request);
  auto req2 = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(req2.has_value());
  EXPECT_TRUE(req2->has_request_body());
  ext_proc_stream->SendResponseAndStatus(
      MakeRequestBodyMutationResponse(req2->request_body().body()),
      absl::OkStatus());
  (void)stream.WaitForWriteDone();
  Status status = stream.Finish();
  EXPECT_THAT(status, GrpcStatusIs(StatusCode::INTERNAL,
                                   ::testing::HasSubstr(
                                       "Stream closed cleanly without drain")));
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

TEST_P(XdsExtProcEnd2endTest,
       StreamCleanCloseRequestBodyWithInFlightFailureModeTrue) {
  CreateAndStartBackends(1);
  ResetStubWithUniqueArg();
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(true)
                             .SetRequestHeaderMode()
                             .SetResponseHeaderMode()
                             .SetRequestBodyMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  AsyncBidiStream stream;
  RpcOptions rpc_options;
  stream.Start(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(req.has_value());
  EXPECT_TRUE(req->has_request_headers());
  ext_proc_stream->SendResponse(MakeRequestHeadersMutationResponse({}));
  EchoRequest request;
  request.set_message(kMessage1);
  stream.StartWrite(request);
  auto req2 = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(req2.has_value());
  EXPECT_TRUE(req2->has_request_body());
  ext_proc_stream->SendStatus(absl::OkStatus());
  (void)stream.WaitForWriteDone();
  Status status = stream.Finish();
  EXPECT_THAT(status, GrpcStatusIs(StatusCode::INTERNAL,
                                   ::testing::HasSubstr(
                                       "Stream closed cleanly without drain")));
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

TEST_P(XdsExtProcEnd2endTest, StreamCleanCloseRequestHeadersFailureModeFalse) {
  CreateAndStartBackends(1);
  ResetStubWithUniqueArg();
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetObservabilityMode(false)
                             .SetFailureModeAllow(false)
                             .SetRequestHeaderMode()
                             .SetResponseHeaderMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  AsyncBidiStream stream;
  RpcOptions rpc_options;
  stream.Start(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(req.has_value());
  EXPECT_TRUE(req->has_request_headers());
  ext_proc_stream->SendStatus(absl::OkStatus());
  EchoRequest request;
  request.set_message(kMessage1);
  stream.StartWrite(request);
  EXPECT_TRUE(stream.WaitForWriteDone());
  EchoResponse response;
  EXPECT_TRUE(stream.ReadMessage(&response));
  EXPECT_EQ(response.message(), kMessage1);
  stream.StartWritesDone();
  Status status = stream.Finish();
  EXPECT_TRUE(status.ok()) << status.error_message();
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

TEST_P(XdsExtProcEnd2endTest, StreamCleanCloseRequestHeadersFailureModeTrue) {
  CreateAndStartBackends(1);
  ResetStubWithUniqueArg();
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetObservabilityMode(false)
                             .SetFailureModeAllow(true)
                             .SetRequestHeaderMode()
                             .SetResponseHeaderMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  AsyncBidiStream stream;
  RpcOptions rpc_options;
  stream.Start(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(req.has_value());
  EXPECT_TRUE(req->has_request_headers());
  ext_proc_stream->SendStatus(absl::OkStatus());
  EchoRequest request;
  request.set_message(kMessage1);
  stream.StartWrite(request);
  EXPECT_TRUE(stream.WaitForWriteDone());
  EchoResponse response;
  EXPECT_TRUE(stream.ReadMessage(&response));
  EXPECT_EQ(response.message(), kMessage1);
  stream.StartWritesDone();
  Status status = stream.Finish();
  EXPECT_TRUE(status.ok()) << status.error_message();
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

TEST_P(XdsExtProcEnd2endTest, StreamCleanCloseResponseBodyFailureModeFalse) {
  CreateAndStartBackends(1);
  ResetStubWithUniqueArg();
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(false)
                             .SetRequestHeaderMode()
                             .SetResponseHeaderMode()
                             .SetResponseTrailerMode()
                             .SetResponseBodyMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  AsyncBidiStream stream;
  RpcOptions rpc_options;
  stream.Start(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(req.has_value());
  EXPECT_TRUE(req->has_request_headers());
  ext_proc_stream->SendResponse(MakeRequestHeadersMutationResponse({}));
  EchoRequest request;
  request.set_message(kMessage1);
  stream.StartWrite(request);
  (void)stream.WaitForWriteDone();
  auto req2 = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(req2.has_value());
  EXPECT_TRUE(req2->has_response_headers());
  ext_proc_stream->SendResponseAndStatus(
      MakeResponseHeadersMutationResponse({}), absl::OkStatus());
  (void)stream.WaitForWriteDone();
  EchoResponse response;
  EXPECT_FALSE(stream.ReadMessage(&response));
  Status status = stream.Finish();
  EXPECT_THAT(status, GrpcStatusIs(StatusCode::INTERNAL,
                                   ::testing::HasSubstr(
                                       "Stream closed cleanly without drain")));
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

TEST_P(XdsExtProcEnd2endTest, StreamCleanCloseResponseBodyFailureModeTrue) {
  auto custom_backend_service = std::make_shared<CustomBidiStreamServiceImpl>();
  auto custom_backend_server =
      std::make_unique<CustomBackendServerThread>(this, custom_backend_service);
  ResetStubWithUniqueArg();
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(true)
                             .SetRequestHeaderMode()
                             .SetResponseHeaderMode()
                             .SetResponseTrailerMode()
                             .SetResponseBodyMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config,
                                   custom_backend_server->port());
  custom_backend_server->Start();
  if (GetParam().filter_on_server()) {
    EXPECT_THAT(custom_backend_server->GetNextStatus(),
                ::testing::Optional(absl::OkStatus()));
  }
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", {EdsResourceArgs::Endpoint(custom_backend_server->port())}},
  })));
  AsyncBidiStream stream;
  RpcOptions rpc_options;
  stream.Start(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(req.has_value());
  EXPECT_TRUE(req->has_request_headers());
  ext_proc_stream->SendResponse(MakeRequestHeadersMutationResponse({}));
  EchoRequest request;
  request.set_message(kMessage1);
  stream.StartWrite(request);
  EXPECT_TRUE(stream.WaitForWriteDone());
  auto req2 = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(req2.has_value());
  EXPECT_TRUE(req2->has_response_headers());
  ext_proc_stream->SendResponseAndStatus(
      MakeResponseHeadersMutationResponse({}), absl::OkStatus());
  absl::SleepFor(absl::Milliseconds(100));
  request.set_message(kMessage2);
  stream.StartWrite(request);
  EXPECT_TRUE(stream.WaitForWriteDone());
  EchoResponse response;
  EXPECT_TRUE(stream.ReadMessage(&response));
  EXPECT_EQ(response.message(), kMessage1);
  stream.StartWritesDone();
  Status status = stream.Finish();
  EXPECT_TRUE(status.ok()) << status.error_message();
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
  custom_backend_server->Shutdown();
}

TEST_P(XdsExtProcEnd2endTest,
       StreamCleanCloseResponseBodyWithInFlightFailureModeFalse) {
  CreateAndStartBackends(1);
  ResetStubWithUniqueArg();
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(false)
                             .SetResponseHeaderMode()
                             .SetResponseTrailerMode()
                             .SetResponseBodyMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  AsyncBidiStream stream;
  RpcOptions rpc_options;
  stream.Start(stub_.get(), rpc_options);
  EchoRequest request;
  request.set_message(kMessage1);
  stream.StartWrite(request);
  EXPECT_TRUE(stream.WaitForWriteDone());
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(req.has_value());
  EXPECT_TRUE(req->has_response_headers());
  ext_proc_stream->SendResponse(MakeResponseHeadersMutationResponse({}));
  stream.StartReadMessage();
  auto req2 = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(req2.has_value());
  EXPECT_TRUE(req2->has_response_body());
  ext_proc_stream->SendResponse(
      MakeResponseBodyMutationResponse(req2->response_body().body()));
  EchoResponse response;
  EXPECT_TRUE(stream.WaitForReadDone(&response));
  EXPECT_EQ(response.message(), kMessage1);
  stream.StartReadMessage();
  request.set_message(kMessage2);
  stream.StartWrite(request);
  (void)stream.WaitForWriteDone();
  auto req3 = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(req3.has_value());
  EXPECT_TRUE(req3->has_response_body());
  ext_proc_stream->SendStatus(absl::OkStatus());
  stream.StartWritesDone();
  Status status = stream.Finish();
  EXPECT_THAT(status, GrpcStatusIs(StatusCode::INTERNAL,
                                   ::testing::HasSubstr(
                                       "Stream closed cleanly without drain")));
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

TEST_P(XdsExtProcEnd2endTest,
       StreamCleanCloseResponseBodyWithInFlightFailureModeTrue) {
  CreateAndStartBackends(1);
  ResetStubWithUniqueArg();
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(true)
                             .SetResponseHeaderMode()
                             .SetResponseTrailerMode()
                             .SetResponseBodyMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  AsyncBidiStream stream;
  RpcOptions rpc_options;
  stream.Start(stub_.get(), rpc_options);
  EchoRequest request;
  request.set_message(kMessage1);
  stream.StartWrite(request);
  EXPECT_TRUE(stream.WaitForWriteDone());
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(req.has_value());
  EXPECT_TRUE(req->has_response_headers());
  ext_proc_stream->SendResponse(MakeResponseHeadersMutationResponse({}));
  stream.StartReadMessage();
  auto req2 = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(req2.has_value());
  EXPECT_TRUE(req2->has_response_body());
  ext_proc_stream->SendResponse(
      MakeResponseBodyMutationResponse(req2->response_body().body()));
  EchoResponse response;
  EXPECT_TRUE(stream.WaitForReadDone(&response));
  EXPECT_EQ(response.message(), kMessage1);
  stream.StartReadMessage();
  request.set_message(kMessage2);
  stream.StartWrite(request);
  (void)stream.WaitForWriteDone();
  auto req3 = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(req3.has_value());
  EXPECT_TRUE(req3->has_response_body());
  ext_proc_stream->SendStatus(absl::OkStatus());
  stream.StartWritesDone();
  Status status = stream.Finish();
  EXPECT_THAT(status, GrpcStatusIs(StatusCode::INTERNAL,
                                   ::testing::HasSubstr(
                                       "Stream closed cleanly without drain")));
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

TEST_P(XdsExtProcEnd2endTest, StreamCleanCloseResponseHeadersFailureModeFalse) {
  CreateAndStartBackends(1);
  ResetStubWithUniqueArg();
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(false)
                             .SetRequestHeaderMode()
                             .SetResponseHeaderMode()
                             .SetResponseTrailerMode()
                             .SetResponseBodyMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  AsyncBidiStream stream;
  RpcOptions rpc_options;
  stream.Start(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(req.has_value());
  EXPECT_TRUE(req->has_request_headers());
  ext_proc_stream->SendResponseAndStatus(MakeRequestHeadersMutationResponse({}),
                                         absl::OkStatus());
  EchoRequest request;
  request.set_message(kMessage1);
  stream.StartWrite(request);
  (void)stream.WaitForWriteDone();
  EchoResponse response;
  EXPECT_FALSE(stream.ReadMessage(&response));
  Status status = stream.Finish();
  EXPECT_THAT(status, GrpcStatusIs(StatusCode::INTERNAL,
                                   ::testing::HasSubstr(
                                       "Stream closed cleanly without drain")));
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

TEST_P(XdsExtProcEnd2endTest, StreamCleanCloseResponseHeadersFailureModeTrue) {
  CreateAndStartBackends(1);
  ResetStubWithUniqueArg();
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(true)
                             .SetRequestHeaderMode()
                             .SetResponseHeaderMode()
                             .SetResponseTrailerMode()
                             .SetResponseBodyMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  AsyncBidiStream stream;
  RpcOptions rpc_options;
  stream.Start(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(req.has_value());
  EXPECT_TRUE(req->has_request_headers());
  ext_proc_stream->SendResponseAndStatus(MakeRequestHeadersMutationResponse({}),
                                         absl::OkStatus());
  absl::SleepFor(absl::Milliseconds(100));
  EchoRequest request;
  request.set_message(kMessage1);
  stream.StartWrite(request);
  EXPECT_TRUE(stream.WaitForWriteDone());
  EchoResponse response;
  EXPECT_TRUE(stream.ReadMessage(&response));
  EXPECT_EQ(response.message(), kMessage1);
  stream.StartWritesDone();
  Status status = stream.Finish();
  EXPECT_TRUE(status.ok()) << status.error_message();
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

TEST_P(XdsExtProcEnd2endTest,
       StreamCleanCloseResponseTrailersFailureModeFalse) {
  CreateAndStartBackends(1);
  ResetStubWithUniqueArg();
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(false)
                             .SetObservabilityMode(false)
                             .SetResponseTrailerMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get());
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(req.has_value());
  EXPECT_TRUE(req->has_response_trailers());
  ext_proc_stream->SendStatus(absl::OkStatus());
  Status status = rpc.GetStatus();
  EXPECT_TRUE(status.ok()) << status.error_message();
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

TEST_P(XdsExtProcEnd2endTest, StreamCleanCloseResponseTrailersFailureModeTrue) {
  CreateAndStartBackends(1);
  ResetStubWithUniqueArg();
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(true)
                             .SetObservabilityMode(false)
                             .SetResponseTrailerMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get());
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(req.has_value());
  EXPECT_TRUE(req->has_response_trailers());
  ext_proc_stream->SendStatus(absl::OkStatus());
  Status status = rpc.GetStatus();
  EXPECT_TRUE(status.ok()) << status.error_message();
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

TEST_P(XdsExtProcEnd2endTest, StreamCleanCloseRequestBodyObservability) {
  CreateAndStartBackends(1);
  ResetStubWithUniqueArg();
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetObservabilityMode(true)
                             .SetFailureModeAllow(false)
                             .SetRequestHeaderMode()
                             .SetResponseHeaderMode()
                             .SetRequestBodyMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  AsyncBidiStream stream;
  RpcOptions rpc_options;
  stream.Start(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(req.has_value());
  EXPECT_TRUE(req->has_request_headers());
  ext_proc_stream->SendResponseAndStatus(MakeRequestHeadersMutationResponse({}),
                                         absl::OkStatus());
  EchoRequest request;
  request.set_message(kMessage1);
  stream.StartWrite(request);
  EXPECT_TRUE(stream.WaitForWriteDone());
  EchoResponse response;
  EXPECT_TRUE(stream.ReadMessage(&response));
  EXPECT_EQ(response.message(), kMessage1);
  stream.StartWritesDone();
  Status status = stream.Finish();
  EXPECT_TRUE(status.ok()) << status.error_message();
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

TEST_P(XdsExtProcEnd2endTest,
       StreamCleanCloseRequestBodyWithInFlightObservability) {
  CreateAndStartBackends(1);
  ResetStubWithUniqueArg();
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetObservabilityMode(true)
                             .SetFailureModeAllow(false)
                             .SetRequestHeaderMode()
                             .SetResponseHeaderMode()
                             .SetRequestBodyMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  AsyncBidiStream stream;
  RpcOptions rpc_options;
  stream.Start(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(req.has_value());
  EXPECT_TRUE(req->has_request_headers());
  ext_proc_stream->SendResponse(MakeRequestHeadersMutationResponse({}));
  EchoRequest request;
  request.set_message(kMessage1);
  stream.StartWrite(request);
  EXPECT_TRUE(stream.WaitForWriteDone());
  auto req2 = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(req2.has_value());
  EXPECT_TRUE(req2->has_request_body());
  ext_proc_stream->SendResponseAndStatus(
      MakeRequestBodyMutationResponse(req2->request_body().body()),
      absl::OkStatus());
  EchoResponse response;
  EXPECT_TRUE(stream.ReadMessage(&response));
  EXPECT_EQ(response.message(), kMessage1);
  request.set_message(kMessage2);
  stream.StartWrite(request);
  EXPECT_TRUE(stream.WaitForWriteDone());
  EXPECT_TRUE(stream.ReadMessage(&response));
  EXPECT_EQ(response.message(), kMessage2);
  stream.StartWritesDone();
  Status status = stream.Finish();
  EXPECT_TRUE(status.ok()) << status.error_message();
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

TEST_P(XdsExtProcEnd2endTest, StreamCleanCloseRequestHeadersObservability) {
  CreateAndStartBackends(1);
  ResetStubWithUniqueArg();
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetObservabilityMode(true)
                             .SetFailureModeAllow(false)
                             .SetRequestHeaderMode()
                             .SetResponseHeaderMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  AsyncBidiStream stream;
  RpcOptions rpc_options;
  stream.Start(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  ext_proc_stream->SendStatus(absl::OkStatus());
  EchoRequest request;
  request.set_message(kMessage1);
  stream.StartWrite(request);
  EXPECT_TRUE(stream.WaitForWriteDone());
  EchoResponse response;
  EXPECT_TRUE(stream.ReadMessage(&response));
  EXPECT_EQ(response.message(), kMessage1);
  stream.StartWritesDone();
  Status status = stream.Finish();
  EXPECT_TRUE(status.ok()) << status.error_message();
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

TEST_P(XdsExtProcEnd2endTest, StreamCleanCloseResponseBodyObservability) {
  CreateAndStartBackends(1);
  ResetStubWithUniqueArg();
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetObservabilityMode(true)
                             .SetFailureModeAllow(false)
                             .SetRequestHeaderMode()
                             .SetResponseHeaderMode()
                             .SetResponseTrailerMode()
                             .SetResponseBodyMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  AsyncBidiStream stream;
  RpcOptions rpc_options;
  stream.Start(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(req.has_value());
  EXPECT_TRUE(req->has_request_headers());
  ext_proc_stream->SendResponse(MakeRequestHeadersMutationResponse({}));
  EchoRequest request;
  request.set_message(kMessage1);
  stream.StartWrite(request);
  EXPECT_TRUE(stream.WaitForWriteDone());
  stream.StartReadMessage();
  auto req2 = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(req2.has_value());
  EXPECT_TRUE(req2->has_response_headers());
  ext_proc_stream->SendResponseAndStatus(
      MakeResponseHeadersMutationResponse({}), absl::OkStatus());
  EchoResponse response;
  EXPECT_TRUE(stream.WaitForReadDone(&response));
  EXPECT_EQ(response.message(), kMessage1);
  stream.StartWritesDone();
  Status status = stream.Finish();
  EXPECT_TRUE(status.ok()) << status.error_message();
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

TEST_P(XdsExtProcEnd2endTest,
       StreamCleanCloseResponseBodyWithInFlightObservability) {
  CreateAndStartBackends(1);
  ResetStubWithUniqueArg();
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetObservabilityMode(true)
                             .SetFailureModeAllow(false)
                             .SetResponseHeaderMode()
                             .SetResponseTrailerMode()
                             .SetResponseBodyMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  AsyncBidiStream stream;
  RpcOptions rpc_options;
  stream.Start(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  EchoRequest request;
  request.set_message(kMessage1);
  stream.StartWrite(request);
  EXPECT_TRUE(stream.WaitForWriteDone());
  stream.StartReadMessage();
  auto resp_headers_req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(resp_headers_req.has_value());
  EXPECT_TRUE(resp_headers_req->has_response_headers());
  ext_proc_stream->SendResponse(MakeResponseHeadersMutationResponse({}));
  auto resp_body_req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(resp_body_req.has_value());
  EXPECT_TRUE(resp_body_req->has_response_body());
  ext_proc_stream->SendResponse(MakeResponseBodyMutationResponse(
      resp_body_req->response_body().body(),
      resp_body_req->response_body().end_of_stream()));
  EchoResponse response;
  EXPECT_TRUE(stream.WaitForReadDone(&response));
  EXPECT_EQ(response.message(), kMessage1);
  request.set_message(kMessage2);
  stream.StartWrite(request);
  EXPECT_TRUE(stream.WaitForWriteDone());
  stream.StartReadMessage();
  auto resp_body_req2 = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(resp_body_req2.has_value());
  EXPECT_TRUE(resp_body_req2->has_response_body());
  ext_proc_stream->SendStatus(absl::OkStatus());
  EXPECT_TRUE(stream.WaitForReadDone(&response));
  EXPECT_EQ(response.message(), kMessage2);
  request.set_message("message3");
  stream.StartWrite(request);
  EXPECT_TRUE(stream.WaitForWriteDone());
  EXPECT_TRUE(stream.ReadMessage(&response));
  EXPECT_EQ(response.message(), "message3");
  stream.StartWritesDone();
  Status status = stream.Finish();
  EXPECT_TRUE(status.ok()) << status.error_message();
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

TEST_P(XdsExtProcEnd2endTest, StreamCleanCloseResponseHeadersObservability) {
  CreateAndStartBackends(1);
  ResetStubWithUniqueArg();
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetObservabilityMode(true)
                             .SetFailureModeAllow(false)
                             .SetRequestHeaderMode()
                             .SetResponseHeaderMode()
                             .SetResponseTrailerMode()
                             .SetResponseBodyMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  AsyncBidiStream stream;
  RpcOptions rpc_options;
  stream.Start(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(req.has_value());
  EXPECT_TRUE(req->has_request_headers());
  ext_proc_stream->SendResponseAndStatus(MakeRequestHeadersMutationResponse({}),
                                         absl::OkStatus());
  EchoRequest request;
  request.set_message(kMessage1);
  stream.StartWrite(request);
  EXPECT_TRUE(stream.WaitForWriteDone());
  EchoResponse response;
  EXPECT_TRUE(stream.ReadMessage(&response));
  EXPECT_EQ(response.message(), kMessage1);
  stream.StartWritesDone();
  Status status = stream.Finish();
  EXPECT_TRUE(status.ok()) << status.error_message();
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

TEST_P(XdsExtProcEnd2endTest, StreamCleanCloseResponseTrailersObservability) {
  CreateAndStartBackends(1);
  ResetStubWithUniqueArg();
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(false)
                             .SetObservabilityMode(true)
                             .SetResponseTrailerMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  AsyncBidiStream stream;
  RpcOptions rpc_options;
  stream.Start(stub_.get(), rpc_options);
  EchoRequest request;
  request.set_message(kMessage1);
  stream.StartWrite(request);
  EXPECT_TRUE(stream.WaitForWriteDone());
  EchoResponse response;
  EXPECT_TRUE(stream.ReadMessage(&response));
  EXPECT_EQ(response.message(), kMessage1);
  stream.StartWritesDone();
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(req.has_value());
  EXPECT_TRUE(req->has_response_trailers());
  ext_proc_stream->SendResponseAndStatus(
      MakeResponseTrailersMutationResponse({}), absl::OkStatus());
  Status status = stream.Finish();
  EXPECT_TRUE(status.ok()) << status.error_message();
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

//
// ExtProcClientMetrics
//

TEST_P(XdsExtProcEnd2endTest, ExtProcClientHeadersDurationMetric) {
  auto stats_plugin = grpc_core::FakeStatsPluginBuilder()
                          .UseDisabledByDefaultMetrics(true)
                          .BuildAndRegister();
  ResetStubWithUniqueArg();
  CreateAndStartBackends(1);
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(false)
                             .SetRequestHeaderMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  RpcOptions rpc_options;
  rpc_options.set_echo_metadata_initially(true);
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(req.has_value());
  EXPECT_TRUE(req->has_request_headers());
  ext_proc_stream->SendResponse(MakeRequestHeadersMutationResponse({}));
  Status status = rpc.GetStatus();
  EXPECT_TRUE(status.ok()) << status.error_message();
  const std::string expected_target = absl::StrCat("xds:", kServerName);
  const std::string metric_name =
      GetParam().filter_on_server()
          ? "grpc.server_ext_proc.client_headers_duration"
          : "grpc.client_ext_proc.client_headers_duration";
  const std::vector<absl::string_view> labels =
      GetParam().filter_on_server()
          ? std::vector<absl::string_view>{}
          : std::vector<absl::string_view>{expected_target};
  auto get_histogram = [&](absl::string_view name) {
    auto deadline =
        absl::Now() + absl::Seconds(10) * grpc_test_slowdown_factor();
    while (absl::Now() < deadline) {
      auto val = stats_plugin->GetHistogramValueByName(name, labels);
      if (val.has_value()) return val;
      absl::SleepFor(absl::Milliseconds(20));
    }
    return stats_plugin->GetHistogramValueByName(name, labels);
  };
  EXPECT_TRUE(get_histogram(metric_name).has_value());
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

TEST_P(XdsExtProcEnd2endTest, ExtProcClientHalfCloseDurationMetric) {
  auto stats_plugin = grpc_core::FakeStatsPluginBuilder()
                          .UseDisabledByDefaultMetrics(true)
                          .BuildAndRegister();
  ResetStubWithUniqueArg();
  CreateAndStartBackends(1);
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(false)
                             .SetRequestHeaderMode()
                             .SetResponseHeaderMode()
                             .SetResponseTrailerMode()
                             .SetRequestBodyMode()
                             .SetResponseBodyMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  RpcOptions rpc_options;
  rpc_options.set_echo_metadata_initially(true);
  rpc_options.set_echo_metadata(true);
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  while (true) {
    auto req = ext_proc_stream->GetNextRequest();
    ASSERT_TRUE(req.has_value());
    if (req->has_request_headers()) {
      ext_proc_stream->SendResponse(MakeRequestHeadersMutationResponse({}));
    } else if (req->has_request_body()) {
      ext_proc_stream->SendResponse(MakeRequestBodyMutationResponse(
          req->request_body().body(), req->request_body().end_of_stream()));
    } else if (req->has_response_headers()) {
      ext_proc_stream->SendResponse(MakeResponseHeadersMutationResponse({}));
    } else if (req->has_response_body()) {
      ext_proc_stream->SendResponse(MakeResponseBodyMutationResponse(
          req->response_body().body(), req->response_body().end_of_stream()));
    } else if (req->has_response_trailers()) {
      ext_proc_stream->SendResponse(MakeResponseTrailersMutationResponse({}));
      break;
    } else {
      FAIL() << "Unexpected request type: " << req->DebugString();
    }
  }
  Status status = rpc.GetStatus();
  EXPECT_TRUE(status.ok()) << status.error_message();
  const std::string expected_target = absl::StrCat("xds:", kServerName);
  const std::string metric_name =
      GetParam().filter_on_server()
          ? "grpc.server_ext_proc.client_half_close_duration"
          : "grpc.client_ext_proc.client_half_close_duration";
  const std::vector<absl::string_view> labels =
      GetParam().filter_on_server()
          ? std::vector<absl::string_view>{}
          : std::vector<absl::string_view>{expected_target};
  auto get_histogram = [&](absl::string_view name) {
    auto deadline =
        absl::Now() + absl::Seconds(10) * grpc_test_slowdown_factor();
    while (absl::Now() < deadline) {
      auto val = stats_plugin->GetHistogramValueByName(name, labels);
      if (val.has_value()) return val;
      absl::SleepFor(absl::Milliseconds(20));
    }
    return stats_plugin->GetHistogramValueByName(name, labels);
  };
  EXPECT_TRUE(get_histogram(metric_name).has_value());
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

TEST_P(XdsExtProcEnd2endTest, ExtProcServerHeadersDurationMetric) {
  auto stats_plugin = grpc_core::FakeStatsPluginBuilder()
                          .UseDisabledByDefaultMetrics(true)
                          .BuildAndRegister();
  ResetStubWithUniqueArg();
  CreateAndStartBackends(1);
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(false)
                             .SetResponseHeaderMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  RpcOptions rpc_options;
  rpc_options.set_echo_metadata_initially(true);
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(req.has_value());
  EXPECT_TRUE(req->has_response_headers());
  ext_proc_stream->SendResponse(MakeResponseHeadersMutationResponse({}));
  Status status = rpc.GetStatus();
  EXPECT_TRUE(status.ok()) << status.error_message();
  const std::string expected_target = absl::StrCat("xds:", kServerName);
  const std::string metric_name =
      GetParam().filter_on_server()
          ? "grpc.server_ext_proc.server_headers_duration"
          : "grpc.client_ext_proc.server_headers_duration";
  const std::vector<absl::string_view> labels =
      GetParam().filter_on_server()
          ? std::vector<absl::string_view>{}
          : std::vector<absl::string_view>{expected_target};
  auto get_histogram = [&](absl::string_view name) {
    auto deadline =
        absl::Now() + absl::Seconds(10) * grpc_test_slowdown_factor();
    while (absl::Now() < deadline) {
      auto val = stats_plugin->GetHistogramValueByName(name, labels);
      if (val.has_value()) return val;
      absl::SleepFor(absl::Milliseconds(20));
    }
    return stats_plugin->GetHistogramValueByName(name, labels);
  };
  EXPECT_TRUE(get_histogram(metric_name).has_value());
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

TEST_P(XdsExtProcEnd2endTest, ExtProcServerTrailersDurationMetric) {
  auto stats_plugin = grpc_core::FakeStatsPluginBuilder()
                          .UseDisabledByDefaultMetrics(true)
                          .BuildAndRegister();
  ResetStubWithUniqueArg();
  CreateAndStartBackends(1);
  auto ext_proc_config = ExtProcFilterConfigBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(false)
                             .SetResponseTrailerMode()
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  RpcOptions rpc_options;
  rpc_options.set_echo_metadata(true);
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(req.has_value());
  EXPECT_TRUE(req->has_response_trailers());
  ext_proc_stream->SendResponse(MakeResponseTrailersMutationResponse({}));
  Status status = rpc.GetStatus();
  EXPECT_TRUE(status.ok()) << status.error_message();
  const std::string expected_target = absl::StrCat("xds:", kServerName);
  const std::string metric_name =
      GetParam().filter_on_server()
          ? "grpc.server_ext_proc.server_trailers_duration"
          : "grpc.client_ext_proc.server_trailers_duration";
  const std::vector<absl::string_view> labels =
      GetParam().filter_on_server()
          ? std::vector<absl::string_view>{}
          : std::vector<absl::string_view>{expected_target};
  auto get_histogram = [&](absl::string_view name) {
    auto deadline =
        absl::Now() + absl::Seconds(10) * grpc_test_slowdown_factor();
    while (absl::Now() < deadline) {
      auto val = stats_plugin->GetHistogramValueByName(name, labels);
      if (val.has_value()) return val;
      absl::SleepFor(absl::Milliseconds(20));
    }
    return stats_plugin->GetHistogramValueByName(name, labels);
  };
  EXPECT_TRUE(get_histogram(metric_name).has_value());
  EXPECT_EQ(ext_proc_service_->stream_count(), 1);
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  // Make the backup poller poll very frequently in order to pick up
  // updates from all the subchannels's FDs.
  grpc_core::ConfigVars::Overrides overrides;
  overrides.client_channel_backup_poll_interval_ms = 1;
  grpc_core::ConfigVars::SetOverrides(overrides);
  grpc_core::ForceEnableExperiment("v2_non_owning_waker_implementation", true);
  grpc_core::ForceEnableExperiment("recv_message_filter_bypass_fix", true);
  grpc_core::ForceEnableExperiment("xds_server_filter_chain_per_route", true);
  grpc_init();
  const auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}

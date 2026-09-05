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
#include "envoy/extensions/grpc_service/channel_credentials/insecure/v3/insecure_credentials.pb.h"
#include "envoy/service/ext_proc/v3/external_processor.grpc.pb.h"
#include "src/core/config/config_vars.h"
#include "src/core/lib/experiments/config.h"
#include "src/core/util/ref_counted.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/sync.h"
#include "test/core/test_util/fake_stats_plugin.h"
#include "test/core/test_util/scoped_env_var.h"
#include "test/core/test_util/test_config.h"
#include "test/cpp/end2end/xds/xds_end2end_test_lib.h"
#include "test/cpp/end2end/xds/xds_utils.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

namespace grpc {
namespace testing {
namespace {

using ::envoy::extensions::filters::http::ext_proc::v3::ExternalProcessor;
using ::envoy::extensions::filters::network::http_connection_manager::v3::
    HttpFilter;
using ::envoy::service::ext_proc::v3::ProcessingRequest;
using ::envoy::service::ext_proc::v3::ProcessingResponse;

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
constexpr char kMessage3[] = "message3";
constexpr char kMutatedSuffix[] = "-mutated";
constexpr char kMessage1Mutated[] = "message1-mutated";
constexpr char kMessage1DoubleMutated[] = "message1-mutated-mutated";

// A stream-based fake external processor service that provides fine-grained,
// sequential control over incoming ext_proc stream requests and outgoing
// responses/statuses for test assertions.
class FakeExtProcService final : public ::envoy::service::ext_proc::v3::
                                     ExternalProcessor::CallbackService {
 public:
  // Represents a single bidirectional stream between the client ext_proc filter
  // and this service, implemented as a ServerBidiReactor.
  class Stream final : public grpc::ServerBidiReactor<
                           ::envoy::service::ext_proc::v3::ProcessingRequest,
                           ::envoy::service::ext_proc::v3::ProcessingResponse>,
                       public grpc_core::RefCounted<Stream> {
   public:
    Stream() = default;

    // Returns the next request received from the client, or std::nullopt
    // if stream finished without receiving another request.
    std::optional<::envoy::service::ext_proc::v3::ProcessingRequest>
    GetNextRequest() {
      grpc_core::MutexLock lock(&mu_);
      while (requests_.empty() && !is_done_) {
        cv_.Wait(&mu_);
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
      response_ = std::move(response);
      write_in_flight_ = true;
      StartWrite(&response_);
      while (write_in_flight_ && !is_done_) {
        cv_.Wait(&mu_);
      }
    }

    // Closes the stream with the specified status.
    void SendStatus(const absl::Status& status) {
      grpc_core::MutexLock lock(&mu_);
      MaybeFinish(grpc::Status(static_cast<grpc::StatusCode>(status.code()),
                               std::string(status.message())));
      while (!is_done_) {
        cv_.Wait(&mu_);
      }
    }

   private:
    friend class FakeExtProcService;

    void StartCall() { StartRead(&request_); }

    void OnReadDone(bool ok) override {
      grpc_core::MutexLock lock(&mu_);
      if (ok) {
        requests_.push(std::move(request_));
        cv_.SignalAll();
        StartRead(&request_);
      } else {
        MaybeFinish(grpc::Status::OK);
      }
    }

    void OnWriteDone(bool /*ok*/) override {
      grpc_core::MutexLock lock(&mu_);
      write_in_flight_ = false;
      cv_.SignalAll();
    }

    void OnCancel() override { MaybeFinish(grpc::Status::CANCELLED); }

    void OnDone() override {
      {
        grpc_core::MutexLock lock(&mu_);
        is_done_ = true;
        cv_.SignalAll();
      }
      Unref();
    }

    void MaybeFinish(const grpc::Status& status) {
      bool expected = false;
      if (called_finish_.compare_exchange_strong(expected, true)) {
        Finish(status);
      }
    }

    grpc_core::Mutex mu_;
    grpc_core::CondVar cv_;
    std::queue<::envoy::service::ext_proc::v3::ProcessingRequest> requests_
        ABSL_GUARDED_BY(mu_);
    ::envoy::service::ext_proc::v3::ProcessingRequest request_;
    ::envoy::service::ext_proc::v3::ProcessingResponse response_
        ABSL_GUARDED_BY(mu_);
    bool write_in_flight_ ABSL_GUARDED_BY(mu_) = false;
    bool is_done_ ABSL_GUARDED_BY(mu_) = false;
    std::atomic<bool> called_finish_{false};
  };

  // Returns the incoming stream, or nullptr if the service is shutdown.
  grpc_core::RefCountedPtr<Stream> GetStream() {
    grpc_core::MutexLock lock(&mu_);
    while (stream_ == nullptr && !is_shutdown_) {
      cv_.Wait(&mu_);
    }
    return stream_;
  }

  void Shutdown() {
    grpc_core::RefCountedPtr<Stream> stream;
    {
      grpc_core::MutexLock lock(&mu_);
      is_shutdown_ = true;
      stream = std::move(stream_);
      cv_.SignalAll();
    }
    if (stream != nullptr) {
      stream->MaybeFinish(
          grpc::Status(grpc::StatusCode::UNAVAILABLE, "Server shutdown"));
    }
  }

  Stream* Process(grpc::CallbackServerContext* /*context*/) override {
    auto stream = grpc_core::MakeRefCounted<Stream>();
    {
      grpc_core::MutexLock lock(&mu_);
      if (is_shutdown_) {
        stream->MaybeFinish(
            grpc::Status(grpc::StatusCode::UNAVAILABLE, "Server shutdown"));
        return stream.release();
      }
      stream_ = stream;
      cv_.SignalAll();
    }
    stream->StartCall();
    return stream.release();
  }

 private:
  grpc_core::Mutex mu_;
  grpc_core::CondVar cv_;
  grpc_core::RefCountedPtr<Stream> stream_ ABSL_GUARDED_BY(mu_);
  bool is_shutdown_ ABSL_GUARDED_BY(mu_) = false;
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
      auto* google_grpc =
          ext_proc_.mutable_grpc_service()->mutable_google_grpc();
      google_grpc->add_channel_credentials_plugin()->PackFrom(
          envoy::extensions::grpc_service::channel_credentials::insecure::v3::
              InsecureCredentials());
    }

    ExtProcFilterConfigBuilder& SetTargetUri(const std::string& target_uri) {
      auto* google_grpc =
          ext_proc_.mutable_grpc_service()->mutable_google_grpc();
      google_grpc->set_target_uri(target_uri);
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
  //
  // TODO(rishesh): Remove this class and use the synchronous streaming API once
  // the v3 migration is finished. Currently, due to limitations of the
  // v3-to-v1 adaptor code, synchronous ClientReaderWriter::Write() blocks on
  // cq_.Pluck() waiting for the write to finish in the filter stack, but
  // ExtProcFilter cannot complete the write until FakeExtProcService responds.
  // Because responding to FakeExtProcService must be done by the test thread,
  // calling Write() synchronously on the same thread creates a deadlock.
  // Once the v3 migration is complete and the adaptor code is no longer used,
  // we will no longer have this limitation and can switch back to the
  // synchronous streaming API.
  class AsyncBidiStream final
      : public grpc::ClientBidiReactor<EchoRequest, EchoResponse> {
   public:
    AsyncBidiStream() = default;

    ~AsyncBidiStream() override {
      grpc_core::MutexLock lock(&mu_);
      while (!status_.has_value() && (write_state_ == OpState::kInFlight ||
                                      read_state_ == OpState::kInFlight)) {
        cv_.Wait(&mu_);
      }
    }

    void Start(grpc::testing::EchoTestService::Stub* stub,
               const RpcOptions& rpc_options = RpcOptions()) {
      rpc_options.SetupContext(&context_);
      stub->async()->BidiStream(&context_, this);
      StartCall();
    }

    void StartWrite(const EchoRequest& request) {
      grpc_core::MutexLock lock(&mu_);
      write_msg_ = request;
      if (status_.has_value() || write_state_ == OpState::kFailed ||
          read_state_ == OpState::kFailed) {
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
             write_state_ != OpState::kFailed) {
        if (cv_.WaitWithDeadline(&mu_, deadline)) {
          return false;
        }
      }
      return write_state_ == OpState::kSuccess;
    }

    void StartWritesDone() {
      grpc_core::MutexLock lock(&mu_);
      if (status_.has_value() || write_state_ == OpState::kFailed ||
          read_state_ == OpState::kFailed) {
        return;
      }
      ClientBidiReactor::StartWritesDone();
    }

    void StartReadMessage() {
      grpc_core::MutexLock lock(&mu_);
      read_msg_.Clear();
      if (status_.has_value() || write_state_ == OpState::kFailed ||
          read_state_ == OpState::kFailed) {
        read_state_ = OpState::kFailed;
        cv_.SignalAll();
        return;
      }
      read_state_ = OpState::kInFlight;
      StartRead(&read_msg_);
    }

    std::optional<EchoResponse> WaitForReadDone(
        absl::Duration timeout = absl::Seconds(10)) {
      grpc_core::MutexLock lock(&mu_);
      const absl::Time deadline =
          absl::Now() + timeout * grpc_test_slowdown_factor();
      while (read_state_ != OpState::kSuccess &&
             read_state_ != OpState::kFailed) {
        if (cv_.WaitWithDeadline(&mu_, deadline)) {
          return std::nullopt;
        }
      }
      if (read_state_ == OpState::kSuccess) {
        return read_msg_;
      }
      return std::nullopt;
    }

    std::optional<EchoResponse> ReadMessage(
        absl::Duration timeout = absl::Seconds(10)) {
      StartReadMessage();
      return WaitForReadDone(timeout);
    }

    std::optional<Status> WaitForStatus(
        absl::Duration timeout = absl::Seconds(10)) {
      grpc_core::MutexLock lock(&mu_);
      const absl::Time deadline =
          absl::Now() + timeout * grpc_test_slowdown_factor();
      while (!status_.has_value()) {
        if (cv_.WaitWithDeadline(&mu_, deadline)) {
          return std::nullopt;
        }
      }
      return *status_;
    }

    std::multimap<std::string, std::string> GetServerInitialMetadata() {
      std::multimap<std::string, std::string> map;
      for (const auto& [key, value] : context_.GetServerInitialMetadata()) {
        map.emplace(std::string(key.data(), key.size()),
                    std::string(value.data(), value.size()));
      }
      return map;
    }

    std::multimap<std::string, std::string> GetServerTrailingMetadata() {
      std::multimap<std::string, std::string> map;
      for (const auto& [key, value] : context_.GetServerTrailingMetadata()) {
        map.emplace(std::string(key.data(), key.size()),
                    std::string(value.data(), value.size()));
      }
      return map;
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

    void OnDone(const Status& status) override {
      grpc_core::MutexLock lock(&mu_);
      status_ = status;
      cv_.SignalAll();
    }

   private:
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
    explicit ExtProcServerThread(XdsEnd2endTest* test_obj)
        : ServerThread(test_obj, /*use_xds_enabled_server=*/false,
                       grpc::InsecureServerCredentials()),
          service_(std::make_shared<FakeExtProcService>()) {}

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

  static std::multimap<std::string, std::string> HeaderMapToMultimap(
      const envoy::config::core::v3::HeaderMap& header_map) {
    std::multimap<std::string, std::string> map;
    for (const auto& header : header_map.headers()) {
      std::string val =
          !header.raw_value().empty() ? header.raw_value() : header.value();
      map.emplace(header.key(), std::move(val));
    }
    return map;
  }

  static std::string GetExtProcAttribute(
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

  static void PopulateHeaderMutation(
      ::envoy::service::ext_proc::v3::HeaderMutation* mutation,
      const std::vector<std::pair<std::string, std::string>>& set_headers,
      const std::vector<std::string>& remove_headers = {}) {
    for (const auto& [key, value] : set_headers) {
      auto* header = mutation->add_set_headers();
      header->mutable_header()->set_key(key);
      header->mutable_header()->set_value(value);
    }
    for (const auto& key : remove_headers) {
      mutation->add_remove_headers(key);
    }
  }

  static ::envoy::service::ext_proc::v3::ProcessingResponse
  MakeRequestHeadersMutationResponse(
      const std::vector<std::pair<std::string, std::string>>& set_headers,
      const std::vector<std::string>& remove_headers = {},
      bool request_drain = false) {
    ::envoy::service::ext_proc::v3::ProcessingResponse response;
    if (request_drain) {
      response.set_request_drain(true);
    }
    PopulateHeaderMutation(response.mutable_request_headers()
                               ->mutable_response()
                               ->mutable_header_mutation(),
                           set_headers, remove_headers);
    return response;
  }

  static ::envoy::service::ext_proc::v3::ProcessingResponse
  MakeResponseHeadersMutationResponse(
      const std::vector<std::pair<std::string, std::string>>& set_headers,
      const std::vector<std::string>& remove_headers = {},
      bool request_drain = false) {
    ::envoy::service::ext_proc::v3::ProcessingResponse response;
    if (request_drain) {
      response.set_request_drain(true);
    }
    PopulateHeaderMutation(response.mutable_response_headers()
                               ->mutable_response()
                               ->mutable_header_mutation(),
                           set_headers, remove_headers);
    return response;
  }

  static void PopulateBodyMutation(
      ::envoy::service::ext_proc::v3::BodyMutation* body_mutation,
      absl::string_view body, bool end_of_stream = false) {
    body_mutation->mutable_streamed_response()->set_body(std::string(body));
    body_mutation->mutable_streamed_response()->set_end_of_stream(
        end_of_stream);
  }

  static ::envoy::service::ext_proc::v3::ProcessingResponse
  MakeRequestBodyMutationResponse(absl::string_view body,
                                  bool end_of_stream = false,
                                  bool request_drain = false) {
    ::envoy::service::ext_proc::v3::ProcessingResponse response;
    if (request_drain) {
      response.set_request_drain(true);
    }
    PopulateBodyMutation(response.mutable_request_body()
                             ->mutable_response()
                             ->mutable_body_mutation(),
                         body, end_of_stream);
    return response;
  }

  static ::envoy::service::ext_proc::v3::ProcessingResponse
  MakeResponseBodyMutationResponse(absl::string_view body,
                                   bool end_of_stream = false,
                                   bool request_drain = false) {
    ::envoy::service::ext_proc::v3::ProcessingResponse response;
    if (request_drain) {
      response.set_request_drain(true);
    }
    PopulateBodyMutation(response.mutable_response_body()
                             ->mutable_response()
                             ->mutable_body_mutation(),
                         body, end_of_stream);
    return response;
  }

  static ::envoy::service::ext_proc::v3::ProcessingResponse
  MakeResponseTrailersMutationResponse(
      const std::vector<std::pair<std::string, std::string>>& set_headers,
      const std::vector<std::string>& remove_headers = {},
      bool request_drain = false) {
    ::envoy::service::ext_proc::v3::ProcessingResponse response;
    if (request_drain) {
      response.set_request_drain(true);
    }
    PopulateHeaderMutation(
        response.mutable_response_trailers()->mutable_header_mutation(),
        set_headers, remove_headers);
    return response;
  }

  static ::envoy::service::ext_proc::v3::ProcessingResponse
  MakeImmediateResponse(grpc::StatusCode code, absl::string_view details = "",
                        const std::vector<std::pair<std::string, std::string>>&
                            set_headers = {}) {
    ::envoy::service::ext_proc::v3::ProcessingResponse response;
    auto* immediate = response.mutable_immediate_response();
    immediate->mutable_grpc_status()->set_status(code);
    if (!details.empty()) {
      immediate->set_details(std::string(details));
    }
    PopulateHeaderMutation(immediate->mutable_headers(), set_headers);
    return response;
  }

  static std::string ModifyEchoRequest(absl::string_view body,
                                       absl::string_view add_suffix) {
    EchoRequest echo_req;
    CHECK(echo_req.ParseFromString(body));
    echo_req.set_message(absl::StrCat(echo_req.message(), add_suffix));
    std::string mutated;
    CHECK(echo_req.SerializeToString(&mutated));
    return mutated;
  }

  static std::string ModifyEchoResponse(absl::string_view body,
                                        absl::string_view add_suffix) {
    EchoResponse echo_resp;
    CHECK(echo_resp.ParseFromString(body));
    echo_resp.set_message(absl::StrCat(echo_resp.message(), add_suffix));
    std::string mutated;
    CHECK(echo_resp.SerializeToString(&mutated));
    return mutated;
  }

  void SetUp() override {
    InitClient(MakeBootstrapBuilder().SetTrustedXdsServer(),
               /*lb_expected_authority=*/"",
               /*xds_resource_does_not_exist_timeout_ms=*/0,
               /*balancer_authority_override=*/"", /*args=*/nullptr);
    CreateAndStartBackends(1);
    balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
        {"locality0", CreateEndpointsForBackends(0, 1)},
    })));
    ext_proc_server_ = std::make_unique<ExtProcServerThread>(this);
    ext_proc_server_->Start();
    ext_proc_service_ = ext_proc_server_->ext_proc_service();
  }

  void TearDown() override {
    ext_proc_server_->Shutdown();
    XdsEnd2endTest::TearDown();
  }

  Listener BuildListenerWithExtProcFilter(const ExternalProcessor& ext_proc) {
    Listener listener = default_listener_;
    HttpConnectionManager hcm = ClientHcmAccessor().Unpack(listener);
    HttpFilter* filter0 = hcm.mutable_http_filters(0);
    *hcm.add_http_filters() = *filter0;
    filter0->set_name(kFilterInstanceName);
    filter0->mutable_typed_config()->PackFrom(ext_proc);
    ClientHcmAccessor().Pack(hcm, &listener);
    return listener;
  }

  ExtProcFilterConfigBuilder MakeFilterConfigBuilder() {
    return ExtProcFilterConfigBuilder().SetTargetUri(
        ext_proc_server_->target());
  }

  grpc_core::testing::ScopedExperimentalEnvVar env_var_{
      "GRPC_EXPERIMENTAL_XDS_EXT_PROC_ON_CLIENT"};
  FakeExtProcService* ext_proc_service_ = nullptr;
  std::unique_ptr<ExtProcServerThread> ext_proc_server_;
};

INSTANTIATE_TEST_SUITE_P(
    XdsTest, XdsExtProcEnd2endTest,
    ::testing::Values(XdsTestType(),
                      XdsTestType().set_filter_config_setup(
                          XdsTestType::kHttpFilterConfigInRoute)),
    &XdsTestType::Name);

MATCHER(IsOk, "") {
  if (!arg.ok()) {
    *result_listener << arg.error_message();
    return false;
  }
  return true;
}

MATCHER_P2(GrpcStatusIs, code, message_matcher, "") {
  return ::testing::ExplainMatchResult(code, arg.error_code(),
                                       result_listener) &&
         ::testing::ExplainMatchResult(message_matcher, arg.error_message(),
                                       result_listener);
}

MATCHER_P(MatchesRequestHeaders, headers_matcher,
          "matches request_headers with given headers") {
  if (!arg.has_request_headers()) {
    *result_listener << "request does not have request_headers";
    return false;
  }
  auto actual_headers = XdsExtProcEnd2endTest::HeaderMapToMultimap(
      arg.request_headers().headers());
  return ::testing::ExplainMatchResult(headers_matcher, actual_headers,
                                       result_listener);
}

MATCHER_P2(MatchesRequestBody, body_matcher, end_of_stream,
           "matches request_body with given body and end_of_stream") {
  if (!arg.has_request_body()) {
    *result_listener << "request does not have request_body";
    return false;
  }
  const auto& request_body = arg.request_body();
  if (request_body.end_of_stream() != end_of_stream) {
    *result_listener << "expected end_of_stream " << end_of_stream
                     << " but got " << request_body.end_of_stream();
    return false;
  }
  return ::testing::ExplainMatchResult(body_matcher, request_body.body(),
                                       result_listener);
}

MATCHER_P(MatchesResponseHeaders, headers_matcher,
          "matches response_headers with given headers") {
  if (!arg.has_response_headers()) {
    *result_listener << "request does not have response_headers";
    return false;
  }
  auto actual_headers = XdsExtProcEnd2endTest::HeaderMapToMultimap(
      arg.response_headers().headers());
  return ::testing::ExplainMatchResult(headers_matcher, actual_headers,
                                       result_listener);
}

MATCHER_P2(MatchesResponseBody, body_matcher, end_of_stream,
           "matches response_body with given body and end_of_stream") {
  if (!arg.has_response_body()) {
    *result_listener << "request does not have response_body";
    return false;
  }
  const auto& response_body = arg.response_body();
  if (response_body.end_of_stream() != end_of_stream) {
    *result_listener << "expected end_of_stream " << end_of_stream
                     << " but got " << response_body.end_of_stream();
    return false;
  }
  return ::testing::ExplainMatchResult(body_matcher, response_body.body(),
                                       result_listener);
}

MATCHER_P(MatchesResponseTrailers, trailers_matcher,
          "matches response_trailers with given trailers") {
  if (!arg.has_response_trailers()) {
    *result_listener << "request does not have response_trailers";
    return false;
  }
  const auto& response_trailers = arg.response_trailers();
  std::multimap<std::string, std::string> actual_trailers =
      XdsExtProcEnd2endTest::HeaderMapToMultimap(response_trailers.trailers());
  return ::testing::ExplainMatchResult(trailers_matcher, actual_trailers,
                                       result_listener);
}

MATCHER_P(EchoRequestMessageIs, message_matcher,
          "matches serialized EchoRequest with given message") {
  EchoRequest echo_req;
  if (!echo_req.ParseFromString(arg)) {
    *result_listener << "could not be parsed as EchoRequest";
    return false;
  }
  return ::testing::ExplainMatchResult(message_matcher, echo_req.message(),
                                       result_listener);
}

MATCHER_P(EchoResponseMessageIs, message_matcher,
          "matches serialized EchoResponse with given message") {
  EchoResponse echo_resp;
  if (!echo_resp.ParseFromString(arg)) {
    *result_listener << "could not be parsed as EchoResponse";
    return false;
  }
  return ::testing::ExplainMatchResult(message_matcher, echo_resp.message(),
                                       result_listener);
}

MATCHER_P(MatchesEchoResponse, message_matcher,
          "matches EchoResponse with given message") {
  return ::testing::ExplainMatchResult(message_matcher, arg.message(),
                                       result_listener);
}

//
// Tests
//

TEST_P(XdsExtProcEnd2endTest, ProcessingModeAllDisabledSuccess) {
  auto ext_proc_config = MakeFilterConfigBuilder().Build();
  SetListenerAndRouteConfiguration(
      balancer_.get(), BuildListenerWithExtProcFilter(ext_proc_config),
      default_route_config_);
  CheckRpcSendOk(DEBUG_LOCATION);
}

TEST_P(XdsExtProcEnd2endTest, ProcessingModeAllEnabledSuccess) {
  auto ext_proc_config = MakeFilterConfigBuilder()
                             .SetRequestHeaderMode()
                             .SetResponseHeaderMode()
                             .SetResponseTrailerMode()
                             .SetRequestBodyMode()
                             .SetResponseBodyMode()
                             .Build();
  SetListenerAndRouteConfiguration(
      balancer_.get(), BuildListenerWithExtProcFilter(ext_proc_config),
      default_route_config_);
  RpcOptions rpc_options;
  rpc_options.set_echo_metadata_initially(true);
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  // ext_proc server sees request headers and sends them back.
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_THAT(
      req,
      ::testing::Optional(MatchesRequestHeaders(::testing::Contains(
          ::testing::Pair(":path", "/grpc.testing.EchoTestService/Echo")))));
  ext_proc_stream->SendResponse(MakeRequestHeadersMutationResponse(
      {{kRequestHeadersMutatedHeaderKey, kHeaderMutatedValue}}));
  // ext_proc server sees client message and sends it back.
  req = ext_proc_stream->GetNextRequest();
  ASSERT_THAT(req, ::testing::Optional(
                       MatchesRequestBody(EchoRequestMessageIs(kRequestMessage),
                                          /*end_of_stream=*/false)));
  ext_proc_stream->SendResponse(MakeRequestBodyMutationResponse(
      ModifyEchoRequest(req->request_body().body(), kRequestBodyMutatedSuffix),
      /*end_of_stream=*/false));
  // Next two events may arrive in either order: client half-close and server
  // response headers.
  for (int i = 0; i < 2; ++i) {
    req = ext_proc_stream->GetNextRequest();
    ASSERT_TRUE(req.has_value());
    if (req->has_request_body()) {
      EXPECT_THAT(*req,
                  MatchesRequestBody(/*body=*/"", /*end_of_stream=*/true));
      ext_proc_stream->SendResponse(MakeRequestBodyMutationResponse(
          /*body=*/"", /*end_of_stream=*/true));
    } else if (req->has_response_headers()) {
      EXPECT_THAT(*req, MatchesResponseHeaders(::testing::_));
      ext_proc_stream->SendResponse(MakeResponseHeadersMutationResponse(
          {{kResponseHeadersMutatedHeaderKey, kHeaderMutatedValue}}));
    } else {
      FAIL() << "Unexpected request type: " << req->DebugString();
    }
  }
  // ext_proc server sees response body and sends it back.
  req = ext_proc_stream->GetNextRequest();
  ASSERT_THAT(req, ::testing::Optional(MatchesResponseBody(
                       EchoResponseMessageIs(absl::StrCat(
                           kRequestMessage, kRequestBodyMutatedSuffix)),
                       /*end_of_stream=*/false)));
  ext_proc_stream->SendResponse(MakeResponseBodyMutationResponse(
      ModifyEchoResponse(req->response_body().body(),
                         kResponseBodyMutatedSuffix),
      /*end_of_stream=*/false));
  // ext_proc server sees response trailers and sends them back.
  req = ext_proc_stream->GetNextRequest();
  ASSERT_THAT(req, ::testing::Optional(MatchesResponseTrailers(::testing::_)));
  ext_proc_stream->SendResponse(MakeResponseTrailersMutationResponse(
      {{kResponseTrailersMutatedHeaderKey, kHeaderMutatedValue}}));
  Status status = rpc.GetStatus();
  EXPECT_TRUE(status.ok()) << "RPC failed: " << status.error_message();
  EXPECT_THAT(rpc.GetServerInitialMetadata(),
              ::testing::AllOf(
                  ::testing::Contains(::testing::Pair(
                      kRequestHeadersMutatedHeaderKey, kHeaderMutatedValue)),
                  ::testing::Contains(::testing::Pair(
                      kResponseHeadersMutatedHeaderKey, kHeaderMutatedValue))));
  EXPECT_THAT(rpc.GetServerTrailingMetadata(),
              ::testing::Contains(::testing::Pair(
                  kResponseTrailersMutatedHeaderKey, kHeaderMutatedValue)));
  EXPECT_EQ(rpc.response().message(),
            absl::StrCat(kRequestMessage, kRequestBodyMutatedSuffix,
                         kResponseBodyMutatedSuffix));
}

TEST_P(XdsExtProcEnd2endTest,
       ProcessingModeAllEnabledWithObservabilityModeSuccess) {
  auto ext_proc_config =
      MakeFilterConfigBuilder()
          .SetObservabilityMode(true)
          .SetDeferredCloseTimeout(grpc_core::Duration::Seconds(1))
          .SetRequestHeaderMode()
          .SetResponseHeaderMode()
          .SetResponseTrailerMode()
          .SetRequestBodyMode()
          .SetResponseBodyMode()
          .Build();
  SetListenerAndRouteConfiguration(
      balancer_.get(), BuildListenerWithExtProcFilter(ext_proc_config),
      default_route_config_);
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get());
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  for (int i = 0; i < 6; ++i) {
    auto req = ext_proc_stream->GetNextRequest();
    ASSERT_TRUE(req.has_value());
    if (req->has_request_headers()) {
      EXPECT_THAT(*req,
                  MatchesRequestHeaders(::testing::Contains(::testing::Pair(
                      ":path", "/grpc.testing.EchoTestService/Echo"))));
    } else if (req->has_request_body()) {
      if (req->request_body().end_of_stream()) {
        EXPECT_THAT(*req,
                    MatchesRequestBody(/*body=*/"", /*end_of_stream=*/true));
      } else {
        EXPECT_THAT(*req,
                    MatchesRequestBody(EchoRequestMessageIs(kRequestMessage),
                                       /*end_of_stream=*/false));
      }
    } else if (req->has_response_headers()) {
      EXPECT_THAT(*req, MatchesResponseHeaders(::testing::_));
    } else if (req->has_response_body()) {
      EXPECT_THAT(*req,
                  MatchesResponseBody(EchoResponseMessageIs(kRequestMessage),
                                      /*end_of_stream=*/false));
    } else if (req->has_response_trailers()) {
      EXPECT_THAT(*req, MatchesResponseTrailers(::testing::_));
    } else {
      FAIL() << "Unexpected request type: " << req->DebugString();
    }
  }
  Status status = rpc.GetStatus();
  EXPECT_TRUE(status.ok()) << "RPC failed: " << status.error_message();
  // In observability mode, mutations should NOT be applied.
  EXPECT_THAT(rpc.GetServerInitialMetadata(),
              ::testing::Not(::testing::Contains(::testing::Pair(
                  kRequestHeadersMutatedHeaderKey, ::testing::_))));
  EXPECT_THAT(rpc.GetServerInitialMetadata(),
              ::testing::Not(::testing::Contains(::testing::Pair(
                  kResponseHeadersMutatedHeaderKey, ::testing::_))));
  EXPECT_THAT(rpc.GetServerTrailingMetadata(),
              ::testing::Not(::testing::Contains(::testing::Pair(
                  kResponseTrailersMutatedHeaderKey, ::testing::_))));
  EXPECT_EQ(rpc.response().message(), kRequestMessage);
}

TEST_P(XdsExtProcEnd2endTest, TrailersOnlyProcessingModeAllEnabled) {
  auto ext_proc_config = MakeFilterConfigBuilder()
                             .SetRequestHeaderMode()
                             .SetResponseHeaderMode()
                             .SetResponseTrailerMode()
                             .SetRequestBodyMode()
                             .SetResponseBodyMode()
                             .Build();
  SetListenerAndRouteConfiguration(
      balancer_.get(), BuildListenerWithExtProcFilter(ext_proc_config),
      default_route_config_);
  RpcOptions rpc_options;
  rpc_options.set_server_fail(true);
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  // ext_proc server sees request headers and sends them back.
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_THAT(
      req,
      ::testing::Optional(MatchesRequestHeaders(::testing::Contains(
          ::testing::Pair(":path", "/grpc.testing.EchoTestService/Echo")))));
  ext_proc_stream->SendResponse(MakeRequestHeadersMutationResponse(
      {{kRequestHeadersMutatedHeaderKey, kHeaderMutatedValue}}));
  // ext_proc server sees client message and sends it back.
  req = ext_proc_stream->GetNextRequest();
  ASSERT_THAT(req, ::testing::Optional(
                       MatchesRequestBody(EchoRequestMessageIs(kRequestMessage),
                                          /*end_of_stream=*/false)));
  ext_proc_stream->SendResponse(MakeRequestBodyMutationResponse(
      req->request_body().body(), req->request_body().end_of_stream()));
  // Next two events may arrive in either order: client half-close and server
  // response headers.
  for (int i = 0; i < 2; ++i) {
    req = ext_proc_stream->GetNextRequest();
    ASSERT_TRUE(req.has_value());
    if (req->has_request_body()) {
      EXPECT_THAT(*req,
                  MatchesRequestBody(/*body=*/"", /*end_of_stream=*/true));
      ext_proc_stream->SendResponse(MakeRequestBodyMutationResponse(
          /*body=*/"", /*end_of_stream=*/true));
    } else if (req->has_response_headers()) {
      EXPECT_THAT(*req, MatchesResponseHeaders(::testing::_));
      ext_proc_stream->SendResponse(MakeResponseHeadersMutationResponse(
          {{kResponseHeadersMutatedHeaderKey, kHeaderMutatedValue}}));
    } else {
      FAIL() << "Unexpected request type: " << req->DebugString();
    }
  }
  // For trailers-only response, ext_proc server sees no further requests.
  EXPECT_EQ(ext_proc_stream->GetNextRequest(), std::nullopt);
  Status status = rpc.GetStatus();
  EXPECT_THAT(status, GrpcStatusIs(StatusCode::FAILED_PRECONDITION, ""));
}

TEST_P(XdsExtProcEnd2endTest,
       TrailersOnlyProcessingModeAllEnabledWithObservabilityMode) {
  auto ext_proc_config = MakeFilterConfigBuilder()
                             .SetObservabilityMode(true)
                             .SetRequestHeaderMode()
                             .SetResponseHeaderMode()
                             .SetResponseTrailerMode()
                             .SetRequestBodyMode()
                             .SetResponseBodyMode()
                             .Build();
  SetListenerAndRouteConfiguration(
      balancer_.get(), BuildListenerWithExtProcFilter(ext_proc_config),
      default_route_config_);
  RpcOptions rpc_options;
  rpc_options.set_server_fail(true);
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  // ext_proc server sees request headers.
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_THAT(
      req,
      ::testing::Optional(MatchesRequestHeaders(::testing::Contains(
          ::testing::Pair(":path", "/grpc.testing.EchoTestService/Echo")))));
  // ext_proc server sees client message.
  req = ext_proc_stream->GetNextRequest();
  ASSERT_THAT(req, ::testing::Optional(
                       MatchesRequestBody(EchoRequestMessageIs(kRequestMessage),
                                          /*end_of_stream=*/false)));
  // Next two events may arrive in either order: client half-close and server
  // response headers (sent in observability mode for trailers-only).
  for (int i = 0; i < 2; ++i) {
    req = ext_proc_stream->GetNextRequest();
    ASSERT_TRUE(req.has_value());
    if (req->has_request_body()) {
      EXPECT_THAT(*req,
                  MatchesRequestBody(/*body=*/"", /*end_of_stream=*/true));
    } else if (req->has_response_headers()) {
      EXPECT_THAT(*req, MatchesResponseHeaders(::testing::_));
    } else {
      FAIL() << "Unexpected request type: " << req->DebugString();
    }
  }
  // For trailers-only response in observability mode, ext_proc server sees no
  // further requests.
  EXPECT_EQ(ext_proc_stream->GetNextRequest(), std::nullopt);
  Status status = rpc.GetStatus();
  EXPECT_THAT(status, GrpcStatusIs(StatusCode::FAILED_PRECONDITION, ""));
}

TEST_P(XdsExtProcEnd2endTest, RequestHeadersContinueAndReplaceFails) {
  auto ext_proc_config = MakeFilterConfigBuilder()
                             .SetRequestHeaderMode()
                             .SetResponseHeaderMode()
                             .SetResponseTrailerMode()
                             .SetRequestBodyMode()
                             .SetResponseBodyMode()
                             .Build();
  SetListenerAndRouteConfiguration(
      balancer_.get(), BuildListenerWithExtProcFilter(ext_proc_config),
      default_route_config_);
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get());
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_THAT(
      req,
      ::testing::Optional(MatchesRequestHeaders(::testing::Contains(
          ::testing::Pair(":path", "/grpc.testing.EchoTestService/Echo")))));
  ::envoy::service::ext_proc::v3::ProcessingResponse response;
  response.mutable_request_headers()->mutable_response()->set_status(
      ::envoy::service::ext_proc::v3::CommonResponse::CONTINUE_AND_REPLACE);
  ext_proc_stream->SendResponse(response);
  Status status = rpc.GetStatus();
  EXPECT_THAT(status, GrpcStatusIs(StatusCode::INTERNAL,
                                   "CONTINUE_AND_REPLACE is not supported"));
}

TEST_P(XdsExtProcEnd2endTest, RequestHeadersInvalidHeaderMutationFails) {
  auto ext_proc_config = MakeFilterConfigBuilder()
                             .SetRequestHeaderMode()
                             .SetResponseHeaderMode()
                             .SetResponseTrailerMode()
                             .SetRequestBodyMode()
                             .SetResponseBodyMode()
                             .Build();
  SetListenerAndRouteConfiguration(
      balancer_.get(), BuildListenerWithExtProcFilter(ext_proc_config),
      default_route_config_);
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get());
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_THAT(
      req,
      ::testing::Optional(MatchesRequestHeaders(::testing::Contains(
          ::testing::Pair(":path", "/grpc.testing.EchoTestService/Echo")))));
  ext_proc_stream->SendResponse(
      MakeRequestHeadersMutationResponse({{"host", "invalid-host"}}));
  Status status = rpc.GetStatus();
  EXPECT_THAT(
      status,
      GrpcStatusIs(StatusCode::INTERNAL,
                   "Failed to parse XdsHeaderValueOption: [field:header.key "
                   "error:header \"host\" not allowed]"));
}

TEST_P(XdsExtProcEnd2endTest, RequestHeadersRequestAttributesSent) {
  auto ext_proc_config = MakeFilterConfigBuilder()
                             .SetRequestHeaderMode()
                             .AddRequestAttribute("request.path")
                             .AddRequestAttribute("request.method")
                             .Build();
  SetListenerAndRouteConfiguration(
      balancer_.get(), BuildListenerWithExtProcFilter(ext_proc_config),
      default_route_config_);
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get());
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  // ext_proc server sees request headers with attributes and sends them back.
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_THAT(
      req,
      ::testing::Optional(MatchesRequestHeaders(::testing::Contains(
          ::testing::Pair(":path", "/grpc.testing.EchoTestService/Echo")))));
  EXPECT_EQ(GetExtProcAttribute(*req, "request.path"),
            "/grpc.testing.EchoTestService/Echo");
  EXPECT_EQ(GetExtProcAttribute(*req, "request.method"), "POST");
  ext_proc_stream->SendResponse(MakeRequestHeadersMutationResponse({}));
  Status status = rpc.GetStatus();
  EXPECT_TRUE(status.ok()) << status.error_message();
}

TEST_P(XdsExtProcEnd2endTest,
       RequestAttributesSentInRequestBodyWhenRequestHeaderIsSkip) {
  auto ext_proc_config = MakeFilterConfigBuilder()
                             .SetRequestBodyMode()
                             .AddRequestAttribute("request.path")
                             .AddRequestAttribute("request.method")
                             .Build();
  SetListenerAndRouteConfiguration(
      balancer_.get(), BuildListenerWithExtProcFilter(ext_proc_config),
      default_route_config_);
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get());
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  // ext_proc server sees client message with request attributes and sends it
  // back.
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_THAT(req, ::testing::Optional(
                       MatchesRequestBody(EchoRequestMessageIs(kRequestMessage),
                                          /*end_of_stream=*/false)));
  EXPECT_EQ(GetExtProcAttribute(*req, "request.path"),
            "/grpc.testing.EchoTestService/Echo");
  EXPECT_EQ(GetExtProcAttribute(*req, "request.method"), "POST");
  ext_proc_stream->SendResponse(MakeRequestBodyMutationResponse(
      req->request_body().body(), req->request_body().end_of_stream()));
  // ext_proc server sees client half-close and sends it back.
  req = ext_proc_stream->GetNextRequest();
  ASSERT_THAT(req, ::testing::Optional(MatchesRequestBody(
                       /*body=*/"", /*end_of_stream=*/true)));
  ext_proc_stream->SendResponse(MakeRequestBodyMutationResponse(
      /*body=*/"", /*end_of_stream=*/true));
  Status status = rpc.GetStatus();
  EXPECT_TRUE(status.ok()) << status.error_message();
}

TEST_P(XdsExtProcEnd2endTest, RequestBodyGrpcMessageCompressed) {
  auto ext_proc_config = MakeFilterConfigBuilder()
                             .SetFailureModeAllow(true)
                             .SetRequestHeaderMode()
                             .SetRequestBodyMode()
                             .Build();
  SetListenerAndRouteConfiguration(
      balancer_.get(), BuildListenerWithExtProcFilter(ext_proc_config),
      default_route_config_);
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get());
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_THAT(
      req,
      ::testing::Optional(MatchesRequestHeaders(::testing::Contains(
          ::testing::Pair(":path", "/grpc.testing.EchoTestService/Echo")))));
  ext_proc_stream->SendResponse(MakeRequestHeadersMutationResponse({}));
  auto next_req = ext_proc_stream->GetNextRequest();
  ASSERT_THAT(next_req, ::testing::Optional(MatchesRequestBody(
                            EchoRequestMessageIs(kRequestMessage),
                            /*end_of_stream=*/false)));
  ::envoy::service::ext_proc::v3::ProcessingResponse response;
  auto* common_response = response.mutable_request_body()->mutable_response();
  auto* body_mutation = common_response->mutable_body_mutation();
  auto* streamed_response = body_mutation->mutable_streamed_response();
  streamed_response->set_grpc_message_compressed(true);
  ext_proc_stream->SendResponse(response);
  Status status = rpc.GetStatus();
  EXPECT_THAT(status, GrpcStatusIs(StatusCode::INTERNAL,
                                   "grpc_message_compressed is not supported"));
}

TEST_P(XdsExtProcEnd2endTest,
       BidiStreamExtProcEarlyHalfCloseWithMessageSubsequentWriteFails) {
  auto ext_proc_config = MakeFilterConfigBuilder()
                             .SetRequestHeaderMode()
                             .SetRequestBodyMode()
                             .Build();
  SetListenerAndRouteConfiguration(
      balancer_.get(), BuildListenerWithExtProcFilter(ext_proc_config),
      default_route_config_);
  AsyncBidiStream stream;
  stream.Start(stub_.get());
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_THAT(req,
              ::testing::Optional(
                  MatchesRequestHeaders(::testing::Contains(::testing::Pair(
                      ":path", "/grpc.testing.EchoTestService/BidiStream")))));
  ext_proc_stream->SendResponse(MakeRequestHeadersMutationResponse({}));
  EchoRequest request;
  request.set_message(kMessage1);
  stream.StartWrite(request);
  auto next_req = ext_proc_stream->GetNextRequest();
  ASSERT_THAT(next_req, ::testing::Optional(
                            MatchesRequestBody(EchoRequestMessageIs(kMessage1),
                                               /*end_of_stream=*/false)));
  ::envoy::service::ext_proc::v3::ProcessingResponse proc_response;
  auto* common_response =
      proc_response.mutable_request_body()->mutable_response();
  auto* streamed_response =
      common_response->mutable_body_mutation()->mutable_streamed_response();
  streamed_response->set_body(
      ModifyEchoRequest(next_req->request_body().body(), kMutatedSuffix));
  streamed_response->set_end_of_stream(true);
  ext_proc_stream->SendResponse(proc_response);
  EXPECT_TRUE(stream.WaitForWriteDone());
  EXPECT_THAT(stream.ReadMessage(),
              ::testing::Optional(MatchesEchoResponse(kMessage1Mutated)));
  request.set_message(kMessage2);
  stream.StartWrite(request);
  (void)stream.WaitForWriteDone();
  EXPECT_THAT(
      stream.WaitForStatus(),
      ::testing::Optional(GrpcStatusIs(
          StatusCode::INTERNAL,
          "Client has requested for half close but external processor server "
          "has already force sent half close to the server")));
}

TEST_P(XdsExtProcEnd2endTest,
       BidiStreamExtProcEarlyHalfCloseWithoutMessageSubsequentWriteFails) {
  auto ext_proc_config = MakeFilterConfigBuilder()
                             .SetRequestHeaderMode()
                             .SetRequestBodyMode()
                             .Build();
  SetListenerAndRouteConfiguration(
      balancer_.get(), BuildListenerWithExtProcFilter(ext_proc_config),
      default_route_config_);
  AsyncBidiStream stream;
  stream.Start(stub_.get());
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_THAT(req,
              ::testing::Optional(
                  MatchesRequestHeaders(::testing::Contains(::testing::Pair(
                      ":path", "/grpc.testing.EchoTestService/BidiStream")))));
  ext_proc_stream->SendResponse(MakeRequestHeadersMutationResponse({}));
  EchoRequest request;
  request.set_message(kMessage1);
  stream.StartWrite(request);
  auto next_req = ext_proc_stream->GetNextRequest();
  ASSERT_THAT(next_req, ::testing::Optional(
                            MatchesRequestBody(EchoRequestMessageIs(kMessage1),
                                               /*end_of_stream=*/false)));
  ::envoy::service::ext_proc::v3::ProcessingResponse proc_response;
  auto* common_response =
      proc_response.mutable_request_body()->mutable_response();
  auto* streamed_response =
      common_response->mutable_body_mutation()->mutable_streamed_response();
  streamed_response->set_end_of_stream(true);
  streamed_response->set_end_of_stream_without_message(true);
  ext_proc_stream->SendResponse(proc_response);
  (void)stream.WaitForWriteDone();
  request.set_message(kMessage2);
  stream.StartWrite(request);
  (void)stream.WaitForWriteDone();
  EXPECT_THAT(
      stream.WaitForStatus(),
      ::testing::Optional(GrpcStatusIs(
          StatusCode::INTERNAL,
          "Client has requested for half close but external processor server "
          "has already force sent half close to the server")));
}

TEST_P(XdsExtProcEnd2endTest, BidiStreamNormalHalfCloseSuccess) {
  auto ext_proc_config = MakeFilterConfigBuilder()
                             .SetRequestHeaderMode()
                             .SetRequestBodyMode()
                             .Build();
  SetListenerAndRouteConfiguration(
      balancer_.get(), BuildListenerWithExtProcFilter(ext_proc_config),
      default_route_config_);
  AsyncBidiStream stream;
  stream.Start(stub_.get());
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_THAT(req,
              ::testing::Optional(
                  MatchesRequestHeaders(::testing::Contains(::testing::Pair(
                      ":path", "/grpc.testing.EchoTestService/BidiStream")))));
  ext_proc_stream->SendResponse(MakeRequestHeadersMutationResponse({}));
  EchoRequest request;
  for (int i = 1; i <= 3; ++i) {
    request.set_message(absl::StrCat("message", i));
    stream.StartWrite(request);
    auto next_req = ext_proc_stream->GetNextRequest();
    ASSERT_THAT(next_req, ::testing::Optional(MatchesRequestBody(
                              EchoRequestMessageIs(absl::StrCat("message", i)),
                              /*end_of_stream=*/false)));
    ext_proc_stream->SendResponse(MakeRequestBodyMutationResponse(
        next_req->request_body().body(),
        next_req->request_body().end_of_stream()));
    EXPECT_TRUE(stream.WaitForWriteDone());
    EXPECT_THAT(
        stream.ReadMessage(),
        ::testing::Optional(MatchesEchoResponse(absl::StrCat("message", i))));
  }
  stream.StartWritesDone();
  auto eos_req = ext_proc_stream->GetNextRequest();
  ASSERT_THAT(eos_req, ::testing::Optional(MatchesRequestBody(
                           /*body=*/"", /*end_of_stream=*/true)));
  ::envoy::service::ext_proc::v3::ProcessingResponse proc_response;
  auto* common_response =
      proc_response.mutable_request_body()->mutable_response();
  auto* streamed_response =
      common_response->mutable_body_mutation()->mutable_streamed_response();
  streamed_response->set_end_of_stream(true);
  streamed_response->set_end_of_stream_without_message(true);
  ext_proc_stream->SendResponse(proc_response);
  EXPECT_FALSE(stream.ReadMessage().has_value());
  EXPECT_THAT(stream.WaitForStatus(), ::testing::Optional(IsOk()));
}

//
// Response Headers tests
//

TEST_P(XdsExtProcEnd2endTest, ResponseHeadersInvalidHeaderMutationFails) {
  auto ext_proc_config =
      MakeFilterConfigBuilder().SetResponseHeaderMode().Build();
  SetListenerAndRouteConfiguration(
      balancer_.get(), BuildListenerWithExtProcFilter(ext_proc_config),
      default_route_config_);
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get());
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto resp_headers_req = ext_proc_stream->GetNextRequest();
  ASSERT_THAT(resp_headers_req,
              ::testing::Optional(MatchesResponseHeaders(::testing::_)));
  ::envoy::service::ext_proc::v3::ProcessingResponse response;
  auto* mutation = response.mutable_response_headers()
                       ->mutable_response()
                       ->mutable_header_mutation();
  auto* header = mutation->add_set_headers();
  header->mutable_header()->set_key("host");
  header->mutable_header()->set_value("invalid-host");
  ext_proc_stream->SendResponse(response);
  Status status = rpc.GetStatus();
  EXPECT_THAT(
      status,
      GrpcStatusIs(StatusCode::INTERNAL,
                   "Failed to parse XdsHeaderValueOption: [field:header.key "
                   "error:header \"host\" not allowed]"));
}

//
// Response Body tests
//

TEST_P(XdsExtProcEnd2endTest, ResponseBodyGrpcMessageCompressed) {
  auto ext_proc_config = MakeFilterConfigBuilder()
                             .SetFailureModeAllow(true)
                             .SetResponseHeaderMode()
                             .SetResponseTrailerMode()
                             .SetResponseBodyMode()
                             .Build();
  SetListenerAndRouteConfiguration(
      balancer_.get(), BuildListenerWithExtProcFilter(ext_proc_config),
      default_route_config_);
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get());
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto resp_headers_req = ext_proc_stream->GetNextRequest();
  ASSERT_THAT(resp_headers_req,
              ::testing::Optional(MatchesResponseHeaders(::testing::_)));
  ext_proc_stream->SendResponse(MakeResponseHeadersMutationResponse({}));
  auto resp_body_req = ext_proc_stream->GetNextRequest();
  ASSERT_THAT(resp_body_req, ::testing::Optional(MatchesResponseBody(
                                 EchoResponseMessageIs(kRequestMessage),
                                 /*end_of_stream=*/false)));
  ::envoy::service::ext_proc::v3::ProcessingResponse response;
  auto* common_response = response.mutable_response_body()->mutable_response();
  auto* body_mutation = common_response->mutable_body_mutation();
  auto* streamed_response = body_mutation->mutable_streamed_response();
  streamed_response->set_grpc_message_compressed(true);
  ext_proc_stream->SendResponse(response);
  Status status = rpc.GetStatus();
  EXPECT_THAT(status, GrpcStatusIs(StatusCode::INTERNAL,
                                   "grpc_message_compressed is not supported"));
}

//
// Response Trailers tests
//

TEST_P(XdsExtProcEnd2endTest, ResponseTrailersInvalidHeaderMutationFails) {
  auto ext_proc_config =
      MakeFilterConfigBuilder().SetResponseTrailerMode().Build();
  SetListenerAndRouteConfiguration(
      balancer_.get(), BuildListenerWithExtProcFilter(ext_proc_config),
      default_route_config_);
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get());
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_THAT(req, ::testing::Optional(MatchesResponseTrailers(::testing::_)));
  ext_proc_stream->SendResponse(
      MakeResponseTrailersMutationResponse({{"host", "invalid-host"}}));
  Status status = rpc.GetStatus();
  EXPECT_THAT(
      status,
      GrpcStatusIs(StatusCode::INTERNAL,
                   "Failed to parse XdsHeaderValueOption: [field:header.key "
                   "error:header \"host\" not allowed]"));
}

//
// Immediate Response (Disabled) tests
//

TEST_P(XdsExtProcEnd2endTest, DisableImmediateResponse) {
  auto ext_proc_config = MakeFilterConfigBuilder()
                             .SetDisableImmediateResponse(true)
                             .SetFailureModeAllow(false)
                             .SetRequestHeaderMode()
                             .SetResponseHeaderMode()
                             .SetResponseTrailerMode()
                             .SetRequestBodyMode()
                             .SetResponseBodyMode()
                             .Build();
  SetListenerAndRouteConfiguration(
      balancer_.get(), BuildListenerWithExtProcFilter(ext_proc_config),
      default_route_config_);
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get());
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_THAT(
      req,
      ::testing::Optional(MatchesRequestHeaders(::testing::Contains(
          ::testing::Pair(":path", "/grpc.testing.EchoTestService/Echo")))));
  ext_proc_stream->SendResponse(MakeImmediateResponse(
      grpc::StatusCode::PERMISSION_DENIED,
      "Access Denied by ExtProc (Request Headers)",
      {{kImmediateResponseHeaderKey, kHeaderMutatedValue}}));
  Status status = rpc.GetStatus();
  EXPECT_THAT(
      status,
      GrpcStatusIs(StatusCode::INTERNAL,
                   "unhandled immediate response due to config disabled it"));
}

//
// Immediate Response (Enabled) tests
//

TEST_P(XdsExtProcEnd2endTest, ImmediateResponse) {
  auto ext_proc_config = MakeFilterConfigBuilder()
                             .SetRequestHeaderMode()
                             .SetResponseHeaderMode()
                             .SetResponseTrailerMode()
                             .SetRequestBodyMode()
                             .SetResponseBodyMode()
                             .Build();
  SetListenerAndRouteConfiguration(
      balancer_.get(), BuildListenerWithExtProcFilter(ext_proc_config),
      default_route_config_);
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get());
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
                           "Access Denied by ExtProc (Request Headers)"));
}

//
// Stream Drain tests
//

TEST_P(XdsExtProcEnd2endTest, StreamDrainRequestOnClientBody) {
  auto ext_proc_config = MakeFilterConfigBuilder()
                             .SetFailureModeAllow(false)
                             .SetRequestBodyMode()
                             .Build();
  SetListenerAndRouteConfiguration(
      balancer_.get(), BuildListenerWithExtProcFilter(ext_proc_config),
      default_route_config_);
  AsyncBidiStream stream;
  stream.Start(stub_.get());
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  EchoRequest request;
  request.set_message(kMessage1);
  stream.StartWrite(request);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_THAT(req,
              ::testing::Optional(MatchesRequestBody(
                  EchoRequestMessageIs(kMessage1), /*end_of_stream=*/false)));
  ext_proc_stream->SendResponse(MakeRequestBodyMutationResponse(
      ModifyEchoRequest(req->request_body().body(), kMutatedSuffix),
      /*end_of_stream=*/false, /*request_drain=*/true));
  EXPECT_TRUE(stream.WaitForWriteDone());
  EXPECT_THAT(stream.ReadMessage(),
              ::testing::Optional(MatchesEchoResponse(kMessage1Mutated)));
  request.set_message(kMessage2);
  stream.StartWrite(request);
  EXPECT_TRUE(stream.WaitForWriteDone());
  EXPECT_THAT(stream.ReadMessage(),
              ::testing::Optional(MatchesEchoResponse(kMessage2)));
  stream.StartWritesDone();
  EXPECT_THAT(stream.WaitForStatus(), ::testing::Optional(IsOk()));
}

TEST_P(XdsExtProcEnd2endTest, StreamDrainRequestOnRequestHeaders) {
  auto ext_proc_config = MakeFilterConfigBuilder()
                             .SetFailureModeAllow(false)
                             .SetRequestHeaderMode()
                             .Build();
  SetListenerAndRouteConfiguration(
      balancer_.get(), BuildListenerWithExtProcFilter(ext_proc_config),
      default_route_config_);
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get());
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_THAT(
      req,
      ::testing::Optional(MatchesRequestHeaders(::testing::Contains(
          ::testing::Pair(":path", "/grpc.testing.EchoTestService/Echo")))));
  ext_proc_stream->SendResponse(
      MakeRequestHeadersMutationResponse({}, {}, /*request_drain=*/true));
  Status status = rpc.GetStatus();
  EXPECT_TRUE(status.ok()) << status.error_message();
  EXPECT_EQ(rpc.response().message(), kRequestMessage);
}

TEST_P(XdsExtProcEnd2endTest, StreamDrainRequestOnResponseHeaders) {
  auto ext_proc_config = MakeFilterConfigBuilder()
                             .SetFailureModeAllow(false)
                             .SetResponseHeaderMode()
                             .Build();
  SetListenerAndRouteConfiguration(
      balancer_.get(), BuildListenerWithExtProcFilter(ext_proc_config),
      default_route_config_);
  AsyncBidiStream stream;
  stream.Start(stub_.get());
  EchoRequest request;
  request.set_message(kMessage1);
  stream.StartWrite(request);
  EXPECT_TRUE(stream.WaitForWriteDone());
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto resp_headers_req = ext_proc_stream->GetNextRequest();
  ASSERT_THAT(resp_headers_req,
              ::testing::Optional(MatchesResponseHeaders(::testing::_)));
  ext_proc_stream->SendResponse(
      MakeResponseHeadersMutationResponse({}, {}, /*request_drain=*/true));
  EXPECT_THAT(stream.ReadMessage(),
              ::testing::Optional(MatchesEchoResponse(kMessage1)));
  stream.StartWritesDone();
  EXPECT_THAT(stream.WaitForStatus(), ::testing::Optional(IsOk()));
}

TEST_P(XdsExtProcEnd2endTest, StreamDrainRequestOnResponseTrailers) {
  auto ext_proc_config = MakeFilterConfigBuilder()
                             .SetFailureModeAllow(false)
                             .SetResponseTrailerMode()
                             .Build();
  SetListenerAndRouteConfiguration(
      balancer_.get(), BuildListenerWithExtProcFilter(ext_proc_config),
      default_route_config_);
  AsyncBidiStream stream;
  stream.Start(stub_.get());
  EchoRequest request;
  request.set_message(kMessage1);
  stream.StartWrite(request);
  EXPECT_TRUE(stream.WaitForWriteDone());
  stream.StartReadMessage();
  EXPECT_THAT(stream.WaitForReadDone(),
              ::testing::Optional(MatchesEchoResponse(kMessage1)));
  stream.StartWritesDone();
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_THAT(req, ::testing::Optional(MatchesResponseTrailers(::testing::_)));
  ext_proc_stream->SendResponse(
      MakeResponseTrailersMutationResponse({}, {}, /*request_drain=*/true));
  EXPECT_THAT(stream.WaitForStatus(), ::testing::Optional(IsOk()));
}

TEST_P(XdsExtProcEnd2endTest, StreamDrainRequestOnServerBody) {
  auto ext_proc_config = MakeFilterConfigBuilder()
                             .SetFailureModeAllow(false)
                             .SetResponseBodyMode()
                             .SetResponseTrailerMode()
                             .Build();
  SetListenerAndRouteConfiguration(
      balancer_.get(), BuildListenerWithExtProcFilter(ext_proc_config),
      default_route_config_);
  AsyncBidiStream stream;
  stream.Start(stub_.get());
  EchoRequest request;
  request.set_message(kMessage1);
  stream.StartWrite(request);
  EXPECT_TRUE(stream.WaitForWriteDone());
  stream.StartReadMessage();
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto resp_body_req = ext_proc_stream->GetNextRequest();
  ASSERT_THAT(resp_body_req,
              ::testing::Optional(MatchesResponseBody(
                  EchoResponseMessageIs(kMessage1), /*end_of_stream=*/false)));
  ext_proc_stream->SendResponse(MakeResponseBodyMutationResponse(
      ModifyEchoResponse(resp_body_req->response_body().body(), kMutatedSuffix),
      /*end_of_stream=*/false, /*request_drain=*/true));
  EXPECT_THAT(stream.WaitForReadDone(),
              ::testing::Optional(MatchesEchoResponse(kMessage1Mutated)));
  request.set_message(kMessage2);
  stream.StartWrite(request);
  EXPECT_TRUE(stream.WaitForWriteDone());
  stream.StartReadMessage();
  EXPECT_THAT(stream.WaitForReadDone(),
              ::testing::Optional(MatchesEchoResponse(kMessage2)));
  stream.StartWritesDone();
  EXPECT_THAT(stream.WaitForStatus(), ::testing::Optional(IsOk()));
}

//
// Ordering tests
//

TEST_P(XdsExtProcEnd2endTest,
       ClientToServerOrderingHeadersResponseWhenDisabled) {
  auto ext_proc_config = MakeFilterConfigBuilder()
                             .SetFailureModeAllow(false)
                             .SetRequestBodyMode()
                             .Build();
  SetListenerAndRouteConfiguration(
      balancer_.get(), BuildListenerWithExtProcFilter(ext_proc_config),
      default_route_config_);
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get());
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_THAT(req, ::testing::Optional(
                       MatchesRequestBody(EchoRequestMessageIs(kRequestMessage),
                                          /*end_of_stream=*/false)));
  ext_proc_stream->SendResponse(MakeRequestHeadersMutationResponse({}));
  Status status = rpc.GetStatus();
  EXPECT_THAT(status, GrpcStatusIs(StatusCode::INTERNAL,
                                   "Received unexpected request headers "
                                   "response from external processor"));
}

TEST_P(XdsExtProcEnd2endTest, ClientToServerOrderingRequestBodyBeforeHeaders) {
  auto ext_proc_config = MakeFilterConfigBuilder()
                             .SetFailureModeAllow(true)
                             .SetRequestHeaderMode()
                             .SetRequestBodyMode()
                             .Build();
  SetListenerAndRouteConfiguration(
      balancer_.get(), BuildListenerWithExtProcFilter(ext_proc_config),
      default_route_config_);
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get());
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_THAT(
      req,
      ::testing::Optional(MatchesRequestHeaders(::testing::Contains(
          ::testing::Pair(":path", "/grpc.testing.EchoTestService/Echo")))));
  ext_proc_stream->SendResponse(MakeRequestBodyMutationResponse(""));
  Status status = rpc.GetStatus();
  EXPECT_THAT(
      status,
      GrpcStatusIs(
          StatusCode::INTERNAL,
          "Received unexpected request body response from external processor"));
}

TEST_P(XdsExtProcEnd2endTest,
       ServerToClientOrderingHeadersResponseWhenDisabled) {
  auto ext_proc_config = MakeFilterConfigBuilder()
                             .SetFailureModeAllow(false)
                             .SetResponseBodyMode()
                             .SetResponseTrailerMode()
                             .Build();
  SetListenerAndRouteConfiguration(
      balancer_.get(), BuildListenerWithExtProcFilter(ext_proc_config),
      default_route_config_);
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get());
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto next_opt = ext_proc_stream->GetNextRequest();
  ASSERT_THAT(next_opt, ::testing::Optional(MatchesResponseBody(
                            EchoResponseMessageIs(kRequestMessage),
                            /*end_of_stream=*/false)));
  ext_proc_stream->SendResponse(MakeResponseHeadersMutationResponse({}));
  Status status = rpc.GetStatus();
  EXPECT_THAT(status,
              GrpcStatusIs(StatusCode::INTERNAL,
                           "Received unexpected response headers response from "
                           "external processor"));
}

TEST_P(XdsExtProcEnd2endTest, ServerToClientOrderingResponseBodyBeforeHeaders) {
  auto ext_proc_config = MakeFilterConfigBuilder()
                             .SetFailureModeAllow(false)
                             .SetResponseHeaderMode()
                             .SetResponseBodyMode()
                             .SetResponseTrailerMode()
                             .Build();
  SetListenerAndRouteConfiguration(
      balancer_.get(), BuildListenerWithExtProcFilter(ext_proc_config),
      default_route_config_);
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get());
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto resp_hdr_req = ext_proc_stream->GetNextRequest();
  ASSERT_THAT(resp_hdr_req,
              ::testing::Optional(MatchesResponseHeaders(::testing::_)));
  ext_proc_stream->SendResponse(MakeResponseBodyMutationResponse(""));
  Status status = rpc.GetStatus();
  EXPECT_THAT(status, GrpcStatusIs(StatusCode::INTERNAL,
                                   "Received unexpected response body response "
                                   "from external processor"));
}

TEST_P(XdsExtProcEnd2endTest, ServerToClientOrderingTrailersBeforeHeaders) {
  auto ext_proc_config = MakeFilterConfigBuilder()
                             .SetFailureModeAllow(false)
                             .SetResponseHeaderMode()
                             .SetResponseTrailerMode()
                             .Build();
  SetListenerAndRouteConfiguration(
      balancer_.get(), BuildListenerWithExtProcFilter(ext_proc_config),
      default_route_config_);
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get());
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto resp_hdr_req = ext_proc_stream->GetNextRequest();
  ASSERT_THAT(resp_hdr_req,
              ::testing::Optional(MatchesResponseHeaders(::testing::_)));
  ext_proc_stream->SendResponse(MakeResponseTrailersMutationResponse({}));
  Status status = rpc.GetStatus();
  EXPECT_THAT(status, GrpcStatusIs(StatusCode::INTERNAL,
                                   "Received unexpected response trailers "
                                   "response from external processor"));
}

TEST_P(XdsExtProcEnd2endTest,
       ServerToClientOrderingTrailersBeforeResponseBody) {
  auto ext_proc_config = MakeFilterConfigBuilder()
                             .SetFailureModeAllow(false)
                             .SetResponseHeaderMode()
                             .SetResponseBodyMode()
                             .SetResponseTrailerMode()
                             .Build();
  SetListenerAndRouteConfiguration(
      balancer_.get(), BuildListenerWithExtProcFilter(ext_proc_config),
      default_route_config_);
  AsyncBidiStream stream;
  stream.Start(stub_.get());
  EchoRequest request;
  request.set_message(kMessage1);
  stream.StartWrite(request);
  EXPECT_TRUE(stream.WaitForWriteDone());
  stream.StartReadMessage();
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto resp_hdr_req = ext_proc_stream->GetNextRequest();
  ASSERT_THAT(resp_hdr_req,
              ::testing::Optional(MatchesResponseHeaders(::testing::_)));
  ext_proc_stream->SendResponse(MakeResponseHeadersMutationResponse({}));
  auto resp_body_req = ext_proc_stream->GetNextRequest();
  ASSERT_THAT(resp_body_req,
              ::testing::Optional(MatchesResponseBody(
                  EchoResponseMessageIs(kMessage1), /*end_of_stream=*/false)));
  ext_proc_stream->SendResponse(MakeResponseTrailersMutationResponse({}));
  EXPECT_FALSE(stream.WaitForReadDone().has_value());
  EXPECT_THAT(stream.WaitForStatus(),
              ::testing::Optional(GrpcStatusIs(
                  StatusCode::INTERNAL,
                  "Received response trailers response before all "
                  "outstanding response body responses were received")));
}

TEST_P(XdsExtProcEnd2endTest,
       ServerToClientOrderingTrailersResponseWhenDisabled) {
  auto ext_proc_config = MakeFilterConfigBuilder()
                             .SetFailureModeAllow(false)
                             .SetResponseHeaderMode()
                             .Build();
  SetListenerAndRouteConfiguration(
      balancer_.get(), BuildListenerWithExtProcFilter(ext_proc_config),
      default_route_config_);
  AsyncBidiStream stream;
  stream.Start(stub_.get());
  EchoRequest request;
  request.set_message(kMessage1);
  stream.StartWrite(request);
  EXPECT_TRUE(stream.WaitForWriteDone());
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto resp_hdr_req = ext_proc_stream->GetNextRequest();
  ASSERT_THAT(resp_hdr_req,
              ::testing::Optional(MatchesResponseHeaders(::testing::_)));
  ext_proc_stream->SendResponse(MakeResponseHeadersMutationResponse({}));
  ext_proc_stream->SendResponse(MakeResponseTrailersMutationResponse({}));
  EXPECT_THAT(
      stream.WaitForStatus(),
      ::testing::Optional(GrpcStatusIs(StatusCode::INTERNAL,
                                       "Received unexpected response trailers "
                                       "response from external processor")));
}

TEST_P(XdsExtProcEnd2endTest, ServerToClientResponseBodyHalfClose) {
  auto ext_proc_config = MakeFilterConfigBuilder()
                             .SetFailureModeAllow(true)
                             .SetResponseHeaderMode()
                             .SetResponseTrailerMode()
                             .SetResponseBodyMode()
                             .Build();
  SetListenerAndRouteConfiguration(
      balancer_.get(), BuildListenerWithExtProcFilter(ext_proc_config),
      default_route_config_);
  AsyncBidiStream stream;
  stream.Start(stub_.get());
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  EchoRequest request;
  request.set_message(kRequestMessage);
  stream.StartWrite(request);
  EXPECT_TRUE(stream.WaitForWriteDone());
  stream.StartReadMessage();
  auto resp_hdr_req = ext_proc_stream->GetNextRequest();
  ASSERT_THAT(resp_hdr_req,
              ::testing::Optional(MatchesResponseHeaders(::testing::_)));
  ext_proc_stream->SendResponse(MakeResponseHeadersMutationResponse({}));
  auto resp_body_req = ext_proc_stream->GetNextRequest();
  ASSERT_THAT(resp_body_req, ::testing::Optional(MatchesResponseBody(
                                 EchoResponseMessageIs(kRequestMessage),
                                 /*end_of_stream=*/false)));
  ext_proc_stream->SendResponse(
      MakeResponseBodyMutationResponse("", /*end_of_stream=*/true));
  EXPECT_FALSE(stream.WaitForReadDone().has_value());
  EXPECT_THAT(stream.WaitForStatus(),
              ::testing::Optional(GrpcStatusIs(
                  StatusCode::INTERNAL,
                  "end_of_stream / end_of_stream_without_message is "
                  "not supported for response_body")));
}

//
// Stream Clean Close and Error tests
//

TEST_P(XdsExtProcEnd2endTest, StreamCleanCloseBodiesNotConfiguredSuccess) {
  ResetStub();
  auto ext_proc_config = MakeFilterConfigBuilder()
                             .SetFailureModeAllow(false)
                             .SetRequestHeaderMode()
                             .SetResponseHeaderMode()
                             .Build();
  SetListenerAndRouteConfiguration(
      balancer_.get(), BuildListenerWithExtProcFilter(ext_proc_config),
      default_route_config_);
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get());
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_THAT(
      req,
      ::testing::Optional(MatchesRequestHeaders(::testing::Contains(
          ::testing::Pair(":path", "/grpc.testing.EchoTestService/Echo")))));
  ext_proc_stream->SendStatus(absl::OkStatus());
  Status status = rpc.GetStatus();
  EXPECT_TRUE(status.ok()) << status.error_message();
  EXPECT_EQ(rpc.response().message(), kRequestMessage);
}

TEST_P(XdsExtProcEnd2endTest, StreamCleanCloseBodiesDrainedSuccess) {
  ResetStub();
  auto ext_proc_config = MakeFilterConfigBuilder()
                             .SetFailureModeAllow(false)
                             .SetRequestBodyMode()
                             .SetResponseBodyMode()
                             .SetResponseTrailerMode()
                             .Build();
  SetListenerAndRouteConfiguration(
      balancer_.get(), BuildListenerWithExtProcFilter(ext_proc_config),
      default_route_config_);
  AsyncBidiStream stream;
  stream.Start(stub_.get());
  EchoRequest request;
  request.set_message(kMessage1);
  stream.StartWrite(request);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_THAT(req,
              ::testing::Optional(MatchesRequestBody(
                  EchoRequestMessageIs(kMessage1), /*end_of_stream=*/false)));
  ext_proc_stream->SendResponse(MakeRequestBodyMutationResponse(
      ModifyEchoRequest(req->request_body().body(), kMutatedSuffix),
      /*end_of_stream=*/false));
  EXPECT_TRUE(stream.WaitForWriteDone());
  stream.StartReadMessage();
  auto resp_body_req = ext_proc_stream->GetNextRequest();
  ASSERT_THAT(resp_body_req, ::testing::Optional(MatchesResponseBody(
                                 EchoResponseMessageIs(kMessage1Mutated),
                                 /*end_of_stream=*/false)));
  ext_proc_stream->SendResponse(MakeResponseBodyMutationResponse(
      ModifyEchoResponse(resp_body_req->response_body().body(), kMutatedSuffix),
      /*end_of_stream=*/false, /*request_drain=*/true));
  EXPECT_THAT(stream.WaitForReadDone(),
              ::testing::Optional(MatchesEchoResponse(kMessage1DoubleMutated)));
  ext_proc_stream->SendStatus(absl::OkStatus());
  stream.StartWritesDone();
  EXPECT_THAT(stream.WaitForStatus(), ::testing::Optional(IsOk()));
}

TEST_P(XdsExtProcEnd2endTest,
       StreamCleanCloseRequestBodyInFlightObservabilitySuccess) {
  ResetStub();
  auto ext_proc_config = MakeFilterConfigBuilder()
                             .SetObservabilityMode(true)
                             .SetFailureModeAllow(false)
                             .SetRequestBodyMode()
                             .Build();
  SetListenerAndRouteConfiguration(
      balancer_.get(), BuildListenerWithExtProcFilter(ext_proc_config),
      default_route_config_);
  AsyncBidiStream stream;
  stream.Start(stub_.get());
  EchoRequest request;
  request.set_message(kMessage1);
  stream.StartWrite(request);
  EXPECT_TRUE(stream.WaitForWriteDone());
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_THAT(req,
              ::testing::Optional(MatchesRequestBody(
                  EchoRequestMessageIs(kMessage1), /*end_of_stream=*/false)));
  ext_proc_stream->SendStatus(absl::OkStatus());
  EXPECT_THAT(stream.ReadMessage(),
              ::testing::Optional(MatchesEchoResponse(kMessage1)));
  request.set_message(kMessage2);
  stream.StartWrite(request);
  EXPECT_TRUE(stream.WaitForWriteDone());
  EXPECT_THAT(stream.ReadMessage(),
              ::testing::Optional(MatchesEchoResponse(kMessage2)));
  stream.StartWritesDone();
  EXPECT_THAT(stream.WaitForStatus(), ::testing::Optional(IsOk()));
}

TEST_P(XdsExtProcEnd2endTest,
       StreamCleanCloseResponseBodyInFlightObservabilitySuccess) {
  ResetStub();
  auto ext_proc_config = MakeFilterConfigBuilder()
                             .SetObservabilityMode(true)
                             .SetFailureModeAllow(false)
                             .SetResponseBodyMode()
                             .SetResponseTrailerMode()
                             .Build();
  SetListenerAndRouteConfiguration(
      balancer_.get(), BuildListenerWithExtProcFilter(ext_proc_config),
      default_route_config_);
  AsyncBidiStream stream;
  stream.Start(stub_.get());
  EchoRequest request;
  request.set_message(kMessage1);
  stream.StartWrite(request);
  EXPECT_TRUE(stream.WaitForWriteDone());
  stream.StartReadMessage();
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto resp_body_req = ext_proc_stream->GetNextRequest();
  ASSERT_THAT(resp_body_req, ::testing::Optional(MatchesResponseBody(
                                 EchoResponseMessageIs(kMessage1),
                                 /*end_of_stream=*/false)));
  EXPECT_THAT(stream.WaitForReadDone(),
              ::testing::Optional(MatchesEchoResponse(kMessage1)));
  request.set_message(kMessage2);
  stream.StartWrite(request);
  EXPECT_TRUE(stream.WaitForWriteDone());
  stream.StartReadMessage();
  auto resp_body_req2 = ext_proc_stream->GetNextRequest();
  ASSERT_THAT(resp_body_req2, ::testing::Optional(MatchesResponseBody(
                                  EchoResponseMessageIs(kMessage2),
                                  /*end_of_stream=*/false)));
  ext_proc_stream->SendStatus(absl::OkStatus());
  EXPECT_THAT(stream.WaitForReadDone(),
              ::testing::Optional(MatchesEchoResponse(kMessage2)));
  request.set_message(kMessage3);
  stream.StartWrite(request);
  EXPECT_TRUE(stream.WaitForWriteDone());
  EXPECT_THAT(stream.ReadMessage(),
              ::testing::Optional(MatchesEchoResponse(kMessage3)));
  stream.StartWritesDone();
  EXPECT_THAT(stream.WaitForStatus(), ::testing::Optional(IsOk()));
}

TEST_P(XdsExtProcEnd2endTest, StreamCleanCloseRequestBodyNotDrainedFails) {
  ResetStub();
  auto ext_proc_config = MakeFilterConfigBuilder()
                             .SetFailureModeAllow(false)
                             .SetRequestBodyMode()
                             .Build();
  SetListenerAndRouteConfiguration(
      balancer_.get(), BuildListenerWithExtProcFilter(ext_proc_config),
      default_route_config_);
  AsyncBidiStream stream;
  stream.Start(stub_.get());
  EchoRequest request;
  request.set_message(kMessage1);
  stream.StartWrite(request);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_THAT(req,
              ::testing::Optional(MatchesRequestBody(
                  EchoRequestMessageIs(kMessage1), /*end_of_stream=*/false)));
  ext_proc_stream->SendStatus(absl::OkStatus());
  stream.StartWritesDone();
  EXPECT_THAT(
      stream.WaitForStatus(),
      ::testing::Optional(GrpcStatusIs(StatusCode::INTERNAL,
                                       "Stream closed cleanly without drain")));
}

TEST_P(XdsExtProcEnd2endTest, StreamCleanCloseResponseBodyNotDrainedFails) {
  ResetStub();
  auto ext_proc_config = MakeFilterConfigBuilder()
                             .SetFailureModeAllow(false)
                             .SetResponseBodyMode()
                             .SetResponseTrailerMode()
                             .Build();
  SetListenerAndRouteConfiguration(
      balancer_.get(), BuildListenerWithExtProcFilter(ext_proc_config),
      default_route_config_);
  AsyncBidiStream stream;
  stream.Start(stub_.get());
  EchoRequest request;
  request.set_message(kMessage1);
  stream.StartWrite(request);
  EXPECT_TRUE(stream.WaitForWriteDone());
  stream.StartReadMessage();
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_THAT(req,
              ::testing::Optional(MatchesResponseBody(
                  EchoResponseMessageIs(kMessage1), /*end_of_stream=*/false)));
  ext_proc_stream->SendResponse(
      MakeResponseBodyMutationResponse(req->response_body().body()));
  EXPECT_THAT(stream.WaitForReadDone(),
              ::testing::Optional(MatchesEchoResponse(kMessage1)));
  stream.StartReadMessage();
  request.set_message(kMessage2);
  stream.StartWrite(request);
  auto req2 = ext_proc_stream->GetNextRequest();
  ASSERT_THAT(req2,
              ::testing::Optional(MatchesResponseBody(
                  EchoResponseMessageIs(kMessage2), /*end_of_stream=*/false)));
  ext_proc_stream->SendStatus(absl::OkStatus());
  stream.StartWritesDone();
  EXPECT_THAT(
      stream.WaitForStatus(),
      ::testing::Optional(GrpcStatusIs(StatusCode::INTERNAL,
                                       "Stream closed cleanly without drain")));
}

TEST_P(XdsExtProcEnd2endTest,
       StreamCleanCloseBeforeBodySentFailureModeAllowSuccess) {
  ResetStub();
  auto ext_proc_config = MakeFilterConfigBuilder()
                             .SetFailureModeAllow(true)
                             .SetRequestHeaderMode()
                             .SetRequestBodyMode()
                             .SetResponseBodyMode()
                             .SetResponseTrailerMode()
                             .Build();
  SetListenerAndRouteConfiguration(
      balancer_.get(), BuildListenerWithExtProcFilter(ext_proc_config),
      default_route_config_);
  AsyncBidiStream stream;
  stream.Start(stub_.get());
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_THAT(req,
              ::testing::Optional(
                  MatchesRequestHeaders(::testing::Contains(::testing::Pair(
                      ":path", "/grpc.testing.EchoTestService/BidiStream")))));
  ext_proc_stream->SendResponse(MakeRequestHeadersMutationResponse({}));
  ext_proc_stream->SendStatus(absl::OkStatus());
  absl::SleepFor(absl::Milliseconds(100));
  EchoRequest request;
  request.set_message(kMessage1);
  stream.StartWrite(request);
  EXPECT_TRUE(stream.WaitForWriteDone());
  EXPECT_THAT(stream.ReadMessage(),
              ::testing::Optional(MatchesEchoResponse(kMessage1)));
  stream.StartWritesDone();
  EXPECT_THAT(stream.WaitForStatus(), ::testing::Optional(IsOk()));
}
TEST_P(XdsExtProcEnd2endTest,
       StreamErrorFailureModeAllowBodiesNotConfiguredSuccess) {
  ResetStub();
  auto ext_proc_config = MakeFilterConfigBuilder()
                             .SetFailureModeAllow(true)
                             .SetRequestHeaderMode()
                             .SetResponseHeaderMode()
                             .Build();
  SetListenerAndRouteConfiguration(
      balancer_.get(), BuildListenerWithExtProcFilter(ext_proc_config),
      default_route_config_);
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get());
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_THAT(
      req,
      ::testing::Optional(MatchesRequestHeaders(::testing::Contains(
          ::testing::Pair(":path", "/grpc.testing.EchoTestService/Echo")))));
  ext_proc_stream->SendStatus(
      absl::UnavailableError("Call closed by ext_proc server on headers"));
  Status status = rpc.GetStatus();
  EXPECT_TRUE(status.ok()) << status.error_message();
  EXPECT_EQ(rpc.response().message(), kRequestMessage);
}

TEST_P(XdsExtProcEnd2endTest, StreamErrorFailureModeAllowObservabilitySuccess) {
  ResetStub();
  auto ext_proc_config = MakeFilterConfigBuilder()
                             .SetObservabilityMode(true)
                             .SetFailureModeAllow(true)
                             .SetRequestHeaderMode()
                             .SetResponseHeaderMode()
                             .SetResponseTrailerMode()
                             .SetRequestBodyMode()
                             .SetResponseBodyMode()
                             .Build();
  SetListenerAndRouteConfiguration(
      balancer_.get(), BuildListenerWithExtProcFilter(ext_proc_config),
      default_route_config_);
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get());
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_THAT(
      req,
      ::testing::Optional(MatchesRequestHeaders(::testing::Contains(
          ::testing::Pair(":path", "/grpc.testing.EchoTestService/Echo")))));
  auto next_req = ext_proc_stream->GetNextRequest();
  ASSERT_THAT(next_req, ::testing::Optional(MatchesRequestBody(
                            EchoRequestMessageIs(kRequestMessage),
                            /*end_of_stream=*/false)));
  ext_proc_stream->SendStatus(absl::ResourceExhaustedError(
      "Call closed by ext_proc server on request body"));
  Status status = rpc.GetStatus();
  EXPECT_TRUE(status.ok()) << "Expected OK, got: " << status.error_message();
  EXPECT_EQ(rpc.response().message(), kRequestMessage);
}

TEST_P(XdsExtProcEnd2endTest,
       StreamErrorFailureModeAllowRequestBodyNotDrainedFails) {
  auto ext_proc_config = MakeFilterConfigBuilder()
                             .SetFailureModeAllow(true)
                             .SetRequestBodyMode()
                             .Build();
  SetListenerAndRouteConfiguration(
      balancer_.get(), BuildListenerWithExtProcFilter(ext_proc_config),
      default_route_config_);
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get());
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_THAT(req, ::testing::Optional(
                       MatchesRequestBody(EchoRequestMessageIs(kRequestMessage),
                                          /*end_of_stream=*/false)));
  ext_proc_stream->SendStatus(absl::ResourceExhaustedError(
      "Call closed by ext_proc server on request body"));
  Status status = rpc.GetStatus();
  EXPECT_THAT(
      status,
      GrpcStatusIs(StatusCode::INTERNAL,
                   "External processor stream failed: RESOURCE_EXHAUSTED: Call "
                   "closed by ext_proc server on request body"));
}

TEST_P(XdsExtProcEnd2endTest,
       StreamErrorFailureModeAllowResponseBodyNotDrainedFails) {
  auto ext_proc_config = MakeFilterConfigBuilder()
                             .SetFailureModeAllow(true)
                             .SetResponseBodyMode()
                             .SetResponseTrailerMode()
                             .Build();
  SetListenerAndRouteConfiguration(
      balancer_.get(), BuildListenerWithExtProcFilter(ext_proc_config),
      default_route_config_);
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get());
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto resp_body_req = ext_proc_stream->GetNextRequest();
  ASSERT_THAT(resp_body_req, ::testing::Optional(MatchesResponseBody(
                                 EchoResponseMessageIs(kRequestMessage),
                                 /*end_of_stream=*/false)));
  ext_proc_stream->SendStatus(absl::ResourceExhaustedError(
      "Call closed by ext_proc server on response body"));
  Status status = rpc.GetStatus();
  EXPECT_THAT(
      status,
      GrpcStatusIs(StatusCode::INTERNAL,
                   "External processor stream failed: RESOURCE_EXHAUSTED: Call "
                   "closed by ext_proc server on response body"));
}

TEST_P(XdsExtProcEnd2endTest, StreamErrorFailureModeFalseFails) {
  auto ext_proc_config = MakeFilterConfigBuilder()
                             .SetFailureModeAllow(false)
                             .SetRequestHeaderMode()
                             .Build();
  SetListenerAndRouteConfiguration(
      balancer_.get(), BuildListenerWithExtProcFilter(ext_proc_config),
      default_route_config_);
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get());
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_THAT(
      req,
      ::testing::Optional(MatchesRequestHeaders(::testing::Contains(
          ::testing::Pair(":path", "/grpc.testing.EchoTestService/Echo")))));
  ext_proc_stream->SendStatus(absl::UnavailableError(
      "Call closed by ext_proc server on request headers"));
  Status status = rpc.GetStatus();
  EXPECT_THAT(status, GrpcStatusIs(
                          StatusCode::INTERNAL,
                          "External processor stream failed: UNAVAILABLE: Call "
                          "closed by ext_proc server on request headers"));
}
//
// ExtProcClientMetrics
//

TEST_P(XdsExtProcEnd2endTest, ExtProcClientHeadersDurationMetric) {
  auto stats_plugin = grpc_core::FakeStatsPluginBuilder()
                          .UseDisabledByDefaultMetrics(true)
                          .BuildAndRegister();
  ResetStub();
  auto ext_proc_config = MakeFilterConfigBuilder()
                             .SetFailureModeAllow(false)
                             .SetRequestHeaderMode()
                             .Build();
  SetListenerAndRouteConfiguration(
      balancer_.get(), BuildListenerWithExtProcFilter(ext_proc_config),
      default_route_config_);
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get());
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_THAT(
      req,
      ::testing::Optional(MatchesRequestHeaders(::testing::Contains(
          ::testing::Pair(":path", "/grpc.testing.EchoTestService/Echo")))));
  ext_proc_stream->SendResponse(MakeRequestHeadersMutationResponse({}));
  Status status = rpc.GetStatus();
  EXPECT_TRUE(status.ok()) << status.error_message();
  const std::string expected_target = absl::StrCat("xds:", kServerName);
  EXPECT_TRUE(
      stats_plugin
          ->GetHistogramValueByName(
              "grpc.client_ext_proc.client_headers_duration", {expected_target})
          .has_value());
}

TEST_P(XdsExtProcEnd2endTest, ExtProcClientHalfCloseDurationMetric) {
  auto stats_plugin = grpc_core::FakeStatsPluginBuilder()
                          .UseDisabledByDefaultMetrics(true)
                          .BuildAndRegister();
  ResetStub();
  auto ext_proc_config = MakeFilterConfigBuilder()
                             .SetFailureModeAllow(false)
                             .SetRequestBodyMode()
                             .Build();
  SetListenerAndRouteConfiguration(
      balancer_.get(), BuildListenerWithExtProcFilter(ext_proc_config),
      default_route_config_);
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get());
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_THAT(req, ::testing::Optional(
                       MatchesRequestBody(EchoRequestMessageIs(kRequestMessage),
                                          /*end_of_stream=*/false)));
  ext_proc_stream->SendResponse(MakeRequestBodyMutationResponse(
      req->request_body().body(), req->request_body().end_of_stream()));
  req = ext_proc_stream->GetNextRequest();
  ASSERT_TRUE(req.has_value());
  EXPECT_THAT(*req, MatchesRequestBody(/*body=*/"", /*end_of_stream=*/true));
  ext_proc_stream->SendResponse(
      MakeRequestBodyMutationResponse(/*body=*/"", /*end_of_stream=*/true));
  Status status = rpc.GetStatus();
  EXPECT_TRUE(status.ok()) << status.error_message();
  const std::string expected_target = absl::StrCat("xds:", kServerName);
  EXPECT_TRUE(stats_plugin
                  ->GetHistogramValueByName(
                      "grpc.client_ext_proc.client_half_close_duration",
                      {expected_target})
                  .has_value());
}

TEST_P(XdsExtProcEnd2endTest, ExtProcServerHeadersDurationMetric) {
  auto stats_plugin = grpc_core::FakeStatsPluginBuilder()
                          .UseDisabledByDefaultMetrics(true)
                          .BuildAndRegister();
  ResetStub();
  auto ext_proc_config = MakeFilterConfigBuilder()
                             .SetFailureModeAllow(false)
                             .SetResponseHeaderMode()
                             .Build();
  SetListenerAndRouteConfiguration(
      balancer_.get(), BuildListenerWithExtProcFilter(ext_proc_config),
      default_route_config_);
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get());
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_THAT(req, ::testing::Optional(MatchesResponseHeaders(::testing::_)));
  ext_proc_stream->SendResponse(MakeResponseHeadersMutationResponse({}));
  Status status = rpc.GetStatus();
  EXPECT_TRUE(status.ok()) << status.error_message();
  const std::string expected_target = absl::StrCat("xds:", kServerName);
  EXPECT_TRUE(
      stats_plugin
          ->GetHistogramValueByName(
              "grpc.client_ext_proc.server_headers_duration", {expected_target})
          .has_value());
}

TEST_P(XdsExtProcEnd2endTest, ExtProcServerTrailersDurationMetric) {
  auto stats_plugin = grpc_core::FakeStatsPluginBuilder()
                          .UseDisabledByDefaultMetrics(true)
                          .BuildAndRegister();
  ResetStub();
  auto ext_proc_config = MakeFilterConfigBuilder()
                             .SetFailureModeAllow(false)
                             .SetResponseTrailerMode()
                             .Build();
  SetListenerAndRouteConfiguration(
      balancer_.get(), BuildListenerWithExtProcFilter(ext_proc_config),
      default_route_config_);
  RpcOptions rpc_options;
  rpc_options.set_echo_metadata(true);
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service_->GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_THAT(req, ::testing::Optional(MatchesResponseTrailers(::testing::_)));
  ext_proc_stream->SendResponse(MakeResponseTrailersMutationResponse({}));
  Status status = rpc.GetStatus();
  EXPECT_TRUE(status.ok()) << status.error_message();
  const std::string expected_target = absl::StrCat("xds:", kServerName);
  EXPECT_TRUE(stats_plugin
                  ->GetHistogramValueByName(
                      "grpc.client_ext_proc.server_trailers_duration",
                      {expected_target})
                  .has_value());
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
  grpc_init();
  const auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}

//
// Copyright 2017 gRPC authors.
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

#include <grpc/credentials.h>
#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/time.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>

#include <deque>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>

#include "src/core/client_channel/backup_poller.h"
#include "src/core/config/config_vars.h"
#include "src/core/credentials/transport/fake/fake_credentials.h"
#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/load_balancing/grpclb/grpclb.h"
#include "src/core/load_balancing/grpclb/grpclb_balancer_addresses.h"
#include "src/core/resolver/endpoint_addresses.h"
#include "src/core/resolver/fake/fake_resolver.h"
#include "src/core/service_config/service_config_impl.h"
#include "src/core/util/crash.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/env.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/sync.h"
#include "src/cpp/server/secure_server_credentials.h"
#include "src/proto/grpc/lb/v1/load_balancer.grpc.pb.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/test_util/port.h"
#include "test/core/test_util/resolve_localhost_ip46.h"
#include "test/core/test_util/test_call_creds.h"
#include "test/core/test_util/test_config.h"
#include "test/cpp/end2end/counted_service.h"
#include "test/cpp/end2end/test_service_impl.h"
#include "test/cpp/util/credentials.h"
#include "test/cpp/util/test_config.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/cleanup/cleanup.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/notification.h"
#include "absl/types/span.h"

// TODO(dgq): Other scenarios in need of testing:
// - Send a serverlist with faulty ip:port addresses (port > 2^16, etc).
// - Test reception of invalid serverlist
// - Test against a non-LB server.
// - Random LB server closing the stream unexpectedly.
//
// Findings from end to end testing to be covered here:
// - Handling of LB servers restart, including reconnection after backing-off
//   retries.
// - Destruction of load balanced channel (and therefore of grpclb instance)
//   while:
//   1) the internal LB call is still active. This should work by virtue
//   of the weak reference the LB call holds. The call should be terminated as
//   part of the grpclb shutdown process.
//   2) the retry timer is active. Again, the weak reference it holds should
//   prevent a premature call to \a glb_destroy.

using grpc::lb::v1::LoadBalancer;
using grpc::lb::v1::LoadBalanceRequest;
using grpc::lb::v1::LoadBalanceResponse;

using grpc_core::SourceLocation;

namespace grpc {
namespace testing {
namespace {

constexpr char kDefaultServiceConfig[] =
    "{\n"
    "  \"loadBalancingConfig\":[\n"
    "    { \"grpclb\":{} }\n"
    "  ]\n"
    "}";

using BackendService = CountedService<TestServiceImpl>;
using BalancerService = CountedService<LoadBalancer::Service>;

const char kCallCredsMdKey[] = "call-creds";
const char kCallCredsMdValue[] = "should not be received by balancer";
const char kRequestMessage[] = "Live long and prosper.";
const absl::string_view kApplicationTargetName = "application_target_name";

// A test user agent string sent by the client only to the grpclb loadbalancer.
// The backend should not see this user-agent string.
constexpr char kGrpclbSpecificUserAgentString[] = "grpc-grpclb-test-user-agent";

class BackendServiceImpl : public BackendService {
 public:
  BackendServiceImpl() {}

  Status Echo(ServerContext* context, const EchoRequest* request,
              EchoResponse* response) override {
    // The backend should not see a test user agent configured at the client
    // using GRPC_ARG_GRPCLB_CHANNEL_ARGS.
    auto it = context->client_metadata().find("user-agent");
    if (it != context->client_metadata().end()) {
      EXPECT_FALSE(it->second.starts_with(kGrpclbSpecificUserAgentString));
    }
    // Backend should receive the call credentials metadata.
    auto call_credentials_entry =
        context->client_metadata().find(kCallCredsMdKey);
    EXPECT_NE(call_credentials_entry, context->client_metadata().end());
    if (call_credentials_entry != context->client_metadata().end()) {
      EXPECT_EQ(call_credentials_entry->second, kCallCredsMdValue);
    }
    IncreaseRequestCount();
    const auto status = TestServiceImpl::Echo(context, request, response);
    IncreaseResponseCount();
    AddClient(context->peer());
    return status;
  }

  void Start() {}

  void Shutdown() {}

  std::set<std::string> clients() {
    grpc_core::MutexLock lock(&clients_mu_);
    return clients_;
  }

 private:
  void AddClient(const std::string& client) {
    grpc_core::MutexLock lock(&clients_mu_);
    clients_.insert(client);
  }

  grpc_core::Mutex clients_mu_;
  std::set<std::string> clients_ ABSL_GUARDED_BY(&clients_mu_);
};

std::string Ip4ToPackedString(const char* ip_str) {
  struct in_addr ip4;
  GRPC_CHECK_EQ(inet_pton(AF_INET, ip_str, &ip4), 1);
  return std::string(reinterpret_cast<const char*>(&ip4), sizeof(ip4));
}

std::string Ip6ToPackedString(const char* ip_str) {
  struct in6_addr ip6;
  GRPC_CHECK_EQ(inet_pton(AF_INET6, ip_str, &ip6), 1);
  return std::string(reinterpret_cast<const char*>(&ip6), sizeof(ip6));
}

struct ClientStats {
  size_t num_calls_started = 0;
  size_t num_calls_finished = 0;
  size_t num_calls_finished_with_client_failed_to_send = 0;
  size_t num_calls_finished_known_received = 0;
  std::map<std::string, size_t> drop_token_counts;

  ClientStats& operator+=(const ClientStats& other) {
    num_calls_started += other.num_calls_started;
    num_calls_finished += other.num_calls_finished;
    num_calls_finished_with_client_failed_to_send +=
        other.num_calls_finished_with_client_failed_to_send;
    num_calls_finished_known_received +=
        other.num_calls_finished_known_received;
    for (const auto& [token, count] : other.drop_token_counts) {
      drop_token_counts[token] += count;
    }
    return *this;
  }

  void Reset() {
    num_calls_started = 0;
    num_calls_finished = 0;
    num_calls_finished_with_client_failed_to_send = 0;
    num_calls_finished_known_received = 0;
    drop_token_counts.clear();
  }
};

class BalancerServiceImpl : public BalancerService {
 public:
  using Stream = ServerReaderWriter<LoadBalanceResponse, LoadBalanceRequest>;

  void Start() {
    {
      grpc_core::MutexLock lock(&mu_);
      shutdown_ = false;
      response_queue_.clear();
    }
    {
      grpc_core::MutexLock lock(&load_report_mu_);
      load_report_queue_.clear();
    }
  }

  void Shutdown() {
    {
      grpc_core::MutexLock lock(&mu_);
      shutdown_ = true;
    }
    ShutdownStream();
    LOG(INFO) << "LB[" << this << "]: shut down";
  }

  void set_client_load_reporting_interval_seconds(int seconds) {
    client_load_reporting_interval_seconds_ = seconds;
  }

  void SendResponse(LoadBalanceResponse response) {
    grpc_core::MutexLock lock(&mu_);
    response_queue_.emplace_back(std::move(response));
    if (response_cond_ != nullptr) response_cond_->SignalAll();
  }

  void ShutdownStream() {
    grpc_core::MutexLock lock(&mu_);
    response_queue_.emplace_back(std::nullopt);
    if (response_cond_ != nullptr) response_cond_->SignalAll();
  }

  std::optional<ClientStats> WaitForLoadReport(absl::Duration timeout) {
    grpc_core::MutexLock lock(&load_report_mu_);
    if (load_report_queue_.empty()) {
      grpc_core::CondVar condition;
      load_report_cond_ = &condition;
      condition.WaitWithTimeout(&load_report_mu_,
                                timeout * grpc_test_slowdown_factor());
      load_report_cond_ = nullptr;
    }
    if (load_report_queue_.empty()) return std::nullopt;
    ClientStats load_report = std::move(load_report_queue_.front());
    load_report_queue_.pop_front();
    return load_report;
  }

  bool WaitForNewStream(size_t prev_seen_count,
                        absl::Duration timeout = absl::Seconds(5)) {
    grpc_core::MutexLock lock(&stream_count_mu_);
    if (stream_count_ == prev_seen_count) {
      grpc_core::CondVar condition;
      stream_count_cond_ = &condition;
      condition.WaitWithTimeout(&stream_count_mu_,
                                timeout * grpc_test_slowdown_factor());
      stream_count_cond_ = nullptr;
    }
    return stream_count_ > prev_seen_count;
  }

  std::vector<std::string> service_names() {
    grpc_core::MutexLock lock(&mu_);
    return service_names_;
  }

  std::set<std::string> clients() {
    grpc_core::MutexLock lock(&clients_mu_);
    return clients_;
  }

 private:
  // Request handler.
  Status BalanceLoad(ServerContext* context, Stream* stream) override {
    LOG(INFO) << "LB[" << this << "]: BalanceLoad";
    {
      grpc_core::MutexLock lock(&mu_);
      if (shutdown_) {
        LOG(INFO) << "LB[" << this << "]: shutdown at stream start";
        return Status::OK;
      }
    }
    IncrementStreamCount();
    AddClient(context->peer());
    // The loadbalancer should see a test user agent because it was
    // specifically configured at the client using
    // GRPC_ARG_GRPCLB_CHANNEL_ARGS
    auto it = context->client_metadata().find("user-agent");
    EXPECT_TRUE(it != context->client_metadata().end());
    if (it != context->client_metadata().end()) {
      EXPECT_THAT(std::string(it->second.data(), it->second.length()),
                  ::testing::StartsWith(kGrpclbSpecificUserAgentString));
    }
    // Balancer shouldn't receive the call credentials metadata.
    EXPECT_EQ(context->client_metadata().find(kCallCredsMdKey),
              context->client_metadata().end());
    // Read initial request.
    LoadBalanceRequest request;
    if (!stream->Read(&request)) {
      LOG(INFO) << "LB[" << this << "]: stream read returned false";
      return Status::OK;
    }
    EXPECT_TRUE(request.has_initial_request());
    {
      grpc_core::MutexLock lock(&mu_);
      service_names_.push_back(request.initial_request().name());
    }
    IncreaseRequestCount();
    LOG(INFO) << "LB[" << this << "]: received initial message '"
              << request.DebugString() << "'";
    // Send initial response.
    LoadBalanceResponse response;
    auto* initial_response = response.mutable_initial_response();
    if (client_load_reporting_interval_seconds_ > 0) {
      initial_response->mutable_client_stats_report_interval()->set_seconds(
          client_load_reporting_interval_seconds_);
    }
    stream->Write(response);
    // Spawn a separate thread to read requests from the client.
    absl::Notification reader_shutdown;
    std::thread reader(std::bind(&BalancerServiceImpl::ReadThread, this, stream,
                                 &reader_shutdown));
    auto thread_cleanup = absl::MakeCleanup([&]() {
      LOG(INFO) << "shutting down reader thread";
      reader_shutdown.Notify();
      LOG(INFO) << "joining reader thread";
      reader.join();
      LOG(INFO) << "joining reader thread complete";
    });
    // Send responses as instructed by the test.
    while (true) {
      auto response = GetNextResponse();
      if (!response.has_value()) {
        context->TryCancel();
        break;
      }
      LOG(INFO) << "LB[" << this
                << "]: Sending response: " << response->DebugString();
      IncreaseResponseCount();
      stream->Write(*response);
    }
    LOG(INFO) << "LB[" << this << "]: done";
    return Status::OK;
  }

  // Reader thread spawned by request handler.
  void ReadThread(Stream* stream, absl::Notification* shutdown) {
    LoadBalanceRequest request;
    while (!shutdown->HasBeenNotified() && stream->Read(&request)) {
      LOG(INFO) << "LB[" << this << "]: received client load report message '"
                << request.DebugString() << "'";
      EXPECT_GT(client_load_reporting_interval_seconds_, 0);
      EXPECT_TRUE(request.has_client_stats());
      ClientStats load_report;
      load_report.num_calls_started =
          request.client_stats().num_calls_started();
      load_report.num_calls_finished =
          request.client_stats().num_calls_finished();
      load_report.num_calls_finished_with_client_failed_to_send =
          request.client_stats()
              .num_calls_finished_with_client_failed_to_send();
      load_report.num_calls_finished_known_received =
          request.client_stats().num_calls_finished_known_received();
      for (const auto& drop_token_count :
           request.client_stats().calls_finished_with_drop()) {
        load_report.drop_token_counts[drop_token_count.load_balance_token()] =
            drop_token_count.num_calls();
      }
      // We need to acquire the lock here in order to prevent the notify_one
      // below from firing before its corresponding wait is executed.
      grpc_core::MutexLock lock(&load_report_mu_);
      load_report_queue_.emplace_back(std::move(load_report));
      if (load_report_cond_ != nullptr) load_report_cond_->Signal();
    }
  }

  // Helper for request handler thread to get the next response to be
  // sent on the stream.  Returns nullopt when the test has requested
  // stream shutdown.
  std::optional<LoadBalanceResponse> GetNextResponse() {
    grpc_core::MutexLock lock(&mu_);
    if (response_queue_.empty()) {
      grpc_core::CondVar condition;
      response_cond_ = &condition;
      condition.Wait(&mu_);
      response_cond_ = nullptr;
    }
    auto response = std::move(response_queue_.front());
    response_queue_.pop_front();
    return response;
  }

  void AddClient(const std::string& client) {
    grpc_core::MutexLock lock(&clients_mu_);
    clients_.insert(client);
  }

  void IncrementStreamCount() {
    grpc_core::MutexLock lock(&stream_count_mu_);
    ++stream_count_;
    if (stream_count_cond_ != nullptr) stream_count_cond_->Signal();
  }

  int client_load_reporting_interval_seconds_ = 0;

  grpc_core::Mutex mu_;
  bool shutdown_ ABSL_GUARDED_BY(&mu_) = false;
  std::vector<std::string> service_names_ ABSL_GUARDED_BY(mu_);
  std::deque<std::optional<LoadBalanceResponse>> response_queue_
      ABSL_GUARDED_BY(mu_);
  grpc_core::CondVar* response_cond_ ABSL_GUARDED_BY(mu_) = nullptr;

  grpc_core::Mutex load_report_mu_;
  grpc_core::CondVar* load_report_cond_ ABSL_GUARDED_BY(load_report_mu_) =
      nullptr;
  std::deque<ClientStats> load_report_queue_ ABSL_GUARDED_BY(load_report_mu_);

  grpc_core::Mutex clients_mu_;
  std::set<std::string> clients_ ABSL_GUARDED_BY(&clients_mu_);

  grpc_core::Mutex stream_count_mu_;
  grpc_core::CondVar* stream_count_cond_ ABSL_GUARDED_BY(&stream_count_mu_) =
      nullptr;
  size_t stream_count_ ABSL_GUARDED_BY(&stream_count_mu_) = 0;
};

class GrpclbEnd2endTest : public ::testing::Test {
 protected:
  template <typename T>
  class ServerThread {
   public:
    template <typename... Args>
    explicit ServerThread(const std::string& type, Args&&... args)
        : port_(grpc_pick_unused_port_or_die()),
          type_(type),
          service_(std::forward<Args>(args)...) {}

    ~ServerThread() { Shutdown(); }

    void Start() {
      LOG(INFO) << "starting " << type_ << " server on port " << port_;
      GRPC_CHECK(!running_);
      running_ = true;
      service_.Start();
      grpc_core::Mutex mu;
      // We need to acquire the lock here in order to prevent the notify_one
      // by ServerThread::Serve from firing before the wait below is hit.
      grpc_core::MutexLock lock(&mu);
      grpc_core::CondVar cond;
      thread_ = std::make_unique<std::thread>(
          std::bind(&ServerThread::Serve, this, &mu, &cond));
      cond.Wait(&mu);
      LOG(INFO) << type_ << " server startup complete";
    }

    void Serve(grpc_core::Mutex* mu, grpc_core::CondVar* cond) {
      // We need to acquire the lock here in order to prevent the notify_one
      // below from firing before its corresponding wait is executed.
      grpc_core::MutexLock lock(mu);
      ServerBuilder builder;
      std::shared_ptr<ServerCredentials> creds(new SecureServerCredentials(
          grpc_fake_transport_security_server_credentials_create()));
      builder.AddListeningPort(grpc_core::LocalIpAndPort(port_), creds);
      builder.RegisterService(&service_);
      server_ = builder.BuildAndStart();
      cond->Signal();
    }

    void Shutdown() {
      if (!running_) return;
      LOG(INFO) << type_ << " about to shutdown";
      service_.Shutdown();
      server_->Shutdown(grpc_timeout_milliseconds_to_deadline(0));
      thread_->join();
      LOG(INFO) << type_ << " shutdown completed";
      running_ = false;
    }

    int port() const { return port_; }

    T& service() { return service_; }

   private:
    const int port_;
    std::string type_;
    T service_;
    std::unique_ptr<Server> server_;
    std::unique_ptr<std::thread> thread_;
    bool running_ = false;
  };

  static void SetUpTestSuite() {
    // Make the backup poller poll very frequently in order to pick up
    // updates from all the subchannels's FDs.
    grpc_core::ConfigVars::Overrides overrides;
    overrides.client_channel_backup_poll_interval_ms = 1;
    grpc_core::ConfigVars::SetOverrides(overrides);
    grpc_init();
  }

  static void TearDownTestSuite() { grpc_shutdown(); }

  void SetUp() override {
    response_generator_ =
        grpc_core::MakeRefCounted<grpc_core::FakeResolverResponseGenerator>();
    balancer_ = CreateAndStartBalancer();
    ResetStub();
  }

  void TearDown() override {
    ShutdownAllBackends();
    balancer_->Shutdown();
  }

  void CreateBackends(size_t num_backends) {
    for (size_t i = 0; i < num_backends; ++i) {
      backends_.emplace_back(
          std::make_unique<ServerThread<BackendServiceImpl>>("backend"));
      backends_.back()->Start();
    }
  }

  void StartAllBackends() {
    for (auto& backend : backends_) backend->Start();
  }

  void StartBackend(size_t index) { backends_[index]->Start(); }

  void ShutdownAllBackends() {
    for (auto& backend : backends_) backend->Shutdown();
  }

  void ShutdownBackend(size_t index) { backends_[index]->Shutdown(); }

  std::unique_ptr<ServerThread<BalancerServiceImpl>> CreateAndStartBalancer() {
    auto balancer =
        std::make_unique<ServerThread<BalancerServiceImpl>>("balancer");
    balancer->Start();
    return balancer;
  }

  void ResetStub(int fallback_timeout_ms = 0,
                 const std::string& expected_targets = "",
                 int subchannel_cache_delay_ms = 0) {
    // Send a separate user agent string for the grpclb load balancer alone.
    grpc_core::ChannelArgs grpclb_channel_args;
    // Set a special user agent string for the grpclb load balancer. It
    // will be verified at the load balancer.
    grpclb_channel_args = grpclb_channel_args.Set(
        GRPC_ARG_PRIMARY_USER_AGENT_STRING, kGrpclbSpecificUserAgentString);
    ChannelArguments args;
    if (fallback_timeout_ms > 0) {
      args.SetGrpclbFallbackTimeout(fallback_timeout_ms *
                                    grpc_test_slowdown_factor());
    }
    args.SetPointer(GRPC_ARG_FAKE_RESOLVER_RESPONSE_GENERATOR,
                    response_generator_.get());
    if (!expected_targets.empty()) {
      args.SetString(GRPC_ARG_FAKE_SECURITY_EXPECTED_TARGETS, expected_targets);
      grpclb_channel_args = grpclb_channel_args.Set(
          GRPC_ARG_FAKE_SECURITY_EXPECTED_TARGETS, expected_targets);
    }
    if (subchannel_cache_delay_ms > 0) {
      args.SetInt(GRPC_ARG_GRPCLB_SUBCHANNEL_CACHE_INTERVAL_MS,
                  subchannel_cache_delay_ms * grpc_test_slowdown_factor());
    }
    static const grpc_arg_pointer_vtable channel_args_vtable = {
        // copy
        [](void* p) -> void* {
          return grpc_channel_args_copy(static_cast<grpc_channel_args*>(p));
        },
        // destroy
        [](void* p) {
          grpc_channel_args_destroy(static_cast<grpc_channel_args*>(p));
        },
        // compare
        [](void* p1, void* p2) {
          return grpc_channel_args_compare(static_cast<grpc_channel_args*>(p1),
                                           static_cast<grpc_channel_args*>(p2));
        },
    };
    // Specify channel args for the channel to the load balancer.
    args.SetPointerWithVtable(
        GRPC_ARG_EXPERIMENTAL_GRPCLB_CHANNEL_ARGS,
        const_cast<grpc_channel_args*>(grpclb_channel_args.ToC().get()),
        &channel_args_vtable);
    // TODO(dgq): templatize tests to run everything using both secure and
    // insecure channel credentials.
    grpc_channel_credentials* channel_creds =
        grpc_fake_transport_security_credentials_create();
    grpc_call_credentials* call_creds = grpc_md_only_test_credentials_create(
        kCallCredsMdKey, kCallCredsMdValue);
    auto creds = std::make_shared<TestCompositeChannelCredentials>(
        channel_creds, call_creds);
    call_creds->Unref();
    channel_creds->Unref();
    channel_ = grpc::CreateCustomChannel(
        absl::StrCat("fake:", kApplicationTargetName), creds, args);
    stub_ = grpc::testing::EchoTestService::NewStub(channel_);
  }

  void ResetBackendCounters() {
    for (auto& backend : backends_) backend->service().ResetCounters();
  }

  std::optional<ClientStats> WaitForLoadReports(
      absl::Duration timeout = absl::Seconds(5)) {
    return balancer_->service().WaitForLoadReport(timeout);
  }

  bool SeenAllBackends(size_t start_index = 0, size_t stop_index = 0) {
    if (stop_index == 0) stop_index = backends_.size();
    for (size_t i = start_index; i < stop_index; ++i) {
      if (backends_[i]->service().request_count() == 0) return false;
    }
    return true;
  }

  void SendRpcAndCount(int* num_total, int* num_ok, int* num_failure,
                       int* num_drops) {
    const Status status = SendRpc();
    if (status.ok()) {
      ++*num_ok;
    } else {
      if (status.error_message() == "drop directed by grpclb balancer") {
        ++*num_drops;
      } else {
        ++*num_failure;
      }
    }
    ++*num_total;
  }

  struct WaitForBackendOptions {
    int timeout_seconds = 10;
    int num_requests_multiple_of = 1;

    WaitForBackendOptions() {}
    WaitForBackendOptions& SetTimeoutSeconds(int seconds) {
      timeout_seconds = seconds;
      return *this;
    }
    WaitForBackendOptions& SetNumRequestsMultipleOf(int multiple) {
      num_requests_multiple_of = multiple;
      return *this;
    }
  };

  std::tuple<int, int, int> WaitForAllBackends(
      size_t start_index = 0, size_t stop_index = 0,
      WaitForBackendOptions options = WaitForBackendOptions(),
      SourceLocation location = SourceLocation()) {
    LOG(INFO) << "Waiting for backends [" << start_index << ", " << stop_index
              << ")";
    const absl::Time deadline =
        absl::Now() +
        absl::Seconds(options.timeout_seconds * grpc_test_slowdown_factor());
    int num_ok = 0;
    int num_failure = 0;
    int num_drops = 0;
    int num_total = 0;
    while (!SeenAllBackends(start_index, stop_index)) {
      absl::Time now = absl::Now();
      EXPECT_LT(now, deadline) << location.file() << ":" << location.line();
      if (now > deadline) break;
      SendRpcAndCount(&num_total, &num_ok, &num_failure, &num_drops);
    }
    while (num_total % options.num_requests_multiple_of != 0) {
      absl::Time now = absl::Now();
      EXPECT_LT(now, deadline) << location.file() << ":" << location.line();
      if (now > deadline) break;
      SendRpcAndCount(&num_total, &num_ok, &num_failure, &num_drops);
    }
    ResetBackendCounters();
    LOG(INFO) << "Performed " << num_total
              << " warm up requests (a multiple of "
              << options.num_requests_multiple_of << ") against the backends. "
              << num_ok << " succeeded, " << num_failure << " failed, "
              << num_drops << " dropped.";
    return std::tuple(num_ok, num_failure, num_drops);
  }

  void WaitForBackend(size_t backend_idx,
                      WaitForBackendOptions options = WaitForBackendOptions(),
                      SourceLocation location = SourceLocation()) {
    WaitForAllBackends(backend_idx, backend_idx + 1, options, location);
  }

  grpc_core::EndpointAddressesList CreateAddressListFromPorts(
      const absl::Span<const int> ports, absl::string_view balancer_name = "") {
    grpc_core::EndpointAddressesList addresses;
    for (int port : ports) {
      absl::StatusOr<grpc_core::URI> lb_uri =
          grpc_core::URI::Parse(grpc_core::LocalIpUri(port));
      GRPC_CHECK_OK(lb_uri);

      grpc_core::ChannelArgs args;
      if (!balancer_name.empty()) {
        args = args.Set(GRPC_ARG_DEFAULT_AUTHORITY, balancer_name);
      }
      addresses.emplace_back(lb_uri->ToString(), args);
    }
    return addresses;
  }

  void SetNextResolutionFromEndpoints(
      grpc_core::EndpointAddressesList balancers,
      grpc_core::EndpointAddressesList backends = {},
      const char* service_config_json = kDefaultServiceConfig) {
    grpc_core::ExecCtx exec_ctx;
    grpc_core::Resolver::Result result;
    result.addresses = std::move(backends);
    result.service_config = grpc_core::ServiceConfigImpl::Create(
        grpc_core::ChannelArgs(), service_config_json);
    GRPC_CHECK_OK(result.service_config);
    result.args = grpc_core::SetGrpcLbBalancerAddresses(
        grpc_core::ChannelArgs(), std::move(balancers));
    response_generator_->SetResponseSynchronously(std::move(result));
  }

  void SetNextResolution(
      const absl::Span<const int> balancer_ports,
      const absl::Span<const int> backend_ports = {},
      const char* service_config_json = kDefaultServiceConfig) {
    SetNextResolutionFromEndpoints(CreateAddressListFromPorts(balancer_ports),
                                   CreateAddressListFromPorts(backend_ports),
                                   service_config_json);
  }

  void SetNextResolutionDefaultBalancer(
      const char* service_config_json = kDefaultServiceConfig) {
    SetNextResolution({balancer_->port()}, {}, service_config_json);
  }

  std::vector<int> GetBackendPorts(size_t start_index = 0,
                                   size_t stop_index = 0) const {
    if (stop_index == 0) stop_index = backends_.size();
    std::vector<int> backend_ports;
    for (size_t i = start_index; i < stop_index; ++i) {
      backend_ports.push_back(backends_[i]->port());
    }
    return backend_ports;
  }

  void SendBalancerResponse(LoadBalanceResponse response) {
    balancer_->service().SendResponse(std::move(response));
  }

  LoadBalanceResponse BuildResponseForBackends(
      const std::vector<int>& backend_ports,
      const std::map<std::string, size_t>& drop_token_counts) {
    LoadBalanceResponse response;
    for (const auto& [token, count] : drop_token_counts) {
      for (size_t i = 0; i < count; ++i) {
        auto* server = response.mutable_server_list()->add_servers();
        server->set_drop(true);
        server->set_load_balance_token(token);
      }
    }
    for (const int& backend_port : backend_ports) {
      auto* server = response.mutable_server_list()->add_servers();
      server->set_ip_address(grpc_core::RunningWithIPv6Only()
                                 ? Ip6ToPackedString("::1")
                                 : Ip4ToPackedString("127.0.0.1"));
      server->set_port(backend_port);
      static int token_count = 0;
      server->set_load_balance_token(
          absl::StrFormat("token%03d", ++token_count));
    }
    return response;
  }

  Status SendRpc(EchoResponse* response = nullptr, int timeout_ms = 3000,
                 bool wait_for_ready = false,
                 const Status& expected_status = Status::OK) {
    const bool local_response = (response == nullptr);
    if (local_response) response = new EchoResponse;
    EchoRequest request;
    request.set_message(kRequestMessage);
    if (!expected_status.ok()) {
      auto* error = request.mutable_param()->mutable_expected_error();
      error->set_code(expected_status.error_code());
      error->set_error_message(expected_status.error_message());
    }
    ClientContext context;
    context.set_deadline(grpc_timeout_milliseconds_to_deadline(timeout_ms));
    if (wait_for_ready) context.set_wait_for_ready(true);
    Status status = stub_->Echo(&context, request, response);
    if (local_response) delete response;
    return status;
  }

  void CheckRpcSendOk(const size_t times = 1, const int timeout_ms = 3000,
                      bool wait_for_ready = false) {
    for (size_t i = 0; i < times; ++i) {
      EchoResponse response;
      const Status status = SendRpc(&response, timeout_ms, wait_for_ready);
      EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                               << " message=" << status.error_message();
      EXPECT_EQ(response.message(), kRequestMessage);
    }
  }

  void CheckRpcSendFailure() {
    const Status status = SendRpc();
    EXPECT_FALSE(status.ok());
  }

  std::shared_ptr<Channel> channel_;
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub_;
  std::vector<std::unique_ptr<ServerThread<BackendServiceImpl>>> backends_;
  std::unique_ptr<ServerThread<BalancerServiceImpl>> balancer_;
  grpc_core::RefCountedPtr<grpc_core::FakeResolverResponseGenerator>
      response_generator_;
};

TEST_F(GrpclbEnd2endTest, Vanilla) {
  const size_t kNumBackends = 3;
  const size_t kNumRpcsPerAddress = 100;
  CreateBackends(kNumBackends);
  SendBalancerResponse(BuildResponseForBackends(GetBackendPorts(), {}));
  SetNextResolutionDefaultBalancer();
  // Make sure that trying to connect works without a call.
  channel_->GetState(true /* try_to_connect */);
  // We need to wait for all backends to come online.
  WaitForAllBackends();
  // Send kNumRpcsPerAddress RPCs per server.
  CheckRpcSendOk(kNumRpcsPerAddress * kNumBackends);
  // Each backend should have gotten 100 requests.
  for (size_t i = 0; i < backends_.size(); ++i) {
    EXPECT_EQ(kNumRpcsPerAddress, backends_[i]->service().request_count());
  }
  // The balancer got a single request.
  EXPECT_EQ(1U, balancer_->service().request_count());
  // and sent a single response.
  EXPECT_EQ(1U, balancer_->service().response_count());
  // Check LB policy name for the channel.
  EXPECT_EQ("grpclb", channel_->GetLoadBalancingPolicyName());
}

TEST_F(GrpclbEnd2endTest, SubchannelCaching) {
  CreateBackends(3);
  ResetStub(/*fallback_timeout_ms=*/0, /*expected_targets=*/"",
            /*subchannel_cache_delay_ms=*/1500);
  SetNextResolutionDefaultBalancer();
  // Initially send backends 0 and 1.
  SendBalancerResponse(BuildResponseForBackends(GetBackendPorts(0, 2), {}));
  WaitForAllBackends(0, 2);
  // Now remove backends 0 and 1 and add backend 2.
  SendBalancerResponse(BuildResponseForBackends(GetBackendPorts(2), {}));
  WaitForBackend(2);
  // Now re-add backend 1.
  SendBalancerResponse(BuildResponseForBackends(GetBackendPorts(1), {}));
  WaitForBackend(1);
  // Backend 1 should never have lost its connection from the client.
  EXPECT_EQ(1UL, backends_[1]->service().clients().size());
  // The balancer got a single request.
  EXPECT_EQ(1U, balancer_->service().request_count());
  // And sent 3 responses.
  EXPECT_EQ(3U, balancer_->service().response_count());
}

TEST_F(GrpclbEnd2endTest, ReturnServerStatus) {
  CreateBackends(1);
  SetNextResolutionDefaultBalancer();
  SendBalancerResponse(BuildResponseForBackends(GetBackendPorts(), {}));
  // We need to wait for all backends to come online.
  WaitForAllBackends();
  // Send a request that the backend will fail, and make sure we get
  // back the right status.
  Status expected(StatusCode::INVALID_ARGUMENT, "He's dead, Jim!");
  Status actual = SendRpc(/*response=*/nullptr, /*timeout_ms=*/3000,
                          /*wait_for_ready=*/false, expected);
  EXPECT_EQ(actual.error_code(), expected.error_code());
  EXPECT_EQ(actual.error_message(), expected.error_message());
}

TEST_F(GrpclbEnd2endTest, SelectGrpclbWithMigrationServiceConfig) {
  CreateBackends(1);
  SetNextResolutionDefaultBalancer(
      "{\n"
      "  \"loadBalancingConfig\":[\n"
      "    { \"does_not_exist\":{} },\n"
      "    { \"grpclb\":{} }\n"
      "  ]\n"
      "}");
  SendBalancerResponse(BuildResponseForBackends(GetBackendPorts(), {}));
  CheckRpcSendOk(1, 3000 /* timeout_ms */, true /* wait_for_ready */);
  // The balancer got a single request.
  EXPECT_EQ(1U, balancer_->service().request_count());
  // and sent a single response.
  EXPECT_EQ(1U, balancer_->service().response_count());
  // Check LB policy name for the channel.
  EXPECT_EQ("grpclb", channel_->GetLoadBalancingPolicyName());
}

TEST_F(GrpclbEnd2endTest,
       SelectGrpclbWithMigrationServiceConfigAndNoAddresses) {
  const int kFallbackTimeoutMs = 200;
  ResetStub(kFallbackTimeoutMs);
  SetNextResolution({}, {},
                    "{\n"
                    "  \"loadBalancingConfig\":[\n"
                    "    { \"does_not_exist\":{} },\n"
                    "    { \"grpclb\":{} }\n"
                    "  ]\n"
                    "}");
  // Try to connect.
  EXPECT_EQ(GRPC_CHANNEL_IDLE, channel_->GetState(true));
  // Should go into state TRANSIENT_FAILURE when we enter fallback mode.
  const gpr_timespec deadline = grpc_timeout_seconds_to_deadline(1);
  grpc_connectivity_state state;
  while ((state = channel_->GetState(false)) !=
         GRPC_CHANNEL_TRANSIENT_FAILURE) {
    ASSERT_TRUE(channel_->WaitForStateChange(state, deadline));
  }
  // Check LB policy name for the channel.
  EXPECT_EQ("grpclb", channel_->GetLoadBalancingPolicyName());
}

TEST_F(GrpclbEnd2endTest, UsePickFirstChildPolicy) {
  const size_t kNumBackends = 2;
  const size_t kNumRpcs = kNumBackends * 2;
  CreateBackends(kNumBackends);
  SetNextResolutionDefaultBalancer(
      "{\n"
      "  \"loadBalancingConfig\":[\n"
      "    { \"grpclb\":{\n"
      "      \"childPolicy\":[\n"
      "        { \"pick_first\":{} }\n"
      "      ]\n"
      "    } }\n"
      "  ]\n"
      "}");
  SendBalancerResponse(BuildResponseForBackends(GetBackendPorts(), {}));
  CheckRpcSendOk(kNumRpcs, 3000 /* timeout_ms */, true /* wait_for_ready */);
  // Check that all requests went to one backend.  This verifies that we
  // used pick_first instead of round_robin as the child policy.
  bool found = false;
  for (size_t i = 0; i < backends_.size(); ++i) {
    if (backends_[i]->service().request_count() > 0) {
      LOG(INFO) << "backend " << i << " saw traffic";
      EXPECT_EQ(backends_[i]->service().request_count(), kNumRpcs)
          << "backend " << i;
      EXPECT_FALSE(found) << "multiple backends saw traffic";
      found = true;
    }
  }
  EXPECT_TRUE(found) << "no backends saw traffic";
  // The balancer got a single request.
  EXPECT_EQ(1U, balancer_->service().request_count());
  // and sent a single response.
  EXPECT_EQ(1U, balancer_->service().response_count());
  // Check LB policy name for the channel.
  EXPECT_EQ("grpclb", channel_->GetLoadBalancingPolicyName());
}

TEST_F(GrpclbEnd2endTest, SwapChildPolicy) {
  const size_t kNumBackends = 2;
  const size_t kNumRpcs = kNumBackends * 2;
  CreateBackends(kNumBackends);
  SetNextResolutionDefaultBalancer(
      "{\n"
      "  \"loadBalancingConfig\":[\n"
      "    { \"grpclb\":{\n"
      "      \"childPolicy\":[\n"
      "        { \"pick_first\":{} }\n"
      "      ]\n"
      "    } }\n"
      "  ]\n"
      "}");
  SendBalancerResponse(BuildResponseForBackends(GetBackendPorts(), {}));
  CheckRpcSendOk(kNumRpcs, 3000 /* timeout_ms */, true /* wait_for_ready */);
  // Check that all requests went to one backend.  This verifies that we
  // used pick_first instead of round_robin as the child policy.
  bool found = false;
  for (size_t i = 0; i < backends_.size(); ++i) {
    if (backends_[i]->service().request_count() > 0) {
      LOG(INFO) << "backend " << i << " saw traffic";
      EXPECT_EQ(backends_[i]->service().request_count(), kNumRpcs)
          << "backend " << i;
      EXPECT_FALSE(found) << "multiple backends saw traffic";
      found = true;
    }
  }
  EXPECT_TRUE(found) << "no backends saw traffic";
  // Send new resolution that removes child policy from service config.
  SetNextResolutionDefaultBalancer();
  // We should now be using round_robin, which will send traffic to all
  // backends.
  WaitForAllBackends();
  // The balancer got a single request.
  EXPECT_EQ(1U, balancer_->service().request_count());
  // and sent a single response.
  EXPECT_EQ(1U, balancer_->service().response_count());
  // Check LB policy name for the channel.
  EXPECT_EQ("grpclb", channel_->GetLoadBalancingPolicyName());
}

TEST_F(GrpclbEnd2endTest, SameBackendListedMultipleTimes) {
  CreateBackends(1);
  SetNextResolutionDefaultBalancer();
  // Same backend listed twice.
  std::vector<int> ports;
  ports.push_back(backends_[0]->port());
  ports.push_back(backends_[0]->port());
  const size_t kNumRpcsPerAddress = 10;
  SendBalancerResponse(BuildResponseForBackends(ports, {}));
  // We need to wait for the backend to come online.
  WaitForBackend(0);
  // Send kNumRpcsPerAddress RPCs per server.
  CheckRpcSendOk(kNumRpcsPerAddress * ports.size());
  // Backend should have gotten 20 requests.
  EXPECT_EQ(kNumRpcsPerAddress * 2, backends_[0]->service().request_count());
  // And they should have come from a single client port, because of
  // subchannel sharing.
  EXPECT_EQ(1UL, backends_[0]->service().clients().size());
}

TEST_F(GrpclbEnd2endTest, InitiallyEmptyServerlist) {
  CreateBackends(1);
  SetNextResolutionDefaultBalancer();
  // First response is an empty serverlist.  RPCs should fail.
  SendBalancerResponse(LoadBalanceResponse());
  CheckRpcSendFailure();
  // Now send a non-empty serverlist.
  SendBalancerResponse(BuildResponseForBackends(GetBackendPorts(), {}));
  CheckRpcSendOk(1, /*timeout_ms=*/3000, /*wait_for_ready=*/true);
  // The balancer got a single request.
  EXPECT_EQ(1U, balancer_->service().request_count());
  // and sent two responses.
  EXPECT_EQ(2U, balancer_->service().response_count());
}

TEST_F(GrpclbEnd2endTest, AllServersUnreachableFailFast) {
  SetNextResolutionDefaultBalancer();
  const size_t kNumUnreachableServers = 5;
  std::vector<int> ports;
  for (size_t i = 0; i < kNumUnreachableServers; ++i) {
    ports.push_back(grpc_pick_unused_port_or_die());
  }
  SendBalancerResponse(BuildResponseForBackends(ports, {}));
  const Status status = SendRpc();
  // The error shouldn't be DEADLINE_EXCEEDED.
  EXPECT_EQ(StatusCode::UNAVAILABLE, status.error_code());
  // The balancer got a single request.
  EXPECT_EQ(1U, balancer_->service().request_count());
  // and sent a single response.
  EXPECT_EQ(1U, balancer_->service().response_count());
}

TEST_F(GrpclbEnd2endTest, Fallback) {
  const size_t kNumBackends = 4;
  const size_t kNumBackendsInResolution = kNumBackends / 2;
  CreateBackends(kNumBackends);
  // Inject resolver result that contains the fallback backends.
  SetNextResolution({balancer_->port()},
                    GetBackendPorts(0, kNumBackendsInResolution));
  // Balancer has not sent a serverlist, so we should use fallback.
  // Wait until all the fallback backends are reachable.
  WaitForAllBackends(0, kNumBackendsInResolution,
                     WaitForBackendOptions().SetTimeoutSeconds(20));
  // Send serverlist.
  SendBalancerResponse(BuildResponseForBackends(
      GetBackendPorts(/*start_index=*/kNumBackendsInResolution), {}));
  // Now we should be using the backends from the balancer.
  WaitForAllBackends(kNumBackendsInResolution);
  // The balancer got a single request.
  EXPECT_EQ(1U, balancer_->service().request_count());
  // and sent a single response.
  EXPECT_EQ(1U, balancer_->service().response_count());
}

TEST_F(GrpclbEnd2endTest, FallbackUpdate) {
  const size_t kNumBackends = 6;
  const size_t kNumBackendsInResolution = kNumBackends / 3;
  const size_t kNumBackendsInResolutionUpdate = kNumBackends / 3;
  ResetStub(/*fallback_timeout_ms=*/500);
  CreateBackends(kNumBackends);
  // Inject resolver result with fallback addresses.
  SetNextResolution({balancer_->port()},
                    GetBackendPorts(0, kNumBackendsInResolution));
  // Balancer has not sent a serverlist, so we should use fallback.
  // Wait until all the fallback backends are reachable.
  WaitForAllBackends(0, kNumBackendsInResolution);
  // Now send a resolver result with a different set of backend addresses.
  SetNextResolution({balancer_->port()},
                    GetBackendPorts(kNumBackendsInResolution,
                                    kNumBackendsInResolution +
                                        kNumBackendsInResolutionUpdate));
  // Wait until the new fallback backends are reachable.
  WaitForAllBackends(kNumBackendsInResolution,
                     kNumBackendsInResolution + kNumBackendsInResolutionUpdate);
  // Send non-empty serverlist.
  SendBalancerResponse(
      BuildResponseForBackends(GetBackendPorts(kNumBackendsInResolution +
                                               kNumBackendsInResolutionUpdate),
                               {}));
  // Wait for backends from balancer to be seen.
  WaitForAllBackends(kNumBackendsInResolution + kNumBackendsInResolutionUpdate);
  // The balancer got a single request.
  EXPECT_EQ(1U, balancer_->service().request_count());
  // and sent a single response.
  EXPECT_EQ(1U, balancer_->service().response_count());
}

TEST_F(GrpclbEnd2endTest,
       FallbackAfterStartupLoseContactWithBalancerThenBackends) {
  // First two backends are fallback, last two are pointed to by balancer.
  const size_t kNumBackends = 4;
  const size_t kNumFallbackBackends = 2;
  const size_t kNumBalancerBackends = kNumBackends - kNumFallbackBackends;
  CreateBackends(kNumBackends);
  SetNextResolution({balancer_->port()},
                    GetBackendPorts(0, kNumFallbackBackends));
  SendBalancerResponse(
      BuildResponseForBackends(GetBackendPorts(kNumFallbackBackends), {}));
  // Try to connect.
  WaitForAllBackends(kNumFallbackBackends /* start_index */);
  // Stop balancer.  RPCs should continue going to backends from balancer.
  balancer_->Shutdown();
  CheckRpcSendOk(100 * kNumBalancerBackends);
  for (size_t i = kNumFallbackBackends; i < backends_.size(); ++i) {
    EXPECT_EQ(100UL, backends_[i]->service().request_count());
  }
  // Stop backends from balancer.  This should put us in fallback mode.
  for (size_t i = kNumFallbackBackends; i < backends_.size(); ++i) {
    ShutdownBackend(i);
  }
  WaitForAllBackends(0, kNumFallbackBackends);
  // Restart the backends from the balancer.  We should *not* start
  // sending traffic back to them at this point.
  for (size_t i = kNumFallbackBackends; i < backends_.size(); ++i) {
    StartBackend(i);
  }
  CheckRpcSendOk(100 * kNumBalancerBackends);
  for (size_t i = 0; i < kNumFallbackBackends; ++i) {
    EXPECT_EQ(100UL, backends_[i]->service().request_count());
  }
  // Now start the balancer again.  This should cause us to exit
  // fallback mode.
  balancer_->Start();
  SendBalancerResponse(
      BuildResponseForBackends(GetBackendPorts(kNumFallbackBackends), {}));
  WaitForAllBackends(kNumFallbackBackends);
}

TEST_F(GrpclbEnd2endTest,
       FallbackAfterStartupLoseContactWithBackendsThenBalancer) {
  // First two backends are fallback, last two are pointed to by balancer.
  const size_t kNumBackends = 4;
  const size_t kNumFallbackBackends = 2;
  const size_t kNumBalancerBackends = kNumBackends - kNumFallbackBackends;
  CreateBackends(kNumBackends);
  SetNextResolution({balancer_->port()},
                    GetBackendPorts(0, kNumFallbackBackends));
  SendBalancerResponse(
      BuildResponseForBackends(GetBackendPorts(kNumFallbackBackends), {}));
  // Try to connect.
  WaitForAllBackends(kNumFallbackBackends);
  // Stop backends from balancer.  Since we are still in contact with
  // the balancer at this point, RPCs should be failing.
  for (size_t i = kNumFallbackBackends; i < backends_.size(); ++i) {
    ShutdownBackend(i);
  }
  CheckRpcSendFailure();
  // Stop balancer.  This should put us in fallback mode.
  balancer_->Shutdown();
  WaitForAllBackends(0, kNumFallbackBackends);
  // Restart the backends from the balancer.  We should *not* start
  // sending traffic back to them at this point (although the behavior
  // in xds may be different).
  for (size_t i = kNumFallbackBackends; i < backends_.size(); ++i) {
    StartBackend(i);
  }
  CheckRpcSendOk(100 * kNumBalancerBackends);
  for (size_t i = 0; i < kNumFallbackBackends; ++i) {
    EXPECT_EQ(100UL, backends_[i]->service().request_count());
  }
  // Now start the balancer again.  This should cause us to exit
  // fallback mode.
  balancer_->Start();
  SendBalancerResponse(
      BuildResponseForBackends(GetBackendPorts(kNumFallbackBackends), {}));
  WaitForAllBackends(kNumFallbackBackends);
}

TEST_F(GrpclbEnd2endTest, FallbackEarlyWhenBalancerChannelFails) {
  const int kFallbackTimeoutMs = 10000;
  ResetStub(kFallbackTimeoutMs);
  CreateBackends(1);
  // Return an unreachable balancer and one fallback backend.
  SetNextResolution({grpc_pick_unused_port_or_die()}, GetBackendPorts());
  // Send RPC with deadline less than the fallback timeout and make sure it
  // succeeds.
  CheckRpcSendOk(/* times */ 1, /* timeout_ms */ 3000,
                 /* wait_for_ready */ false);
}

TEST_F(GrpclbEnd2endTest, FallbackEarlyWhenBalancerCallFails) {
  const int kFallbackTimeoutMs = 10000;
  ResetStub(kFallbackTimeoutMs);
  CreateBackends(1);
  // Return one balancer and one fallback backend.
  SetNextResolution({balancer_->port()}, GetBackendPorts());
  // Balancer drops call without sending a serverlist.
  balancer_->service().ShutdownStream();
  // Send RPC with deadline less than the fallback timeout and make sure it
  // succeeds.
  CheckRpcSendOk(/* times */ 1, /* timeout_ms */ 3000,
                 /* wait_for_ready */ false);
}

TEST_F(GrpclbEnd2endTest, FallbackControlledByBalancerBeforeFirstServerlist) {
  const int kFallbackTimeoutMs = 10000;
  ResetStub(kFallbackTimeoutMs);
  CreateBackends(1);
  // Return one balancer and one fallback backend.
  SetNextResolution({balancer_->port()}, GetBackendPorts());
  // Balancer explicitly tells client to fallback.
  LoadBalanceResponse response;
  response.mutable_fallback_response();
  SendBalancerResponse(std::move(response));
  // Send RPC with deadline less than the fallback timeout and make sure it
  // succeeds.
  CheckRpcSendOk(/* times */ 1, /* timeout_ms */ 3000,
                 /* wait_for_ready */ false);
}

TEST_F(GrpclbEnd2endTest, FallbackControlledByBalancerAfterFirstServerlist) {
  CreateBackends(2);
  // Return one balancer and one fallback backend (backend 0).
  SetNextResolution({balancer_->port()}, {backends_[0]->port()});
  // Balancer sends a serverlist pointing to backend 1.
  SendBalancerResponse(BuildResponseForBackends({backends_[1]->port()}, {}));
  WaitForBackend(1);
  // Balancer tells client to fall back.
  LoadBalanceResponse fallback_response;
  fallback_response.mutable_fallback_response();
  SendBalancerResponse(std::move(fallback_response));
  WaitForBackend(0);
  // Balancer sends a new serverlist, so client exits fallback.
  SendBalancerResponse(BuildResponseForBackends({backends_[1]->port()}, {}));
  WaitForBackend(1);
}

TEST_F(GrpclbEnd2endTest, BackendsRestart) {
  CreateBackends(2);
  SetNextResolutionDefaultBalancer();
  SendBalancerResponse(BuildResponseForBackends(GetBackendPorts(), {}));
  WaitForAllBackends();
  // Stop backends.  RPCs should fail.
  ShutdownAllBackends();
  CheckRpcSendFailure();
  // Restart backends.  RPCs should start succeeding again.
  StartAllBackends();
  CheckRpcSendOk(1 /* times */, 3000 /* timeout_ms */,
                 true /* wait_for_ready */);
  // The balancer got a single request.
  EXPECT_EQ(1U, balancer_->service().request_count());
  // and sent a single response.
  EXPECT_EQ(1U, balancer_->service().response_count());
}

TEST_F(GrpclbEnd2endTest, ServiceNameFromLbPolicyConfig) {
  constexpr char kServiceConfigWithTarget[] =
      "{\n"
      "  \"loadBalancingConfig\":[\n"
      "    { \"grpclb\":{\n"
      "      \"serviceName\":\"test_service\"\n"
      "    }}\n"
      "  ]\n"
      "}";
  SetNextResolutionDefaultBalancer(kServiceConfigWithTarget);
  CreateBackends(1);
  SendBalancerResponse(BuildResponseForBackends(GetBackendPorts(), {}));
  WaitForAllBackends();
  EXPECT_EQ(balancer_->service().service_names().back(), "test_service");
}

TEST_F(GrpclbEnd2endTest,
       NewBalancerAddressNotUsedIfOriginalStreamDoesNotFail) {
  CreateBackends(3);
  // Default balancer sends backend 0.
  SendBalancerResponse(BuildResponseForBackends({backends_[0]->port()}, {}));
  // Second balancer sends backend 1.
  auto balancer2 = CreateAndStartBalancer();
  balancer2->service().SendResponse(
      BuildResponseForBackends({backends_[1]->port()}, {}));
  // Initially, the channel uses the default balancer.
  SetNextResolutionDefaultBalancer();
  WaitForBackend(0);
  // Send 10 requests.
  LOG(INFO) << "========= BEFORE FIRST BATCH ==========";
  CheckRpcSendOk(10);
  LOG(INFO) << "========= DONE WITH FIRST BATCH ==========";
  // All 10 requests should have gone to the first backend.
  EXPECT_EQ(10U, backends_[0]->service().request_count());
  EXPECT_EQ(0U, backends_[1]->service().request_count());
  EXPECT_EQ(0U, backends_[2]->service().request_count());
  // Balancer 0 got a single request and sent a single response.
  EXPECT_EQ(1U, balancer_->service().request_count());
  EXPECT_EQ(1U, balancer_->service().response_count());
  EXPECT_EQ(0U, balancer2->service().request_count());
  EXPECT_EQ(0U, balancer2->service().response_count());
  // Now tell the channel to use balancer 2.  However, the stream to the
  // default balancer is not terminated, so the client will continue to
  // use it.
  LOG(INFO) << "========= ABOUT TO UPDATE 1 ==========";
  SetNextResolution({balancer2->port()});
  LOG(INFO) << "========= UPDATE 1 DONE ==========";
  // Now the default balancer sends backend 2.
  SendBalancerResponse(BuildResponseForBackends({backends_[2]->port()}, {}));
  WaitForBackend(2);
}

// Send an update with the same set of LBs as the previous one in order to
// verify that the LB channel inside grpclb keeps the initial connection (which
// by definition is also present in the update).
TEST_F(GrpclbEnd2endTest,
       UpdatedBalancerAddressesWithSameAddressDoesNotBreakConnection) {
  CreateBackends(2);
  // Default balancer points to backend 0.
  SendBalancerResponse(BuildResponseForBackends({backends_[0]->port()}, {}));
  // Second balancer points to backend 1.
  auto balancer2 = CreateAndStartBalancer();
  balancer2->service().SendResponse(
      BuildResponseForBackends({backends_[1]->port()}, {}));
  // Send both balancer addresses.
  SetNextResolution({balancer_->port(), balancer2->port()});
  // Wait until the first backend is ready.
  WaitForBackend(0);
  // Send 10 requests.
  LOG(INFO) << "========= BEFORE FIRST BATCH ==========";
  CheckRpcSendOk(10);
  LOG(INFO) << "========= DONE WITH FIRST BATCH ==========";
  // All 10 requests should have gone to the first backend.
  EXPECT_EQ(10U, backends_[0]->service().request_count());
  EXPECT_EQ(0U, backends_[1]->service().request_count());
  // Balancer 0 got a single request and sent a single response.
  EXPECT_EQ(1U, balancer_->service().request_count());
  EXPECT_EQ(1U, balancer_->service().response_count());
  EXPECT_EQ(0U, balancer2->service().request_count());
  EXPECT_EQ(0U, balancer2->service().response_count());
  // Send another address list with the same list of balancers.
  LOG(INFO) << "========= ABOUT TO UPDATE 1 ==========";
  SetNextResolution({balancer_->port(), balancer2->port()});
  LOG(INFO) << "========= UPDATE 1 DONE ==========";
  // Shut down the balancer stream to force the client to create a new one.
  // The new stream should go to the default balancer, since the
  // underlying connection should not have been broken.
  LOG(INFO) << "========= SHUTTING DOWN BALANCER CALL ==========";
  balancer_->service().ShutdownStream();
  LOG(INFO) << "========= DONE SHUTTING DOWN BALANCER CALL ==========";
  // Wait until client has created a new balancer stream.
  EXPECT_TRUE(balancer_->service().WaitForNewStream(1));
  // Make sure there was only one client connection seen by the balancer.
  EXPECT_EQ(1UL, balancer_->service().clients().size());
}

TEST_F(GrpclbEnd2endTest, BalancerDiesThenSwitchToNewBalancer) {
  CreateBackends(2);
  // Default balancer sends backend 0.
  SendBalancerResponse(BuildResponseForBackends({backends_[0]->port()}, {}));
  // Balancer 2 sends backend 1.
  auto balancer2 = CreateAndStartBalancer();
  balancer2->service().SendResponse(
      BuildResponseForBackends({backends_[1]->port()}, {}));
  // Channel initially uses default balancer and therefore backend 0.
  SetNextResolutionDefaultBalancer();
  WaitForBackend(0);
  // Default balancer got a single request.
  EXPECT_EQ(1U, balancer_->service().request_count());
  EXPECT_EQ(1U, balancer_->service().response_count());
  EXPECT_EQ(0U, balancer2->service().request_count());
  EXPECT_EQ(0U, balancer2->service().response_count());
  // Send 10 RPCs.
  LOG(INFO) << "========= BEFORE FIRST BATCH ==========";
  CheckRpcSendOk(10);
  LOG(INFO) << "========= DONE WITH FIRST BATCH ==========";
  // All 10 requests should have gone to the first backend.
  EXPECT_EQ(10U, backends_[0]->service().request_count());
  EXPECT_EQ(0U, backends_[1]->service().request_count());
  // Kill default balancer.
  LOG(INFO) << "********** ABOUT TO KILL BALANCER *************";
  balancer_->Shutdown();
  LOG(INFO) << "********** KILLED BALANCER *************";
  // Channel should continue using the last backend it saw from the
  // balancer before the balancer died.
  LOG(INFO) << "========= BEFORE SECOND BATCH ==========";
  CheckRpcSendOk(10);
  LOG(INFO) << "========= DONE WITH SECOND BATCH ==========";
  // All 10 requests should again have gone to the first backend.
  EXPECT_EQ(20U, backends_[0]->service().request_count());
  EXPECT_EQ(0U, backends_[1]->service().request_count());
  // Tell channel to start using balancer 2.
  LOG(INFO) << "========= ABOUT TO UPDATE 1 ==========";
  SetNextResolution({balancer2->port()});
  LOG(INFO) << "========= UPDATE 1 DONE ==========";
  // Channel should start using backend 1.
  WaitForBackend(1);
  // This is serviced by the updated RR policy
  LOG(INFO) << "========= BEFORE THIRD BATCH ==========";
  CheckRpcSendOk(10);
  LOG(INFO) << "========= DONE WITH THIRD BATCH ==========";
  // All 10 requests should have gone to the second backend.
  EXPECT_EQ(0U, backends_[0]->service().request_count());
  EXPECT_EQ(10U, backends_[1]->service().request_count());
  // Both balancers should have gotten one request and sent one response.
  EXPECT_EQ(1U, balancer_->service().request_count());
  EXPECT_EQ(1U, balancer_->service().response_count());
  EXPECT_EQ(1U, balancer2->service().request_count());
  EXPECT_EQ(1U, balancer2->service().response_count());
}

TEST_F(GrpclbEnd2endTest, ReresolveDeadBackendWhileInFallback) {
  ResetStub(/*fallback_timeout_ms=*/500);
  CreateBackends(2);
  // The first resolution contains the addresses of a balancer that never
  // responds, and a fallback backend.
  SetNextResolution({balancer_->port()}, {backends_[0]->port()});
  // Start servers and send 10 RPCs per server.
  LOG(INFO) << "========= BEFORE FIRST BATCH ==========";
  CheckRpcSendOk(10);
  LOG(INFO) << "========= DONE WITH FIRST BATCH ==========";
  // All 10 requests should have gone to the fallback backend.
  EXPECT_EQ(10U, backends_[0]->service().request_count());
  // Kill backend 0.
  LOG(INFO) << "********** ABOUT TO KILL BACKEND 0 *************";
  backends_[0]->Shutdown();
  LOG(INFO) << "********** KILLED BACKEND 0 *************";
  // This should trigger re-resolution.
  EXPECT_TRUE(response_generator_->WaitForReresolutionRequest(
      absl::Seconds(5 * grpc_test_slowdown_factor())));
  // The re-resolution result will contain the addresses of the same balancer
  // and a new fallback backend.
  SetNextResolution({balancer_->port()}, {backends_[1]->port()});
  // Wait until re-resolution has been seen, as signaled by the second backend
  // receiving a request.
  WaitForBackend(1);
  LOG(INFO) << "========= BEFORE SECOND BATCH ==========";
  CheckRpcSendOk(10);
  LOG(INFO) << "========= DONE WITH SECOND BATCH ==========";
  // All 10 requests should have gone to the second backend.
  EXPECT_EQ(10U, backends_[1]->service().request_count());
  EXPECT_EQ(1U, balancer_->service().request_count());
  EXPECT_EQ(0U, balancer_->service().response_count());
}

TEST_F(GrpclbEnd2endTest, ReresolveWhenBalancerCallFails) {
  CreateBackends(2);
  // Default balancer sends backend 0.
  SendBalancerResponse(BuildResponseForBackends({backends_[0]->port()}, {}));
  // Balancer 2 sends backend 1.
  auto balancer2 = CreateAndStartBalancer();
  balancer2->service().SendResponse(
      BuildResponseForBackends({backends_[1]->port()}, {}));
  // Channel initially uses default balancer and therefore backend 0.
  SetNextResolutionDefaultBalancer();
  WaitForBackend(0);
  // Send 10 RPCs.
  LOG(INFO) << "========= BEFORE FIRST BATCH ==========";
  CheckRpcSendOk(10);
  LOG(INFO) << "========= DONE WITH FIRST BATCH ==========";
  // All 10 requests should have gone to the first backend.
  EXPECT_EQ(10U, backends_[0]->service().request_count());
  // Balancer 0 got a single request and sent a single request.
  EXPECT_EQ(1U, balancer_->service().request_count());
  EXPECT_EQ(1U, balancer_->service().response_count());
  EXPECT_EQ(0U, balancer2->service().request_count());
  EXPECT_EQ(0U, balancer2->service().response_count());
  // Kill balancer 0.
  LOG(INFO) << "********** ABOUT TO KILL BALANCER 0 *************";
  balancer_->Shutdown();
  LOG(INFO) << "********** KILLED BALANCER 0 *************";
  // This should trigger a re-resolution.
  EXPECT_TRUE(response_generator_->WaitForReresolutionRequest(
      absl::Seconds(5 * grpc_test_slowdown_factor())));
  LOG(INFO) << "********** SAW RE-RESOLUTION REQUEST *************";
  // Re-resolution result switches to balancer 2.
  SetNextResolution({balancer2->port()});
  // Client should start using backend 1.
  WaitForBackend(1);
  // Both balancers should each have handled one request and sent one response.
  EXPECT_EQ(1U, balancer_->service().request_count());
  EXPECT_EQ(1U, balancer_->service().response_count());
  EXPECT_EQ(1U, balancer2->service().request_count());
  EXPECT_EQ(1U, balancer2->service().response_count());
}

TEST_F(GrpclbEnd2endTest, Drop) {
  const size_t kNumRpcsPerAddress = 100;
  const size_t kNumBackends = 2;
  const int kNumDropRateLimiting = 1;
  const int kNumDropLoadBalancing = 2;
  const int kNumDropTotal = kNumDropRateLimiting + kNumDropLoadBalancing;
  const int kNumAddressesTotal = kNumBackends + kNumDropTotal;
  SetNextResolutionDefaultBalancer();
  CreateBackends(kNumBackends);
  SendBalancerResponse(BuildResponseForBackends(
      GetBackendPorts(), {{"rate_limiting", kNumDropRateLimiting},
                          {"load_balancing", kNumDropLoadBalancing}}));
  // Wait until all backends are ready.
  WaitForAllBackends();
  // Send kNumRpcsPerAddress RPCs for each server and drop address.
  size_t num_drops = 0;
  for (size_t i = 0; i < kNumRpcsPerAddress * kNumAddressesTotal; ++i) {
    EchoResponse response;
    const Status status = SendRpc(&response);
    if (!status.ok() &&
        status.error_message() == "drop directed by grpclb balancer") {
      ++num_drops;
    } else {
      EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                               << " message=" << status.error_message();
      EXPECT_EQ(response.message(), kRequestMessage);
    }
  }
  EXPECT_EQ(kNumRpcsPerAddress * kNumDropTotal, num_drops);
  // Each backend should have gotten 100 requests.
  for (size_t i = 0; i < backends_.size(); ++i) {
    EXPECT_EQ(kNumRpcsPerAddress, backends_[i]->service().request_count());
  }
  // The balancer got a single request.
  EXPECT_EQ(1U, balancer_->service().request_count());
  // and sent a single response.
  EXPECT_EQ(1U, balancer_->service().response_count());
}

TEST_F(GrpclbEnd2endTest, DropAllFirst) {
  SetNextResolutionDefaultBalancer();
  // All registered addresses are marked as "drop".
  const int kNumDropRateLimiting = 1;
  const int kNumDropLoadBalancing = 1;
  SendBalancerResponse(BuildResponseForBackends(
      {}, {{"rate_limiting", kNumDropRateLimiting},
           {"load_balancing", kNumDropLoadBalancing}}));
  const Status status = SendRpc(nullptr, 3000, true);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_message(), "drop directed by grpclb balancer");
}

TEST_F(GrpclbEnd2endTest, DropAll) {
  CreateBackends(1);
  SetNextResolutionDefaultBalancer();
  SendBalancerResponse(BuildResponseForBackends(GetBackendPorts(), {}));
  CheckRpcSendOk();
  SendBalancerResponse(BuildResponseForBackends(
      {}, {{"rate_limiting", 1}, {"load_balancing", 1}}));
  // Eventually, the update with only dropped servers is processed, and calls
  // fail.
  Status status;
  do {
    status = SendRpc(nullptr, 3000, true);
  } while (status.ok());
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_message(), "drop directed by grpclb balancer");
}

TEST_F(GrpclbEnd2endTest, ClientLoadReporting) {
  const size_t kNumBackends = 3;
  CreateBackends(kNumBackends);
  balancer_->service().set_client_load_reporting_interval_seconds(3);
  SetNextResolutionDefaultBalancer();
  const size_t kNumRpcsPerAddress = 100;
  SendBalancerResponse(BuildResponseForBackends(GetBackendPorts(), {}));
  // Wait until all backends are ready.
  int num_ok = 0;
  int num_failure = 0;
  int num_drops = 0;
  std::tie(num_ok, num_failure, num_drops) = WaitForAllBackends();
  // Send kNumRpcsPerAddress RPCs per server.
  CheckRpcSendOk(kNumRpcsPerAddress * kNumBackends);
  // Each backend should have gotten 100 requests.
  for (size_t i = 0; i < backends_.size(); ++i) {
    EXPECT_EQ(kNumRpcsPerAddress, backends_[i]->service().request_count());
  }
  // The balancer got a single request.
  EXPECT_EQ(1U, balancer_->service().request_count());
  // and sent a single response.
  EXPECT_EQ(1U, balancer_->service().response_count());
  ClientStats client_stats;
  do {
    auto stats = WaitForLoadReports();
    ASSERT_TRUE(stats.has_value());
    client_stats += *stats;
  } while (client_stats.num_calls_finished !=
           kNumRpcsPerAddress * kNumBackends + num_ok);
  EXPECT_EQ(kNumRpcsPerAddress * kNumBackends + num_ok,
            client_stats.num_calls_started);
  EXPECT_EQ(kNumRpcsPerAddress * kNumBackends + num_ok,
            client_stats.num_calls_finished);
  EXPECT_EQ(0U, client_stats.num_calls_finished_with_client_failed_to_send);
  EXPECT_EQ(kNumRpcsPerAddress * kNumBackends + (num_ok + num_drops),
            client_stats.num_calls_finished_known_received);
  EXPECT_THAT(client_stats.drop_token_counts, ::testing::ElementsAre());
}

TEST_F(GrpclbEnd2endTest, LoadReportingWithBalancerRestart) {
  const size_t kNumBackends = 4;
  const size_t kNumBackendsFirstPass = 2;
  const size_t kNumBackendsSecondPass = kNumBackends - kNumBackendsFirstPass;
  CreateBackends(kNumBackends);
  balancer_->service().set_client_load_reporting_interval_seconds(3);
  SetNextResolutionDefaultBalancer();
  // Balancer returns backends starting at index 1.
  SendBalancerResponse(
      BuildResponseForBackends(GetBackendPorts(0, kNumBackendsFirstPass), {}));
  // Wait until all backends returned by the balancer are ready.
  int num_ok = 0;
  int num_failure = 0;
  int num_drops = 0;
  std::tie(num_ok, num_failure, num_drops) =
      WaitForAllBackends(0, kNumBackendsFirstPass);
  auto client_stats = WaitForLoadReports();
  ASSERT_TRUE(client_stats.has_value());
  EXPECT_EQ(static_cast<size_t>(num_ok), client_stats->num_calls_started);
  EXPECT_EQ(static_cast<size_t>(num_ok), client_stats->num_calls_finished);
  EXPECT_EQ(0U, client_stats->num_calls_finished_with_client_failed_to_send);
  EXPECT_EQ(static_cast<size_t>(num_ok),
            client_stats->num_calls_finished_known_received);
  EXPECT_THAT(client_stats->drop_token_counts, ::testing::ElementsAre());
  // Shut down the balancer.
  balancer_->Shutdown();
  // Send 10 more requests per backend.  This will continue using the
  // last serverlist we received from the balancer before it was shut down.
  ResetBackendCounters();
  CheckRpcSendOk(kNumBackendsFirstPass);
  // Each backend should have gotten 1 request.
  for (size_t i = 0; i < kNumBackendsFirstPass; ++i) {
    EXPECT_EQ(1UL, backends_[i]->service().request_count());
  }
  // Now restart the balancer, this time pointing to all backends.
  balancer_->Start();
  SendBalancerResponse(
      BuildResponseForBackends(GetBackendPorts(kNumBackendsFirstPass), {}));
  // Wait for queries to start going to one of the new backends.
  // This tells us that we're now using the new serverlist.
  do {
    CheckRpcSendOk();
  } while (backends_[2]->service().request_count() == 0 &&
           backends_[3]->service().request_count() == 0);
  // Send one RPC per backend.
  CheckRpcSendOk(kNumBackendsSecondPass);
  // Check client stats.
  client_stats = WaitForLoadReports();
  ASSERT_TRUE(client_stats.has_value());
  EXPECT_EQ(kNumBackendsSecondPass + 1, client_stats->num_calls_started);
  EXPECT_EQ(kNumBackendsSecondPass + 1, client_stats->num_calls_finished);
  EXPECT_EQ(0U, client_stats->num_calls_finished_with_client_failed_to_send);
  EXPECT_EQ(kNumBackendsSecondPass + 1,
            client_stats->num_calls_finished_known_received);
  EXPECT_THAT(client_stats->drop_token_counts, ::testing::ElementsAre());
}

TEST_F(GrpclbEnd2endTest, LoadReportingWithDrops) {
  const size_t kNumBackends = 3;
  const size_t kNumRpcsPerAddress = 3;
  const int kNumDropRateLimiting = 2;
  const int kNumDropLoadBalancing = 1;
  const int kNumDropTotal = kNumDropRateLimiting + kNumDropLoadBalancing;
  const int kNumAddressesTotal = kNumBackends + kNumDropTotal;
  CreateBackends(kNumBackends);
  balancer_->service().set_client_load_reporting_interval_seconds(3);
  SetNextResolutionDefaultBalancer();
  SendBalancerResponse(BuildResponseForBackends(
      GetBackendPorts(), {{"rate_limiting", kNumDropRateLimiting},
                          {"load_balancing", kNumDropLoadBalancing}}));
  // Wait until all backends are ready.
  int num_warmup_ok = 0;
  int num_warmup_failure = 0;
  int num_warmup_drops = 0;
  std::tie(num_warmup_ok, num_warmup_failure, num_warmup_drops) =
      WaitForAllBackends(
          0, kNumBackends,
          WaitForBackendOptions().SetNumRequestsMultipleOf(kNumAddressesTotal));
  const int num_total_warmup_requests =
      num_warmup_ok + num_warmup_failure + num_warmup_drops;
  size_t num_drops = 0;
  for (size_t i = 0; i < kNumRpcsPerAddress * kNumAddressesTotal; ++i) {
    EchoResponse response;
    const Status status = SendRpc(&response);
    if (!status.ok() &&
        status.error_message() == "drop directed by grpclb balancer") {
      ++num_drops;
    } else {
      EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                               << " message=" << status.error_message();
      EXPECT_EQ(response.message(), kRequestMessage);
    }
  }
  EXPECT_EQ(kNumRpcsPerAddress * kNumDropTotal, num_drops);
  // Each backend should have gotten 100 requests.
  for (size_t i = 0; i < backends_.size(); ++i) {
    EXPECT_EQ(kNumRpcsPerAddress, backends_[i]->service().request_count());
  }
  // The balancer got a single request.
  EXPECT_EQ(1U, balancer_->service().request_count());
  // and sent a single response.
  EXPECT_EQ(1U, balancer_->service().response_count());
  // Get load reports.
  auto client_stats = WaitForLoadReports();
  ASSERT_TRUE(client_stats.has_value());
  EXPECT_EQ(kNumRpcsPerAddress * kNumAddressesTotal + num_total_warmup_requests,
            client_stats->num_calls_started);
  EXPECT_EQ(kNumRpcsPerAddress * kNumAddressesTotal + num_total_warmup_requests,
            client_stats->num_calls_finished);
  EXPECT_EQ(0U, client_stats->num_calls_finished_with_client_failed_to_send);
  EXPECT_EQ(kNumRpcsPerAddress * kNumBackends + num_warmup_ok,
            client_stats->num_calls_finished_known_received);
  // The number of warmup request is a multiple of the number of addresses.
  // Therefore, all addresses in the scheduled balancer response are hit the
  // same number of times.
  const int num_times_drop_addresses_hit = num_warmup_drops / kNumDropTotal;
  EXPECT_THAT(
      client_stats->drop_token_counts,
      ::testing::ElementsAre(
          ::testing::Pair("load_balancing",
                          (kNumRpcsPerAddress + num_times_drop_addresses_hit)),
          ::testing::Pair(
              "rate_limiting",
              (kNumRpcsPerAddress + num_times_drop_addresses_hit) * 2)));
}

TEST_F(GrpclbEnd2endTest, SecureNaming) {
  CreateBackends(1);
  ResetStub(/*fallback_timeout_ms=*/0,
            absl::StrCat(kApplicationTargetName, ";lb"));
  SetNextResolutionFromEndpoints(
      CreateAddressListFromPorts({balancer_->port()}, "lb"));
  SendBalancerResponse(BuildResponseForBackends(GetBackendPorts(), {}));
  // We need to wait for all backends to come online.
  WaitForAllBackends();
  // The balancer got a single request.
  EXPECT_EQ(1U, balancer_->service().request_count());
  // and sent a single response.
  EXPECT_EQ(1U, balancer_->service().response_count());
  // Check LB policy name for the channel.
  EXPECT_EQ("grpclb", channel_->GetLoadBalancingPolicyName());
}

// This death test is kept separate from the rest to ensure that it's run before
// any others. See https://github.com/grpc/grpc/pull/32269 for details.
using SingleBalancerDeathTest = GrpclbEnd2endTest;

TEST_F(SingleBalancerDeathTest, SecureNaming) {
  GTEST_FLAG_SET(death_test_style, "threadsafe");
  // Make sure that we blow up (via abort() from the security connector) when
  // the name from the balancer doesn't match expectations.
  ASSERT_DEATH_IF_SUPPORTED(
      {
        ResetStub(/*fallback_timeout_ms=*/0,
                  absl::StrCat(kApplicationTargetName, ";lb"));
        SetNextResolutionFromEndpoints(
            CreateAddressListFromPorts({balancer_->port()}, "woops"));
        channel_->WaitForConnected(grpc_timeout_seconds_to_deadline(1));
      },
      "");
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  const auto result = RUN_ALL_TESTS();
  return result;
}

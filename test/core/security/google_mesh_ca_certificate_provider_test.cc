//
//
// Copyright 2020 gRPC authors.
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
//

#include <grpc/support/port_platform.h>

#include <chrono>
#include <deque>
#include <fstream>
#include <memory>
#include <thread>

#include "absl/strings/str_cat.h"
#include "absl/synchronization/notification.h"
#include "absl/types/optional.h"
#include "third_party/istio/security/proto/providers/google/meshca.grpc.pb.h"
#include "third_party/istio/security/proto/providers/google/meshca.pb.h"

#include <gmock/gmock-matchers.h>
#include <grpcpp/impl/codegen/sync.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <gtest/gtest.h>

#include "src/core/lib/iomgr/pollset.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/security/credentials/fake/fake_credentials.h"
#include "src/core/lib/security/google_mesh_ca_certificate_provider.h"
#include "src/cpp/server/secure_server_credentials.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

namespace grpc {
namespace testing {
namespace {

using namespace grpc_core;

constexpr const char* kRootCert1 = "root_cert_1_contents";
constexpr const char* kRootCert2 = "root_cert_2_contents";
constexpr const char* kIdentityCert1 = "identity_cert_1_contents";
constexpr const char* kIdentityCert2 = "identity_cert_2_contents";

using MeshCertificateRequest =
    ::google::security::meshca::v1::MeshCertificateRequest;
using MeshCertificateResponse =
    ::google::security::meshca::v1::MeshCertificateResponse;

template <typename ServiceType>
class CountedService : public ServiceType {
 public:
  size_t request_count() {
    grpc::internal::MutexLock lock(&mu_);
    return request_count_;
  }

  void IncreaseRequestCount() {
    grpc::internal::MutexLock lock(&mu_);
    ++request_count_;
  }

  void ResetCounters() {
    grpc::internal::MutexLock lock(&mu_);
    request_count_ = 0;
  }

 protected:
  grpc::internal::Mutex mu_;

 private:
  size_t request_count_ = 0;
};

using MeshCaService = CountedService<
    ::google::security::meshca::v1::MeshCertificateService::Service>;

template <typename T>
class ServerThread {
 public:
  template <typename... Args>
  explicit ServerThread(const grpc::string& type, Args&&... args)
      : port_(grpc_pick_unused_port_or_die()),
        type_(type),
        service_(std::forward<Args>(args)...) {}

  void Start(const grpc::string& server_host) {
    gpr_log(GPR_INFO, "starting %s server on port %d", type_.c_str(), port_);
    GPR_ASSERT(!running_);
    running_ = true;
    Mutex mu;
    MutexLock lock(&mu);
    CondVar cond;
    Mutex* mu_ptr = &mu;
    CondVar* cond_ptr = &cond;
    thread_.reset(new std::thread([this, server_host, mu_ptr, cond_ptr] {
      Serve(server_host, mu_ptr, cond_ptr);
    }));
    cond.Wait(&mu);
    gpr_log(GPR_INFO, "%s server startup complete", type_.c_str());
  }

  void Serve(const grpc::string& server_host, grpc_core::Mutex* mu,
             grpc_core::CondVar* cond) {
    grpc_core::MutexLock lock(mu);
    std::ostringstream server_address;
    server_address << server_host << ":" << port_;
    ServerBuilder builder;
    std::shared_ptr<ServerCredentials> creds(new SecureServerCredentials(
        grpc_fake_transport_security_server_credentials_create()));
    builder.AddListeningPort(server_address.str(), creds);
    builder.RegisterService(&service_);
    server_ = builder.BuildAndStart();
    cond->Signal();
  }

  void SetResponse(const absl::optional<MeshCertificateResponse>& response) {
    service_.SetResponse(response);
  }

  size_t request_count() { return service_.request_count(); }

  void Shutdown() {
    if (!running_) return;
    gpr_log(GPR_INFO, "%s about to shutdown", type_.c_str());
    server_->Shutdown(grpc_timeout_milliseconds_to_deadline(0));
    thread_->join();
    gpr_log(GPR_INFO, "%s shutdown completed", type_.c_str());
    running_ = false;
  }

  int port() const { return port_; }

 private:
  const int port_;
  grpc::string type_;
  T service_;
  std::unique_ptr<Server> server_;
  std::unique_ptr<std::thread> thread_;
  bool running_ = false;
};

class ManagedCaServiceImpl : public MeshCaService {
 public:
  struct ResponseEntry {
    absl::optional<MeshCertificateResponse> response;
    bool success;
  };
  Status CreateCertificate(ServerContext* context,
                           const MeshCertificateRequest* request,
                           MeshCertificateResponse* response) override {
    IncreaseRequestCount();
    grpc::internal::MutexLock lock(&mu_);
    if (!response_.has_value()) {
      return Status(RESOURCE_EXHAUSTED, "no response added");
    }
    *response = response_.value();
    return Status::OK;
  }

  void SetResponse(const absl::optional<MeshCertificateResponse>& response) {
    response_ = response;
  }

 private:
  absl::optional<MeshCertificateResponse> response_;
};

class MockWatcher
    : public grpc_tls_certificate_distributor::TlsCertificatesWatcherInterface {
 public:
  MockWatcher() : notification_(absl::make_unique<absl::Notification>()) {}

  ~MockWatcher() {
    GRPC_ERROR_UNREF(root_cert_error_);
    GRPC_ERROR_UNREF(identity_cert_error_);
  }

  void OnCertificatesChanged(
      absl::optional<absl::string_view> root_certs,
      absl::optional<grpc_tls_certificate_distributor::PemKeyCertPairList>
          key_cert_pairs) override {
    MutexLock lock(&mu_);
    ASSERT_TRUE(root_certs.has_value());
    root_certs_.emplace(root_certs.value());
    key_cert_pairs_ = key_cert_pairs;
    notification_->Notify();
  }

  virtual void OnError(grpc_error* root_cert_error,
                       grpc_error* identity_cert_error) override {
    MutexLock lock(&mu_);
    root_cert_error_ = root_cert_error;
    identity_cert_error_ = identity_cert_error;
    notification_->Notify();
  }

  void ExpectUpdate(const absl::optional<MeshCertificateResponse>& response) {
    notification_->WaitForNotification();
    MutexLock lock(&mu_);
    if (response.has_value()) {
      if (response->cert_chain_size() == 0) {
        EXPECT_THAT(grpc_error_string(root_cert_error_),
                    ::testing::HasSubstr("Failed to parse response"));
        EXPECT_THAT(grpc_error_string(identity_cert_error_),
                    ::testing::HasSubstr("Failed to parse response"));
      } else {
        EXPECT_EQ(response->cert_chain(response->cert_chain_size() - 1),
                  root_certs_.value());
        std::string expected_cert_chain;
        for (const auto& cert : response->cert_chain()) {
          absl::StrAppend(&expected_cert_chain, cert);
        }
        EXPECT_EQ(expected_cert_chain, (*key_cert_pairs_)[0].cert_chain());
      }
    } else {
      EXPECT_THAT(grpc_error_string(root_cert_error_),
                  ::testing::HasSubstr("Call failed"));
      EXPECT_THAT(grpc_error_string(identity_cert_error_),
                  ::testing::HasSubstr("Call failed"));
    }
  }

  void Reset() {
    MutexLock lock(&mu_);
    root_certs_.reset();
    key_cert_pairs_.reset();
    GRPC_ERROR_UNREF(root_cert_error_);
    GRPC_ERROR_UNREF(identity_cert_error_);
    root_cert_error_ = GRPC_ERROR_NONE;
    identity_cert_error_ = GRPC_ERROR_NONE;
    notification_.reset(new absl::Notification);
  }

 private:
  mutable Mutex mu_;
  std::unique_ptr<absl::Notification> notification_;
  absl::optional<std::string> root_certs_;
  absl::optional<grpc_tls_certificate_distributor::PemKeyCertPairList>
      key_cert_pairs_;
  grpc_error* root_cert_error_ = GRPC_ERROR_NONE;
  grpc_error* identity_cert_error_ = GRPC_ERROR_NONE;
};

class Poller {
 public:
  Poller() {
    pollset_ = static_cast<grpc_pollset*>(gpr_zalloc(grpc_pollset_size()));
    grpc_pollset_init(pollset_, &mu_);
  }

  ~Poller() {
    grpc_pollset_shutdown(
        pollset_,
        GRPC_CLOSURE_CREATE(
            [](void* pollset, grpc_error* error) {
              grpc_pollset_destroy(static_cast<grpc_pollset*>(pollset));
              gpr_free(pollset);
            },
            pollset_, grpc_schedule_on_exec_ctx));
  }

  void AddInterestedParties(grpc_pollset_set* interested_parties) {
    grpc_pollset_set_add_pollset(interested_parties, pollset_);
  }

  void RemoveInterestedParties(grpc_pollset_set* interested_parties) {
    grpc_pollset_set_del_pollset(interested_parties, pollset_);
  }

  void Start() { thread_ = std::thread(ThreadMain, this); }

  void Stop() {
    {
      MutexLock lock(mu_);
      shutdown_ = true;
    }
    thread_.join();
  }

  static void ThreadMain(Poller* poller) {
    ExecCtx exec_ctx;
    ReleasableMutexLock lock(poller->mu_);
    while (!poller->shutdown_) {
      ASSERT_EQ(grpc_pollset_work(poller->pollset_, &poller->worker_,
                                  exec_ctx.Now() + 5),
                GRPC_ERROR_NONE);
      poller->worker_ = nullptr;
      lock.Unlock();
      exec_ctx.Flush();
      lock.Lock();
    }
  }

 private:
  gpr_mu* mu_;
  grpc_pollset* pollset_;
  grpc_pollset_worker* worker_;
  std::thread thread_;
  bool shutdown_ = false;
};

class GoogleMeshCaProviderTest : public ::testing::Test {
 public:
  GoogleMeshCaProviderTest() {}

  void SetUp() override {
    ca_server_.reset(new ServerThread<ManagedCaServiceImpl>("MeshCa"));
    ca_server_->Start("localhost");
  }

  void TearDown() override {
    ca_server_->Shutdown();
    ca_server_.reset();
  }

 protected:
  // Managed CA server
  std::unique_ptr<ServerThread<ManagedCaServiceImpl>> ca_server_;
};

TEST_F(GoogleMeshCaProviderTest, Basic) {
  ExecCtx exec_ctx;
  RefCountedPtr<grpc_channel_credentials> fake_creds(
      grpc_fake_transport_security_credentials_create());
  auto provider = MakeRefCounted<GoogleMeshCaCertificateProvider>(
      absl::StrCat("localhost:", ca_server_->port()), fake_creds.get(),
      2000 /* timeout */, 20000 /* certificate_lifetime */,
      10000 /* renewal_grace_period */, 2048 /* key_size */);
  MeshCertificateResponse response;
  response.add_cert_chain(kIdentityCert1);
  response.add_cert_chain(kRootCert1);
  ca_server_->SetResponse(absl::optional<MeshCertificateResponse>(response));
  auto watcher = new MockWatcher;
  provider->distributor()->WatchTlsCertificates(
      std::unique_ptr<MockWatcher>(watcher), "", "");
  exec_ctx.Flush();
  // Use poller to drive the I/O of the call.
  Poller poller;
  poller.AddInterestedParties(provider->interested_parties());
  poller.Start();
  watcher->ExpectUpdate(response);
  poller.RemoveInterestedParties(provider->interested_parties());
  poller.Stop();
  provider->distributor()->CancelTlsCertificatesWatch(watcher);
  EXPECT_EQ(ca_server_->request_count(), 1);
}

TEST_F(GoogleMeshCaProviderTest, Refresh) {
  ExecCtx exec_ctx;
  RefCountedPtr<grpc_channel_credentials> fake_creds(
      grpc_fake_transport_security_credentials_create());
  auto provider = MakeRefCounted<GoogleMeshCaCertificateProvider>(
      absl::StrCat("localhost:", ca_server_->port()), fake_creds.get(),
      2000 /* timeout */, 2000 /* certificate_lifetime */,
      1000 /* renewal_grace_period */, 2048 /* key_size */);
  MeshCertificateResponse response;
  response.add_cert_chain(kIdentityCert1);
  response.add_cert_chain(kRootCert1);
  ca_server_->SetResponse(absl::optional<MeshCertificateResponse>(response));
  auto watcher = new MockWatcher;
  provider->distributor()->WatchTlsCertificates(
      std::unique_ptr<MockWatcher>(watcher), "", "");
  exec_ctx.Flush();
  // Use poller to drive the I/O of the call.
  Poller poller;
  poller.AddInterestedParties(provider->interested_parties());
  poller.Start();
  watcher->ExpectUpdate(response);
  watcher->Reset();
  // Change the service's response
  response.clear_cert_chain();
  response.add_cert_chain(kIdentityCert2);
  response.add_cert_chain(kRootCert2);
  ca_server_->SetResponse(absl::optional<MeshCertificateResponse>(response));
  // After a second (renewal grace period), the mesh ca client should perform a
  // new call and get the new certificates.
  watcher->ExpectUpdate(response);
  poller.RemoveInterestedParties(provider->interested_parties());
  poller.Stop();
  provider->distributor()->CancelTlsCertificatesWatch(watcher);
  EXPECT_EQ(ca_server_->request_count(), 2);
}

TEST_F(GoogleMeshCaProviderTest, WatchCancelAndAddWatcherAgain) {
  ExecCtx exec_ctx;
  RefCountedPtr<grpc_channel_credentials> fake_creds(
      grpc_fake_transport_security_credentials_create());
  auto provider = MakeRefCounted<GoogleMeshCaCertificateProvider>(
      absl::StrCat("localhost:", ca_server_->port()), fake_creds.get(),
      2000 /* timeout */, 20000 /* certificate_lifetime */,
      10000 /* renewal_grace_period */, 2048 /* key_size */);
  MeshCertificateResponse response;
  response.add_cert_chain(kIdentityCert1);
  response.add_cert_chain(kRootCert1);
  ca_server_->SetResponse(absl::optional<MeshCertificateResponse>(response));
  auto watcher = new MockWatcher;
  provider->distributor()->WatchTlsCertificates(
      std::unique_ptr<MockWatcher>(watcher), "", "");
  exec_ctx.Flush();
  // Use poller to drive the I/O of the call.
  Poller poller;
  poller.AddInterestedParties(provider->interested_parties());
  poller.Start();
  watcher->ExpectUpdate(response);
  // Cancel the watch
  provider->distributor()->CancelTlsCertificatesWatch(watcher);
  // Add a new watcher
  watcher = new MockWatcher;
  provider->distributor()->WatchTlsCertificates(
      std::unique_ptr<MockWatcher>(watcher), "", "");
  exec_ctx.Flush();
  // Watcher should get the response
  watcher->ExpectUpdate(response);
  poller.RemoveInterestedParties(provider->interested_parties());
  poller.Stop();
  provider->distributor()->CancelTlsCertificatesWatch(watcher);
  EXPECT_EQ(ca_server_->request_count(), 1);
}

TEST_F(GoogleMeshCaProviderTest, WatchCancelAndAddWatcherAgainAfterDelay) {
  ExecCtx exec_ctx;
  RefCountedPtr<grpc_channel_credentials> fake_creds(
      grpc_fake_transport_security_credentials_create());
  auto provider = MakeRefCounted<GoogleMeshCaCertificateProvider>(
      absl::StrCat("localhost:", ca_server_->port()), fake_creds.get(),
      2000 /* timeout */, 2000 /* certificate_lifetime */,
      1000 /* renewal_grace_period */, 2048 /* key_size */);
  MeshCertificateResponse response;
  response.add_cert_chain(kIdentityCert1);
  response.add_cert_chain(kRootCert1);
  ca_server_->SetResponse(absl::optional<MeshCertificateResponse>(response));
  auto watcher = new MockWatcher;
  provider->distributor()->WatchTlsCertificates(
      std::unique_ptr<MockWatcher>(watcher), "", "");
  exec_ctx.Flush();
  // Use poller to drive the I/O of the call.
  Poller poller;
  poller.AddInterestedParties(provider->interested_parties());
  poller.Start();
  watcher->ExpectUpdate(response);
  // Cancel the watch
  provider->distributor()->CancelTlsCertificatesWatch(watcher);
  gpr_sleep_until(grpc_timeout_seconds_to_deadline(1));
  exec_ctx.InvalidateNow();
  // Add a new watcher
  watcher = new MockWatcher;
  provider->distributor()->WatchTlsCertificates(
      std::unique_ptr<MockWatcher>(watcher), "", "");
  exec_ctx.Flush();
  // Watcher should get the response
  watcher->ExpectUpdate(response);
  poller.RemoveInterestedParties(provider->interested_parties());
  poller.Stop();
  provider->distributor()->CancelTlsCertificatesWatch(watcher);
  EXPECT_EQ(ca_server_->request_count(), 2);
}

TEST_F(GoogleMeshCaProviderTest, CallError) {
  ExecCtx exec_ctx;
  RefCountedPtr<grpc_channel_credentials> fake_creds(
      grpc_fake_transport_security_credentials_create());
  auto provider = MakeRefCounted<GoogleMeshCaCertificateProvider>(
      absl::StrCat("localhost:", ca_server_->port()), fake_creds.get(),
      2000 /* timeout */, 20000 /* certificate_lifetime */,
      10000 /* renewal_grace_period */, 2048 /* key_size */);
  auto watcher = new MockWatcher;
  provider->distributor()->WatchTlsCertificates(
      std::unique_ptr<MockWatcher>(watcher), "", "");
  exec_ctx.Flush();
  // Use poller to drive the I/O of the call.
  Poller poller;
  poller.AddInterestedParties(provider->interested_parties());
  poller.Start();
  watcher->ExpectUpdate(absl::nullopt);
  poller.RemoveInterestedParties(provider->interested_parties());
  poller.Stop();
  provider->distributor()->CancelTlsCertificatesWatch(watcher);
}

TEST_F(GoogleMeshCaProviderTest, EmptyResponse) {
  ExecCtx exec_ctx;
  RefCountedPtr<grpc_channel_credentials> fake_creds(
      grpc_fake_transport_security_credentials_create());
  auto provider = MakeRefCounted<GoogleMeshCaCertificateProvider>(
      absl::StrCat("localhost:", ca_server_->port()), fake_creds.get(),
      2000 /* timeout */, 20000 /* certificate_lifetime */,
      10000 /* renewal_grace_period */, 2048 /* key_size */);
  ca_server_->SetResponse(absl::make_optional<MeshCertificateResponse>());
  auto watcher = new MockWatcher;
  provider->distributor()->WatchTlsCertificates(
      std::unique_ptr<MockWatcher>(watcher), "", "");
  exec_ctx.Flush();
  // Use poller to drive the I/O of the call.
  Poller poller;
  poller.AddInterestedParties(provider->interested_parties());
  poller.Start();
  watcher->ExpectUpdate(absl::make_optional<MeshCertificateResponse>());
  poller.RemoveInterestedParties(provider->interested_parties());
  poller.Stop();
  provider->distributor()->CancelTlsCertificatesWatch(watcher);
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(argc, argv);
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}

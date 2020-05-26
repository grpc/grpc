#include <memory>

#include <gtest/gtest.h>
#include "absl/strings/str_cat.h"
#include "absl/types/optional.h"
#include "src/core/lib/iomgr/pollset.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/security/certificate_provider/factory.h"
#include "src/core/lib/security/certificate_provider/google_mesh_ca.h"
#include "src/core/lib/security/certificate_provider/registry.h"
#include "src/core/lib/security/credentials/fake/fake_credentials.h"
#include "src/core/lib/security/credentials/tls/grpc_tls_credentials_options.h"
#include "src/cpp/server/secure_server_credentials.h"
#include "src/proto/grpc/meshca/v1/ca.grpc.pb.h"
#include "src/proto/grpc/meshca/v1/ca.pb.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

#include <grpcpp/impl/codegen/sync.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>

#include <deque>
#include <fstream>
#include <thread>

namespace grpc {
namespace testing {
namespace {

const char* kServer0Key = "src/core/tsi/test_creds/server0.key";
const char* kServer0CertChain = "src/core/tsi/test_creds/server0.pem";
const char* kServer1Key = "src/core/tsi/test_creds/server1.key";
const char* kServer1CertChain = "src/core/tsi/test_creds/server1.pem";

using MeshCertificateRequest =
    ::google::security::meshca::v1::MeshCertificateRequest;
using MeshCertificateResponse =
    ::google::security::meshca::v1::MeshCertificateResponse;

using namespace grpc_core;

template <typename ServiceType>
class CountedService : public ServiceType {
 public:
  size_t request_count() {
    grpc::internal::MutexLock lock(&mu_);
    return request_count_;
  }

  size_t response_count() {
    grpc::internal::MutexLock lock(&mu_);
    return response_count_;
  }

  void IncreaseResponseCount() {
    grpc::internal::MutexLock lock(&mu_);
    ++response_count_;
  }
  void IncreaseRequestCount() {
    grpc::internal::MutexLock lock(&mu_);
    ++request_count_;
  }

  void ResetCounters() {
    grpc::internal::MutexLock lock(&mu_);
    request_count_ = 0;
    response_count_ = 0;
  }

 protected:
  grpc::internal::Mutex mu_;

 private:
  size_t request_count_ = 0;
  size_t response_count_ = 0;
};

using MeshCaService = CountedService<
    ::google::security::meshca::v1::MeshCertificateService::Service>;

template <typename T>
struct ServerThread {
  template <typename... Args>
  explicit ServerThread(const grpc::string& type, Args&&... args)
      : port_(grpc_pick_unused_port_or_die()),
        type_(type),
        service_(std::forward<Args>(args)...) {}

  void Start(const grpc::string& server_host) {
    gpr_log(GPR_INFO, "starting %s server on port %d", type_.c_str(), port_);
    GPR_ASSERT(!running_);
    running_ = true;
    service_.Start();
    grpc::internal::Mutex mu;
    // We need to acquire the lock here in order to prevent the notify_one
    // by ServerThread::Serve from firing before the wait below is hit.
    grpc::internal::MutexLock lock(&mu);
    grpc::internal::CondVar cond;
    thread_.reset(new std::thread(
        std::bind(&ServerThread::Serve, this, server_host, &mu, &cond)));
    cond.Wait(&mu);
    gpr_log(GPR_INFO, "%s server startup complete", type_.c_str());
  }

  void Serve(const grpc::string& server_host, grpc::internal::Mutex* mu,
             grpc::internal::CondVar* cond) {
    // We need to acquire the lock here in order to prevent the notify_one
    // below from firing before its corresponding wait is executed.
    grpc::internal::MutexLock lock(mu);
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

  void Shutdown() {
    if (!running_) return;
    gpr_log(GPR_INFO, "%s about to shutdown", type_.c_str());
    service_.Shutdown();
    server_->Shutdown(grpc_timeout_milliseconds_to_deadline(0));
    thread_->join();
    gpr_log(GPR_INFO, "%s shutdown completed", type_.c_str());
    running_ = false;
  }

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
    IncreaseResponseCount();
    grpc::internal::MutexLock lock(&mu_);
    if (responses_.empty()) {
      return Status(RESOURCE_EXHAUSTED, "no response added");
    }
    if (responses_[0].success) {
      *response = std::move(responses_[0].response.value());
      responses_.pop_front();
      return Status::OK;
    } else {
      responses_.pop_front();
      return Status(INTERNAL, "error added by test");
    }
  }

  void AddResponse(ResponseEntry response) {
    grpc::internal::MutexLock lock(&mu_);
    responses_.push_back(std::move(response));
  }

  void Start() {
    grpc::internal::MutexLock lock(&mu_);
    responses_.clear();
  }

  void Shutdown() {}

 private:
  std::deque<ResponseEntry> responses_;
};

// Mock distributor that allows checking the root certificates and one cert
// chain from the provider. Does not test the private key because they are
// automatically generated by the provider. Returns true if the expected
// responses are seen. Returns false if responses other than the expected ones
// are seen, or no response seen.
class MockDistributor : public grpc_tls_certificate_distributor {
 public:
  bool WaitForResponse(absl::string_view pem_root_certs,
                       absl::string_view pem_cert_chain,
                       grpc_millis timeout_s = 10000) {
    grpc::internal::MutexLock lock(&mu_);
    while (!new_values_available_) {
      if (cv_.Wait(&mu_, gpr_time_add(
                             gpr_now(GPR_CLOCK_REALTIME),
                             gpr_time_from_millis(timeout_s, GPR_TIMESPAN)))) {
        break;
      }
    }
    if (new_values_available_) {
      return (pem_root_certs_ == pem_root_certs &&
              pem_cert_chain_ == pem_cert_chain);
    } else {
      return false;
    }
  }

  void Reset() {
    grpc::internal::MutexLock lock(&mu_);
    new_values_available_ = false;
    pem_root_certs_.clear();
    pem_cert_chain_.clear();
  }

 private:
  void SetKeyMaterials(absl::string_view pem_root_certs,
                       PemKeyCertPairList pem_key_cert_pairs) override {
    grpc::internal::MutexLock lock(&mu_);
    pem_root_certs_ = std::string(pem_root_certs);
    pem_cert_chain_ = std::string(pem_key_cert_pairs[0].cert_chain());
    new_values_available_ = true;
    cv_.Broadcast();
  }

  void SetRootCerts(absl::string_view pem_root_certs) override {
    grpc::internal::MutexLock lock(&mu_);
    pem_root_certs_ = std::string(pem_root_certs);
    new_values_available_ = true;
    cv_.Broadcast();
  }

  void SetKeyCertPairs(PemKeyCertPairList pem_key_cert_pairs) override {
    grpc::internal::MutexLock lock(&mu_);
    pem_cert_chain_ = pem_key_cert_pairs[0].cert_chain();
    new_values_available_ = true;
    cv_.Broadcast();
  }

  grpc::internal::Mutex mu_;
  grpc::internal::CondVar cv_;
  bool new_values_available_ = false;
  std::string pem_root_certs_;
  std::string pem_cert_chain_;
};

class Poller {
 public:
  Poller() {
    pollset_ = static_cast<grpc_pollset*>(gpr_zalloc(grpc_pollset_size()));
    grpc_pollset_init(pollset_, &mu_);
  }

  ~Poller() {
    grpc_pollset_destroy(pollset_);
    gpr_free(pollset_);
  }

  void AddInterestedParties(grpc_pollset_set* interested_parties) {
    grpc_pollset_set_add_pollset(interested_parties, pollset_);
  }

  void RemoveInterestedParties(grpc_pollset_set* interested_parties) {
    grpc_pollset_set_del_pollset(interested_parties, pollset_);
  }

  void Start() { thread_ = std::thread(ThreadMain, this); }

  void Stop() {
    gpr_mu_lock(mu_);
    shutdown_ = true;
    grpc_pollset_kick(pollset_, worker_);
    gpr_mu_unlock(mu_);
    thread_.join();
  }

  static void ThreadMain(Poller* poller) {
    grpc_core::ExecCtx exec_ctx;
    gpr_mu_lock(poller->mu_);
    while (!poller->shutdown_) {
      grpc_pollset_work(poller->pollset_, &poller->worker_, exec_ctx.Now() + 5);
      poller->worker_ = nullptr;
      gpr_mu_unlock(poller->mu_);
      exec_ctx.Flush();
      gpr_mu_lock(poller->mu_);
    }
    gpr_mu_unlock(poller->mu_);
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

  static void SetUpTestCase() { grpc_init(); }

  static void TearDownTestCase() { grpc_shutdown_blocking(); }

  void SetUp() override {
    ca_server_.reset(new ServerThread<ManagedCaServiceImpl>("MeshCa"));
    ca_server_->Start("localhost");
  }

  void TearDown() override {
    ca_server_->Shutdown();
    ca_server_.reset();
  }

 protected:
  // Create a Json configuration with given parameters
  Json BuildJsonConfig(grpc_millis certificate_lifetime = 0,
                       grpc_millis renewal_grace_period = 0) {
    // Omitting the call credentials part in the unit test.
    Json result = Json(Json::Object{
        {"server",
         Json::Object{
             {"grpcServices",
              Json::Array{Json::Object{
                  {"googleGrpc",
                   Json::Object{
                       {"targetUri",
                        absl::StrCat("localhost:", ca_server_->port_)},
                   }},
                  {"timeout", Json::Object{{"seconds", 5}, {"nanos", 0}}}}}}}},
        {"keyType", "KEY_TYPE_RSA"},
        {"keySize", 2048},
        {"gceZone", ""}});
    if (certificate_lifetime != 0) {
      result.mutable_object()->emplace(
          "certificateLifetime",
          Json::Object{{"seconds", certificate_lifetime / GPR_MS_PER_SEC},
                       {"duration", 0}});
    }
    if (renewal_grace_period != 0) {
      result.mutable_object()->emplace(
          "renewalGracePeriod",
          Json::Object{{"seconds", renewal_grace_period / GPR_MS_PER_SEC}, {}});
    }
    return result;
  }

  // Set the next response that the CA server issues
  void SetNextCaResponse(std::vector<std::string> cert_chains,
                         bool success = true) {
    if (success) {
      MeshCertificateResponse response;
      for (std::string& cert : cert_chains) {
        response.add_cert_chain(cert);
      }
      ca_server_->service_.AddResponse({std::move(response), true});
    } else {
      ca_server_->service_.AddResponse({{}, false});
    }
  }

  // Read the entire file into a string
  std::string ReadFile(const char* file_path) {
    std::ifstream ifs(file_path);
    std::stringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
  }

  // Managed CA server
  std::unique_ptr<ServerThread<ManagedCaServiceImpl>> ca_server_;
};

TEST_F(GoogleMeshCaProviderTest, Vanilla) {
  grpc_core::ExecCtx exec_ctx;
  RefCountedPtr<MockDistributor> mock_distributor =
      MakeRefCounted<MockDistributor>();
  std::string expected_cert_chain_str = ReadFile(kServer0CertChain);
  std::string expected_root_certs_str = expected_cert_chain_str;
  SetNextCaResponse({expected_cert_chain_str});
  Json config_json = BuildJsonConfig();
  CertificateProviderFactory* factory =
      CertificateProviderRegistry::GetFactory("google_mesh_ca");
  grpc_error* error;
  RefCountedPtr<CertificateProviderConfig> config =
      factory->CreateProviderConfig(config_json, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE);
  OrphanablePtr<CertificateProvider> provider =
      MakeOrphanable<GoogleMeshCaProvider>(
          std::move(config), mock_distributor,
          grpc_fake_transport_security_credentials_create());
  exec_ctx.Flush();
  // Use poller to drive the I/O of the call.
  Poller poller;
  poller.AddInterestedParties(provider->interested_parties());
  poller.Start();
  EXPECT_TRUE(mock_distributor->WaitForResponse(expected_root_certs_str,
                                                expected_cert_chain_str));
  poller.RemoveInterestedParties(provider->interested_parties());
  poller.Stop();
  EXPECT_EQ(ca_server_->service_.request_count(), 1);
}

// Test whether the provider renews the certificate when entering the grace
// period.
TEST_F(GoogleMeshCaProviderTest, RefreshCertificate) {
  grpc_core::ExecCtx exec_ctx;
  RefCountedPtr<MockDistributor> mock_distributor =
      MakeRefCounted<MockDistributor>();
  std::string expected_cert_chain_str = ReadFile(kServer0CertChain);
  std::string expected_root_certs_str = expected_cert_chain_str;
  std::string expected_cert_chain_2_str = ReadFile(kServer1CertChain);
  std::string expected_root_certs_2_str = expected_cert_chain_2_str;
  SetNextCaResponse({expected_cert_chain_str});
  SetNextCaResponse({expected_cert_chain_2_str});
  Json config_json = BuildJsonConfig(3000 /* certificate lifetime */,
                                     2000 /* renewal grace period */);
  CertificateProviderFactory* factory =
      CertificateProviderRegistry::GetFactory("google_mesh_ca");
  grpc_error* error;
  RefCountedPtr<CertificateProviderConfig> config =
      factory->CreateProviderConfig(config_json, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE);
  OrphanablePtr<CertificateProvider> provider =
      MakeOrphanable<GoogleMeshCaProvider>(
          std::move(config), mock_distributor,
          grpc_fake_transport_security_credentials_create());
  exec_ctx.Flush();
  // Use poller to drive the I/O of the call.
  Poller poller;
  poller.AddInterestedParties(provider->interested_parties());
  poller.Start();
  // Wait until the certificate is refreshed.
  sleep(2);
  EXPECT_TRUE(mock_distributor->WaitForResponse(expected_root_certs_2_str,
                                                expected_cert_chain_2_str));
  poller.RemoveInterestedParties(provider->interested_parties());
  poller.Stop();
  EXPECT_EQ(ca_server_->service_.request_count(), 2);
}

TEST_F(GoogleMeshCaProviderTest, FailedCall) {
  grpc_core::ExecCtx exec_ctx;
  RefCountedPtr<MockDistributor> mock_distributor =
      MakeRefCounted<MockDistributor>();
  std::string expected_cert_chain_str = ReadFile(kServer0CertChain);
  std::string expected_root_certs_str = expected_cert_chain_str;
  SetNextCaResponse({}, false /* failed response */);
  SetNextCaResponse({expected_cert_chain_str});
  Json config_json = BuildJsonConfig();
  CertificateProviderFactory* factory =
      CertificateProviderRegistry::GetFactory("google_mesh_ca");
  grpc_error* error;
  RefCountedPtr<CertificateProviderConfig> config =
      factory->CreateProviderConfig(config_json, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE);
  OrphanablePtr<CertificateProvider> provider =
      MakeOrphanable<GoogleMeshCaProvider>(
          std::move(config), mock_distributor,
          grpc_fake_transport_security_credentials_create());
  exec_ctx.Flush();
  // Use poller to drive the I/O of the call.
  Poller poller;
  poller.AddInterestedParties(provider->interested_parties());
  poller.Start();
  // Expect the provider receives the failed response first and backoff for 1s.
  EXPECT_FALSE(mock_distributor->WaitForResponse(expected_root_certs_str,
                                                 expected_cert_chain_str, 500));
  EXPECT_EQ(ca_server_->service_.request_count(), 1);
  sleep(1);
  // Expect the privider to receive the success response after the backoff.
  EXPECT_TRUE(mock_distributor->WaitForResponse(expected_root_certs_str,
                                                expected_cert_chain_str));
  poller.RemoveInterestedParties(provider->interested_parties());
  poller.Stop();
  EXPECT_EQ(ca_server_->service_.request_count(), 2);
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(argc, argv);
  return RUN_ALL_TESTS();
}

//
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

#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>
#include <grpc/private_key_signer.h>
#include <openssl/bio.h>
#include <openssl/mem.h>
#include <openssl/pem.h>

#include <atomic>
#include <memory>
#include <string>
#include <utility>
#include <variant>

#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/timer_manager.h"
#include "src/core/tsi/ssl_transport_security.h"
#include "src/core/tsi/transport_security_interface.h"
#include "src/core/util/time.h"
#include "src/core/util/wait_for_single_owner.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.h"
#include "test/core/test_util/test_config.h"
#include "test/core/test_util/tls_utils.h"
#include "test/core/tsi/transport_security_test_lib.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

#if defined(OPENSSL_IS_BORINGSSL)

#include "src/core/credentials/transport/tls/grpc_tls_certificate_selector.h"
#include "test/core/tsi/private_key_signer_test_util.h"

namespace grpc_core {
namespace testing {
namespace {

constexpr absl::string_view kTestCredsRelativePath = "src/core/tsi/test_creds/";
const char kServerName[] = "foo.test.google.fr";

class SyncTestCertificateSelector : public CertificateSelector {
 public:
  SyncTestCertificateSelector(
      absl::string_view cert_chain,
      std::variant<absl::string_view, std::shared_ptr<PrivateKeySigner>>
          private_key,
      bool expect_success = true)
      : cert_chain_(cert_chain),
        private_key_(std::move(private_key)),
        expect_success_(expect_success) {}

  std::variant<absl::StatusOr<SelectCertResult>,
               std::shared_ptr<AsyncCertSelectionHandle>>
  SelectCert(const SelectCertInfo&, OnSelectCertComplete) override {
    if (!expect_success_) {
      return absl::InternalError("Failed to select cert.");
    }
    return CreateSelectCertResult(cert_chain_, private_key_);
  }

  void Cancel(std::shared_ptr<AsyncCertSelectionHandle> /*handle*/) override {}

 private:
  absl::string_view cert_chain_;
  std::variant<absl::string_view, std::shared_ptr<PrivateKeySigner>>
      private_key_;
  bool expect_success_;
};

class AsyncTestCertificateSelector : public CertificateSelector {
 public:
  AsyncTestCertificateSelector(
      absl::string_view cert_chain,
      std::variant<absl::string_view, std::shared_ptr<PrivateKeySigner>>
          private_key,
      std::shared_ptr<grpc_event_engine::experimental::FuzzingEventEngine>
          event_engine,
      bool expect_success = true)
      : cert_chain_(cert_chain),
        private_key_(std::move(private_key)),
        event_engine_(std::move(event_engine)),
        expect_success_(expect_success) {}

  std::variant<absl::StatusOr<SelectCertResult>,
               std::shared_ptr<AsyncCertSelectionHandle>>
  SelectCert(const SelectCertInfo&, OnSelectCertComplete on_complete) override {
    auto handle = std::make_shared<AsyncCertSelectionHandle>();
    event_engine_->RunAfter(Duration::Seconds(2), [this, handle,
                                                   on_complete = std::move(
                                                       on_complete)]() mutable {
      if (!expect_success_) {
        std::move(on_complete)(absl::InternalError("Failed to select cert."));
        return;
      }
      std::move(on_complete)(CreateSelectCertResult(cert_chain_, private_key_));
    });
    return handle;
  }

  void Cancel(std::shared_ptr<AsyncCertSelectionHandle> /*handle*/) override {
    was_cancelled_.store(true);
  }

  bool WasCancelled() { return was_cancelled_.load(); }

 private:
  absl::string_view cert_chain_;
  std::variant<absl::string_view, std::shared_ptr<PrivateKeySigner>>
      private_key_;
  std::shared_ptr<grpc_event_engine::experimental::FuzzingEventEngine>
      event_engine_;
  bool expect_success_;
  std::atomic<bool> was_cancelled_{false};
};

class SslCertSelectorTsiTestFixture {
 public:
  struct FixtureOptions {
    bool use_signer = false;
    bool is_cert_selection_async = false;
    bool is_private_key_signing_async = false;
    bool expect_cert_selection_success = true;
    bool expect_private_key_signing_success = true;
    tsi_tls_version tls_version;
  };

  SslCertSelectorTsiTestFixture(
      const FixtureOptions& options,
      const std::shared_ptr<
          grpc_event_engine::experimental::FuzzingEventEngine>& event_engine)
      : tls_version_(options.tls_version) {
    tsi_test_fixture_init(&base_);
    base_.test_unused_bytes = true;
    base_.vtable = &kVtable;
    ca_cert_ = GetFileContents(absl::StrCat(kTestCredsRelativePath, "ca.pem"));
    server_key_ =
        GetFileContents(absl::StrCat(kTestCredsRelativePath, "server1.key"));
    server_cert_ =
        GetFileContents(absl::StrCat(kTestCredsRelativePath, "server1.pem"));
    std::variant<absl::string_view, std::shared_ptr<PrivateKeySigner>>
        private_key;
    if (options.use_signer) {
      if (options.is_private_key_signing_async) {
        private_key = std::make_shared<AsyncTestPrivateKeySigner>(
            server_key_, event_engine,
            options.expect_private_key_signing_success
                ? AsyncTestPrivateKeySigner::Mode::kSuccess
                : AsyncTestPrivateKeySigner::Mode::kError);
      } else {
        private_key = std::make_shared<SyncTestPrivateKeySigner>(
            server_key_, options.expect_private_key_signing_success
                             ? SyncTestPrivateKeySigner::Mode::kSuccess
                             : SyncTestPrivateKeySigner::Mode::kError);
      }
    } else {
      private_key = server_key_;
    }
    if (options.is_cert_selection_async) {
      cert_selector_ = std::make_shared<AsyncTestCertificateSelector>(
          server_cert_, private_key, event_engine,
          options.expect_cert_selection_success);
    } else {
      cert_selector_ = std::make_shared<SyncTestCertificateSelector>(
          server_cert_, private_key, options.expect_cert_selection_success);
    }
  }

  void Run(bool expect_success,
           grpc_event_engine::experimental::FuzzingEventEngine* event_engine) {
    expect_success_ = expect_success;
    tsi_test_do_handshake(&base_, event_engine);
    event_engine->TickUntilIdle();
    tsi_test_fixture_destroy(&base_);
  }

  bool CertSelectionCancelled() {
    return reinterpret_cast<AsyncTestCertificateSelector*>(cert_selector_.get())
        ->WasCancelled();
  }

  void Shutdown() {
    if (base_.client_handshaker != nullptr) {
      tsi_handshaker_shutdown(base_.client_handshaker);
    }
    if (base_.server_handshaker != nullptr) {
      tsi_handshaker_shutdown(base_.server_handshaker);
    }
  }

  ~SslCertSelectorTsiTestFixture() {
    tsi_ssl_server_handshaker_factory_unref(server_handshaker_factory_);
    tsi_ssl_client_handshaker_factory_unref(client_handshaker_factory_);
  }

 private:
  static void SetupHandshakers(tsi_test_fixture* fixture) {
    auto* self = reinterpret_cast<SslCertSelectorTsiTestFixture*>(fixture);
    self->SetupHandshakersImpl();
  }

  void SetupHandshakersImpl() {
    // Create client handshaker factory.
    tsi_ssl_client_handshaker_options client_options;
    client_options.root_cert_info =
        std::make_shared<tsi::RootCertInfo>(ca_cert_);
    client_options.min_tls_version = tls_version_;
    client_options.max_tls_version = tls_version_;
    ASSERT_EQ(tsi_create_ssl_client_handshaker_factory_with_options(
                  &client_options, &client_handshaker_factory_),
              TSI_OK);

    // Create server handshaker factory.
    tsi_ssl_server_handshaker_options server_options;
    server_options.client_certificate_request =
        TSI_DONT_REQUEST_CLIENT_CERTIFICATE;
    server_options.min_tls_version = tls_version_;
    server_options.max_tls_version = tls_version_;
    ASSERT_NE(cert_selector_, nullptr);
    server_options.pem_key_cert_pairs = cert_selector_;
    ASSERT_EQ(tsi_create_ssl_server_handshaker_factory_with_options(
                  &server_options, &server_handshaker_factory_),
              TSI_OK);

    // Create handshakers.
    tsi_handshaker* client_hs;
    tsi_handshaker* server_hs;
    ASSERT_EQ(tsi_ssl_client_handshaker_factory_create_handshaker(
                  client_handshaker_factory_, kServerName, 0, 0, std::nullopt,
                  &client_hs),
              TSI_OK);
    ASSERT_EQ(tsi_ssl_server_handshaker_factory_create_handshaker(
                  server_handshaker_factory_, 0, 0, &server_hs),
              TSI_OK);
    base_.client_handshaker = client_hs;
    base_.server_handshaker = server_hs;
  }

  static void CheckHandshakerPeers(tsi_test_fixture* fixture) {
    auto* self = reinterpret_cast<SslCertSelectorTsiTestFixture*>(fixture);
    self->CheckHandshakerPeersImpl();
  }

  void CheckHandshakerPeersImpl() {
    if (expect_success_) {
      tsi_peer peer;
      EXPECT_EQ(tsi_handshaker_result_extract_peer(base_.client_result, &peer),
                TSI_OK);
      tsi_peer_destruct(&peer);
      EXPECT_EQ(tsi_handshaker_result_extract_peer(base_.server_result, &peer),
                TSI_OK);
      tsi_peer_destruct(&peer);
    } else {
      EXPECT_EQ(base_.client_result, nullptr);
      EXPECT_EQ(base_.server_result, nullptr);
    }
  }

  static void Destruct(tsi_test_fixture* /*fixture*/) {
    // We don't delete here because we are managed by std::shared_ptr.
  }

  tsi_test_fixture base_;  // MUST BE FIRST
  static struct tsi_test_fixture_vtable kVtable;

  tsi_ssl_server_handshaker_factory* server_handshaker_factory_ = nullptr;
  tsi_ssl_client_handshaker_factory* client_handshaker_factory_ = nullptr;
  std::string ca_cert_;
  std::string server_key_;
  std::string server_cert_;
  std::shared_ptr<CertificateSelector> cert_selector_;
  tsi_tls_version tls_version_;
  bool expect_success_ = false;
};

struct tsi_test_fixture_vtable SslCertSelectorTsiTestFixture::kVtable = {
    &SslCertSelectorTsiTestFixture::SetupHandshakers,
    &SslCertSelectorTsiTestFixture::CheckHandshakerPeers,
    &SslCertSelectorTsiTestFixture::Destruct};

class CertSelectionOffloadTest
    : public ::testing::TestWithParam<tsi_tls_version> {
 protected:
  void SetUp() override {
    // Without this the test had a failure dealing with grpc timers on TSAN
    grpc_timer_manager_set_start_threaded(false);
    event_engine_ =
        std::make_shared<grpc_event_engine::experimental::FuzzingEventEngine>(
            grpc_event_engine::experimental::FuzzingEventEngine::Options(),
            fuzzing_event_engine::Actions());
    grpc_init();
  }

  void TearDown() override {
    ExecCtx exec_ctx;
    event_engine_->FuzzingDone();
    exec_ctx.Flush();
    event_engine_->TickUntilIdle();
    event_engine_->UnsetGlobalHooks();
    WaitForSingleOwner(std::move(event_engine_));
    grpc_shutdown_blocking();
  }

  std::shared_ptr<grpc_event_engine::experimental::FuzzingEventEngine>
      event_engine_;
};

TEST_P(CertSelectionOffloadTest, SyncCertSelectionWithStaticKeySucceeds) {
  SslCertSelectorTsiTestFixture::FixtureOptions options;
  options.use_signer = false;
  options.is_cert_selection_async = false;
  options.expect_cert_selection_success = true;
  options.tls_version = GetParam();
  auto fixture =
      std::make_shared<SslCertSelectorTsiTestFixture>(options, event_engine_);
  fixture->Run(/*expect_success=*/true, event_engine_.get());
}

TEST_P(CertSelectionOffloadTest, SyncCertSelectionFails) {
  SslCertSelectorTsiTestFixture::FixtureOptions options;
  options.is_cert_selection_async = false;
  options.expect_cert_selection_success = false;
  options.tls_version = GetParam();
  auto fixture =
      std::make_shared<SslCertSelectorTsiTestFixture>(options, event_engine_);
  fixture->Run(/*expect_success=*/false, event_engine_.get());
}

TEST_P(CertSelectionOffloadTest, SyncCertSelectionWithSyncSignerSucceeds) {
  SslCertSelectorTsiTestFixture::FixtureOptions options;
  options.use_signer = true;
  options.is_cert_selection_async = false;
  options.is_private_key_signing_async = false;
  options.expect_cert_selection_success = true;
  options.expect_private_key_signing_success = true;
  options.tls_version = GetParam();
  auto fixture =
      std::make_shared<SslCertSelectorTsiTestFixture>(options, event_engine_);
  fixture->Run(/*expect_success=*/true, event_engine_.get());
}

TEST_P(CertSelectionOffloadTest, SyncCertSelectionWithAsyncSignerSucceeds) {
  SslCertSelectorTsiTestFixture::FixtureOptions options;
  options.use_signer = true;
  options.is_cert_selection_async = false;
  options.is_private_key_signing_async = true;
  options.expect_cert_selection_success = true;
  options.expect_private_key_signing_success = true;
  options.tls_version = GetParam();
  auto fixture =
      std::make_shared<SslCertSelectorTsiTestFixture>(options, event_engine_);
  fixture->Run(/*expect_success=*/true, event_engine_.get());
}

TEST_P(CertSelectionOffloadTest, AsyncCertSelectionWithStaticKeySucceeds) {
  SslCertSelectorTsiTestFixture::FixtureOptions options;
  options.use_signer = false;
  options.is_cert_selection_async = true;
  options.expect_cert_selection_success = true;
  options.tls_version = GetParam();
  auto fixture =
      std::make_shared<SslCertSelectorTsiTestFixture>(options, event_engine_);
  fixture->Run(/*expect_success=*/true, event_engine_.get());
}

TEST_P(CertSelectionOffloadTest, AsyncCertSelectionFails) {
  SslCertSelectorTsiTestFixture::FixtureOptions options;
  options.is_cert_selection_async = true;
  options.expect_cert_selection_success = false;
  options.tls_version = GetParam();
  auto fixture =
      std::make_shared<SslCertSelectorTsiTestFixture>(options, event_engine_);
  fixture->Run(/*expect_success=*/false, event_engine_.get());
}

TEST_P(CertSelectionOffloadTest, AsyncCertSelectionCancelled) {
  SslCertSelectorTsiTestFixture::FixtureOptions options;
  options.is_cert_selection_async = true;
  options.expect_cert_selection_success = true;
  options.tls_version = GetParam();
  auto fixture =
      std::make_shared<SslCertSelectorTsiTestFixture>(options, event_engine_);
  event_engine_->RunAfter(Duration::Seconds(1),
                          [&fixture] { fixture->Shutdown(); });
  fixture->Run(/*expect_success=*/false, event_engine_.get());
  EXPECT_TRUE(fixture->CertSelectionCancelled());
}

TEST_P(CertSelectionOffloadTest, AsyncCertSelectionWithSyncSignerSucceeds) {
  SslCertSelectorTsiTestFixture::FixtureOptions options;
  options.use_signer = true;
  options.is_cert_selection_async = true;
  options.is_private_key_signing_async = false;
  options.expect_cert_selection_success = true;
  options.expect_private_key_signing_success = true;
  options.tls_version = GetParam();
  auto fixture =
      std::make_shared<SslCertSelectorTsiTestFixture>(options, event_engine_);
  fixture->Run(/*expect_success=*/true, event_engine_.get());
}

TEST_P(CertSelectionOffloadTest, AsyncCertSelectionWithAsyncSignerSucceeds) {
  SslCertSelectorTsiTestFixture::FixtureOptions options;
  options.use_signer = true;
  options.is_cert_selection_async = true;
  options.is_private_key_signing_async = true;
  options.expect_cert_selection_success = true;
  options.expect_private_key_signing_success = true;
  options.tls_version = GetParam();
  auto fixture =
      std::make_shared<SslCertSelectorTsiTestFixture>(options, event_engine_);
  fixture->Run(/*expect_success=*/true, event_engine_.get());
}

std::string TestNameSuffix(
    const ::testing::TestParamInfo<tsi_tls_version>& version) {
  if (version.param == tsi_tls_version::TSI_TLS1_2) return "TLS_1_2";
  return "TLS_1_3";
}

INSTANTIATE_TEST_SUITE_P(CertSelectionOffloadTest, CertSelectionOffloadTest,
                         ::testing::Values(tsi_tls_version::TSI_TLS1_2,
                                           tsi_tls_version::TSI_TLS1_3),
                         TestNameSuffix);

}  // namespace
}  // namespace testing
}  // namespace grpc_core

#endif  // OPENSSL_IS_BORINGSSL

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

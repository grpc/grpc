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
#include <openssl/pem.h>

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/timer_manager.h"
#include "src/core/tsi/ssl_transport_security.h"
#include "src/core/tsi/transport_security_interface.h"
#include "src/core/util/match.h"
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
#include <openssl/bytestring.h>
#include <openssl/mem.h>

#include "src/core/credentials/transport/tls/grpc_tls_certificate_selector.h"
#include "test/core/tsi/private_key_signer_test_util.h"

namespace grpc_core {
namespace testing {
namespace {

using TaskHandle = grpc_event_engine::experimental::EventEngine::TaskHandle;

constexpr absl::string_view kTestCredsRelativePath = "src/core/tsi/test_creds/";
const char kServerName[] = "foo.test.google.fr";

class SyncTestCertificateSelector : public CertificateSelector {
 public:
  SyncTestCertificateSelector(
      absl::string_view pem_cert_chain,
      std::variant<absl::string_view, std::shared_ptr<PrivateKeySigner>>
          pem_private_key,
      bool expect_success = true)
      : pem_cert_chain_(pem_cert_chain),
        pem_private_key_(std::move(pem_private_key)),
        expect_success_(expect_success) {}

  std::variant<absl::StatusOr<SelectCertificateResult>,
               std::shared_ptr<AsyncCertificateSelectionHandle>>
  SelectCertificate(const SelectCertificateInfo& info,
                    OnSelectCertificateComplete) override {
    if (!expect_success_) {
      return absl::InternalError(
          "Failed to select cert when using SyncTestCertificateSelector.");
    }
    if (info.sni != kServerName) {
      return absl::InvalidArgumentError(absl::StrFormat(
          "Expected SNI to be %s, got %s", kServerName, info.sni));
    }
    absl::StatusOr<SelectCertificateResult> result =
        CreateSelectCertificateResult(pem_cert_chain_, pem_private_key_);
    CHECK_OK(result);
    return result;
  }

  void Cancel(
      std::shared_ptr<AsyncCertificateSelectionHandle> /*handle*/) override {}

 private:
  absl::string_view pem_cert_chain_;
  std::variant<absl::string_view, std::shared_ptr<PrivateKeySigner>>
      pem_private_key_;
  bool expect_success_;
};

class AsyncTestCertificateSelector : public CertificateSelector {
 public:
  AsyncTestCertificateSelector(
      absl::string_view pem_cert_chain,
      std::variant<absl::string_view, std::shared_ptr<PrivateKeySigner>>
          pem_private_key,
      std::shared_ptr<grpc_event_engine::experimental::FuzzingEventEngine>
          event_engine,
      bool expect_success = true)
      : pem_cert_chain_(pem_cert_chain),
        pem_private_key_(std::move(pem_private_key)),
        event_engine_(std::move(event_engine)),
        expect_success_(expect_success) {}

  class TestCertSelectionHandle : public AsyncCertificateSelectionHandle {
   public:
    explicit TestCertSelectionHandle(TaskHandle task_handle)
        : task_handle_(task_handle) {}

    TaskHandle task_handle_;
  };

  std::variant<absl::StatusOr<SelectCertificateResult>,
               std::shared_ptr<AsyncCertificateSelectionHandle>>
  SelectCertificate(const SelectCertificateInfo& info,
                    OnSelectCertificateComplete on_complete) override {
    TaskHandle task_handle = event_engine_->RunAfter(
        Duration::Seconds(2),
        [this, info, on_complete = std::move(on_complete)]() mutable {
          was_done_ = true;
          if (!expect_success_) {
            on_complete(
                absl::InternalError("Failed to select cert when using "
                                    "AsyncTestCertificateSelector"));
            return;
          }
          if (info.sni != kServerName) {
            on_complete(absl::InvalidArgumentError(absl::StrFormat(
                "Expected SNI to be %s, got %s", kServerName, info.sni)));
            return;
          }
          on_complete(
              CreateSelectCertificateResult(pem_cert_chain_, pem_private_key_));
        });
    return std::make_shared<TestCertSelectionHandle>(task_handle);
  }

  void Cancel(
      std::shared_ptr<AsyncCertificateSelectionHandle> handle) override {
    event_engine_->Cancel(
        DownCast<TestCertSelectionHandle*>(handle.get())->task_handle_);
    was_cancelled_ = true;
  }

  bool WasCancelled() { return was_cancelled_; }

  bool WasDone() { return was_done_; }

 private:
  absl::string_view pem_cert_chain_;
  std::variant<absl::string_view, std::shared_ptr<PrivateKeySigner>>
      pem_private_key_;
  std::shared_ptr<grpc_event_engine::experimental::FuzzingEventEngine>
      event_engine_;
  bool expect_success_;
  bool was_cancelled_ = false;
  bool was_done_ = false;
};

class HandshakeHintsCertificateSelector : public SyncTestCertificateSelector {
 public:
  HandshakeHintsCertificateSelector(
      absl::string_view pem_cert_chain,
      std::variant<absl::string_view, std::shared_ptr<PrivateKeySigner>>
          pem_private_key,
      tsi_tls_version tls_version,
      std::weak_ptr<PrivateKeySigner>* signer_weak = nullptr)
      : SyncTestCertificateSelector(pem_cert_chain, std::move(pem_private_key)),
        tls_version_(tls_version),
        signer_weak_(signer_weak) {}

  std::variant<absl::StatusOr<SelectCertificateResult>,
               std::shared_ptr<AsyncCertificateSelectionHandle>>
  SelectCertificate(const SelectCertificateInfo& info,
                    OnSelectCertificateComplete on_complete) override {
    auto select_cert_result = SyncTestCertificateSelector::SelectCertificate(
        info, std::move(on_complete));
    GRPC_RETURN_IF_ERROR(MatchMutable(
        &select_cert_result,
        [&](absl::StatusOr<SelectCertificateResult>*
                select_cert_result) mutable {
          if (select_cert_result->ok()) {
            auto& result = *select_cert_result;
            // Perform an internal handshake to generate real hints.
            bssl::UniquePtr<SSL_CTX> ctx(SSL_CTX_new(TLS_method()));
            CHECK(ctx != nullptr);
            // Important for TLS 1.3.
            SSL_CTX_set_tlsext_servername_callback(
                ctx.get(),
                +[](SSL* /*ssl*/, int* /*out_alert*/, void* /*arg*/) {
                  // SSL_TLSEXT_ERR_OK causes the server_name to be acked in
                  // ServerHello.
                  return SSL_TLSEXT_ERR_OK;
                });
            bssl::UniquePtr<SSL> ssl(SSL_new(ctx.get()));
            CHECK(ssl != nullptr);
            SSL_set_accept_state(ssl.get());
            std::vector<CRYPTO_BUFFER*> cert_chain;
            cert_chain.reserve(result->certificate_chain.size());
            for (auto& cert : result->certificate_chain) {
              cert_chain.push_back(cert.get());
            }
            bssl::UniquePtr<EVP_PKEY> private_key;
            GRPC_RETURN_IF_ERROR(MatchMutable(
                &result->private_key,
                [&private_key](bssl::UniquePtr<EVP_PKEY>* key) {
                  private_key = std::move(*key);
                  return absl::OkStatus();
                },
                [](std::shared_ptr<PrivateKeySigner>*) {
                  return absl::InternalError("PrivateKeySigner not expected");
                }));
            // This signer ensures test will fail if handshake hints don't work
            // properly.
            auto signer = std::make_shared<SyncTestPrivateKeySigner>(
                "", SyncTestPrivateKeySigner::Mode::kError);
            if (signer_weak_ != nullptr) {
              *signer_weak_ = signer;
            }
            result->private_key = std::move(signer);
            // Use AssignCertificate to configure hints generation.
            uint16_t version =
                (tls_version_ == TSI_TLS1_2) ? TLS1_2_VERSION : TLS1_3_VERSION;
            if (!SSL_set_chain_and_key(ssl.get(), cert_chain.data(),
                                       cert_chain.size(), private_key.get(),
                                       nullptr)) {
              return absl::InternalError("failed to set SSL chain and key");
            }
            if (!SSL_set_strict_cipher_list(ssl.get(),
                                            SSL_DEFAULT_CIPHER_LIST)) {
              return absl::InternalError("failed to set SSL cipher list");
            }
            // No session resumption.
            SSL_set_options(ssl.get(),
                            SSL_OP_CIPHER_SERVER_PREFERENCE | SSL_OP_NO_TICKET);
            if (!SSL_set_min_proto_version(ssl.get(), version)) {
              return absl::InternalError("failed to set min TLS version");
            }
            if (!SSL_set_max_proto_version(ssl.get(), version)) {
              return absl::InternalError("failed to set max TLS version");
            }
            // Enforce the preference of post-quantum key exchange groups.
            std::vector<uint16_t> key_exchange_groups = {
                SSL_GROUP_X25519_MLKEM768, SSL_GROUP_X25519,
                SSL_GROUP_SECP256R1, SSL_GROUP_SECP384R1};
            if (!SSL_set1_group_ids(ssl.get(), key_exchange_groups.data(),
                                    key_exchange_groups.size())) {
              return absl::InternalError("failed to set key exchange groups.");
            }
            result->handshake_hints_result.key_exchange_groups =
                std::move(key_exchange_groups);
            if (!SSL_request_handshake_hints(
                    ssl.get(),
                    reinterpret_cast<const uint8_t*>(
                        info.handshake_hints_info.client_hello.data()),
                    info.handshake_hints_info.client_hello.size(),
                    reinterpret_cast<const uint8_t*>(
                        info.handshake_hints_info.ssl_capabilities.data()),
                    info.handshake_hints_info.ssl_capabilities.size())) {
              return absl::InternalError("SSL_request_handshake_hints failed");
            }
            // Drive the handshake.
            int ret = SSL_do_handshake(ssl.get());
            int ssl_error = SSL_get_error(ssl.get(), ret);
            CHECK_EQ(ssl_error, SSL_ERROR_HANDSHAKE_HINTS_READY);
            // Extract real hints.
            bssl::ScopedCBB hints_cbb;
            CHECK(CBB_init(hints_cbb.get(), 256));
            CHECK(SSL_serialize_handshake_hints(ssl.get(), hints_cbb.get()));
            result->handshake_hints_result.handshake_hints = std::string(
                reinterpret_cast<const char*>(CBB_data(hints_cbb.get())),
                CBB_len(hints_cbb.get()));
            // This SSL object and the one within TSI are in the same process
            // with the same BoringSSL library so the consistency is guaranteed.
            result->handshake_hints_result.cipher_list =
                SSL_DEFAULT_CIPHER_LIST;
            uint16_t negotiated_version = SSL_version(ssl.get());
            result->handshake_hints_result.min_tls_version = negotiated_version;
            result->handshake_hints_result.max_tls_version = negotiated_version;
          }
          return absl::OkStatus();
        },
        [](std::shared_ptr<AsyncCertificateSelectionHandle>*) {
          return absl::InternalError("Expected synchronous cert selection.");
        }));
    return select_cert_result;
  }

 private:
  tsi_tls_version tls_version_;
  std::weak_ptr<PrivateKeySigner>* signer_weak_ = nullptr;
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

  void SetCertSelector(std::shared_ptr<CertificateSelector> cert_selector) {
    cert_selector_ = std::move(cert_selector);
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

  bool CertSelectionDone() {
    return reinterpret_cast<AsyncTestCertificateSelector*>(cert_selector_.get())
        ->WasDone();
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
    server_options.key_cert_pairs_or_selector = cert_selector_;
    ASSERT_EQ(tsi_create_ssl_server_handshaker_factory_with_options(
                  &server_options, &server_handshaker_factory_),
              TSI_OK);

    // Create handshakers.
    tsi_handshaker* client_hs;
    tsi_handshaker* server_hs;
    ASSERT_EQ(tsi_ssl_client_handshaker_factory_create_handshaker(
                  client_handshaker_factory_, kServerName, 0, 0, std::nullopt,
                  /*collection_scope=*/nullptr, /*target=*/kServerName,
                  /*locality=*/"", /*backend_service=*/"", &client_hs),
              TSI_OK);
    ASSERT_EQ(tsi_ssl_server_handshaker_factory_create_handshaker(
                  server_handshaker_factory_, 0, 0,
                  /*collection_scope=*/nullptr, &server_hs),
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

TEST_P(CertSelectionOffloadTest, AsyncCertSelectionCancelledBeforeFinished) {
  SslCertSelectorTsiTestFixture::FixtureOptions options;
  options.is_cert_selection_async = true;
  options.expect_cert_selection_success = true;
  options.tls_version = GetParam();
  auto fixture =
      std::make_shared<SslCertSelectorTsiTestFixture>(options, event_engine_);
  // The shutdown of the fixture happens before the async cert selection.
  event_engine_->RunAfter(Duration::Seconds(1),
                          [&fixture] { fixture->Shutdown(); });
  fixture->Run(/*expect_success=*/false, event_engine_.get());
  EXPECT_TRUE(fixture->CertSelectionCancelled());
  EXPECT_FALSE(fixture->CertSelectionDone());
}

TEST_P(CertSelectionOffloadTest, AsyncCertSelectionCancelledAfterFinished) {
  SslCertSelectorTsiTestFixture::FixtureOptions options;
  options.is_cert_selection_async = true;
  options.expect_cert_selection_success = true;
  options.tls_version = GetParam();
  auto fixture =
      std::make_shared<SslCertSelectorTsiTestFixture>(options, event_engine_);
  // The shutdown of the fixture happens after the async cert selection.
  event_engine_->RunAfter(Duration::Seconds(3),
                          [&fixture] { fixture->Shutdown(); });
  fixture->Run(/*expect_success=*/false, event_engine_.get());
  // The handle should have been reset by the handshaker so no cancellation.
  EXPECT_FALSE(fixture->CertSelectionCancelled());
  EXPECT_TRUE(fixture->CertSelectionDone());
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

TEST_P(CertSelectionOffloadTest, HandshakeHintsSucceeds) {
  SslCertSelectorTsiTestFixture::FixtureOptions options;
  options.tls_version = GetParam();
  auto fixture =
      std::make_shared<SslCertSelectorTsiTestFixture>(options, event_engine_);
  std::string server_key =
      GetFileContents(absl::StrCat(kTestCredsRelativePath, "server1.key"));
  std::string server_cert =
      GetFileContents(absl::StrCat(kTestCredsRelativePath, "server1.pem"));
  fixture->SetCertSelector(std::make_shared<HandshakeHintsCertificateSelector>(
      server_cert, server_key, GetParam()));
  fixture->Run(/*expect_success=*/true, event_engine_.get());
}

class TrackingPrivateKeySigner final : public PrivateKeySigner {
 public:
  TrackingPrivateKeySigner(std::shared_ptr<PrivateKeySigner> inner,
                           std::shared_ptr<std::mutex> shared_mu,
                           std::atomic<bool>* cancelled,
                           std::atomic<bool>* destroyed)
      : inner_(std::move(inner)),
        shared_mu_(std::move(shared_mu)),
        cancelled_(cancelled),
        destroyed_(destroyed) {}

  ~TrackingPrivateKeySigner() override {
    if (shared_mu_ != nullptr) {
      std::lock_guard<std::mutex> lock(*shared_mu_);
      if (destroyed_ != nullptr) destroyed_->store(true);
    } else if (destroyed_ != nullptr) {
      destroyed_->store(true);
    }
  }

  std::variant<absl::StatusOr<std::string>, std::shared_ptr<AsyncSigningHandle>>
  Sign(absl::string_view data_to_sign, SignatureAlgorithm signature_algorithm,
       OnSignComplete on_sign_complete) override {
    return inner_->Sign(data_to_sign, signature_algorithm,
                        std::move(on_sign_complete));
  }

  void Cancel(std::shared_ptr<AsyncSigningHandle> handle) override {
    if (shared_mu_ != nullptr) {
      std::lock_guard<std::mutex> lock(*shared_mu_);
      inner_->Cancel(std::move(handle));
    } else {
      inner_->Cancel(std::move(handle));
    }
    if (cancelled_ != nullptr) cancelled_->store(true);
  }

 private:
  std::shared_ptr<PrivateKeySigner> inner_;
  std::shared_ptr<std::mutex> shared_mu_;
  std::atomic<bool>* cancelled_;
  std::atomic<bool>* destroyed_;
};

class FactorySignerCertificateSelector final : public CertificateSelector {
 public:
  using SignerFactory = std::function<std::shared_ptr<PrivateKeySigner>()>;

  FactorySignerCertificateSelector(
      absl::string_view pem_cert_chain, SignerFactory signer_factory,
      std::shared_ptr<grpc_event_engine::experimental::FuzzingEventEngine>
          event_engine,
      std::shared_ptr<std::mutex> shared_mu, bool is_async,
      std::weak_ptr<PrivateKeySigner>* signer_weak)
      : pem_cert_chain_(pem_cert_chain),
        signer_factory_(std::move(signer_factory)),
        event_engine_(std::move(event_engine)),
        shared_mu_(std::move(shared_mu)),
        is_async_(is_async),
        signer_weak_(signer_weak) {}

  std::variant<absl::StatusOr<SelectCertificateResult>,
               std::shared_ptr<AsyncCertificateSelectionHandle>>
  SelectCertificate(const SelectCertificateInfo& /*info*/,
                    OnSelectCertificateComplete on_complete) override {
    if (!is_async_) {
      auto signer = signer_factory_();
      if (signer_weak_ != nullptr) *signer_weak_ = signer;
      return CreateSelectCertificateResult(pem_cert_chain_, std::move(signer));
    }
    event_engine_->RunAfter(
        Duration::Seconds(2),
        [this, on_complete = std::move(on_complete)]() mutable {
          auto signer = signer_factory_();
          if (signer_weak_ != nullptr) *signer_weak_ = signer;
          auto res =
              CreateSelectCertificateResult(pem_cert_chain_, std::move(signer));
          if (shared_mu_ != nullptr) {
            std::lock_guard<std::mutex> lock(*shared_mu_);
            on_complete(std::move(res));
          } else {
            on_complete(std::move(res));
          }
        });
    return std::make_shared<AsyncCertificateSelectionHandle>();
  }

  void Cancel(
      std::shared_ptr<AsyncCertificateSelectionHandle> /*handle*/) override {}

 private:
  absl::string_view pem_cert_chain_;
  SignerFactory signer_factory_;
  std::shared_ptr<grpc_event_engine::experimental::FuzzingEventEngine>
      event_engine_;
  std::shared_ptr<std::mutex> shared_mu_;
  bool is_async_;
  std::weak_ptr<PrivateKeySigner>* signer_weak_;
};

struct StepResult {
  tsi_result status = TSI_INTERNAL_ERROR;
  std::vector<unsigned char> bytes_to_send;
  tsi_handshaker_result* handshaker_result = nullptr;
};

struct PendingStepState {
  std::mutex mu;
  std::condition_variable cv;
  bool done = false;
  StepResult step;
};

void OnSteppedNextDone(tsi_result result, void* user_data,
                       const unsigned char* bytes_to_send,
                       size_t bytes_to_send_size,
                       tsi_handshaker_result* handshaker_result) {
  auto* state = static_cast<PendingStepState*>(user_data);
  std::lock_guard<std::mutex> lock(state->mu);
  state->step.status = result;
  if (bytes_to_send != nullptr && bytes_to_send_size > 0) {
    state->step.bytes_to_send.assign(bytes_to_send,
                                     bytes_to_send + bytes_to_send_size);
  }
  state->step.handshaker_result = handshaker_result;
  state->done = true;
  state->cv.notify_all();
}

class SteppedCertSelectorSession {
 public:
  SteppedCertSelectorSession(
      tsi_tls_version tls_version,
      std::shared_ptr<CertificateSelector> cert_selector,
      grpc_event_engine::experimental::FuzzingEventEngine* event_engine)
      : event_engine_(event_engine) {
    std::string ca_cert =
        GetFileContents(absl::StrCat(kTestCredsRelativePath, "ca.pem"));

    tsi_ssl_client_handshaker_options client_options;
    client_options.root_cert_info =
        std::make_shared<tsi::RootCertInfo>(ca_cert);
    client_options.min_tls_version = tls_version;
    client_options.max_tls_version = tls_version;
    CHECK_EQ(tsi_create_ssl_client_handshaker_factory_with_options(
                 &client_options, &client_factory_),
             TSI_OK);

    tsi_ssl_server_handshaker_options server_options;
    server_options.client_certificate_request =
        TSI_DONT_REQUEST_CLIENT_CERTIFICATE;
    server_options.min_tls_version = tls_version;
    server_options.max_tls_version = tls_version;
    server_options.key_cert_pairs_or_selector = std::move(cert_selector);
    CHECK_EQ(tsi_create_ssl_server_handshaker_factory_with_options(
                 &server_options, &server_factory_),
             TSI_OK);

    CHECK_EQ(tsi_ssl_client_handshaker_factory_create_handshaker(
                 client_factory_, kServerName, 0, 0, std::nullopt,
                 /*collection_scope=*/nullptr, /*target=*/kServerName,
                 /*locality=*/"", /*backend_service=*/"", &client_hs_),
             TSI_OK);
    CHECK_EQ(
        tsi_ssl_server_handshaker_factory_create_handshaker(
            server_factory_, 0, 0, /*collection_scope=*/nullptr, &server_hs_),
        TSI_OK);
  }

  ~SteppedCertSelectorSession() {
    if (client_result_ != nullptr) {
      tsi_handshaker_result_destroy(client_result_);
    }
    if (server_result_ != nullptr) {
      tsi_handshaker_result_destroy(server_result_);
    }
    if (client_hs_ != nullptr) tsi_handshaker_destroy(client_hs_);
    if (server_hs_ != nullptr) tsi_handshaker_destroy(server_hs_);
    if (client_factory_ != nullptr) {
      tsi_ssl_client_handshaker_factory_unref(client_factory_);
    }
    if (server_factory_ != nullptr) {
      tsi_ssl_server_handshaker_factory_unref(server_factory_);
    }
  }

  StepResult Step(tsi_handshaker* hs, const std::vector<unsigned char>& in) {
    PendingStepState state;
    const unsigned char* out_bytes = nullptr;
    size_t out_bytes_size = 0;
    tsi_handshaker_result* hs_result = nullptr;
    tsi_result res = tsi_handshaker_next(
        hs, in.empty() ? nullptr : in.data(), in.size(), &out_bytes,
        &out_bytes_size, &hs_result, &OnSteppedNextDone, &state);
    if (res != TSI_ASYNC) {
      state.step.status = res;
      if (out_bytes != nullptr && out_bytes_size > 0) {
        state.step.bytes_to_send.assign(out_bytes, out_bytes + out_bytes_size);
      }
      state.step.handshaker_result = hs_result;
      return std::move(state.step);
    }
    event_engine_->TickUntilIdle();
    std::unique_lock<std::mutex> lock(state.mu);
    state.cv.wait(lock, [&state]() { return state.done; });
    return std::move(state.step);
  }

  void DriveRemainingHandshake(
      std::vector<unsigned char> pending_to_client,
      std::vector<unsigned char> pending_to_server = {}) {
    while (client_result_ == nullptr || server_result_ == nullptr) {
      bool progressed = false;
      if (client_result_ == nullptr && !pending_to_client.empty()) {
        progressed = true;
        StepResult c = Step(client_hs_, pending_to_client);
        pending_to_client.clear();
        ASSERT_EQ(c.status, TSI_OK);
        if (c.handshaker_result != nullptr) {
          client_result_ = c.handshaker_result;
        }
        pending_to_server.insert(pending_to_server.end(),
                                 c.bytes_to_send.begin(),
                                 c.bytes_to_send.end());
      }
      if (server_result_ == nullptr && !pending_to_server.empty()) {
        progressed = true;
        StepResult s = Step(server_hs_, pending_to_server);
        pending_to_server.clear();
        ASSERT_EQ(s.status, TSI_OK);
        if (s.handshaker_result != nullptr) {
          server_result_ = s.handshaker_result;
        }
        pending_to_client.insert(pending_to_client.end(),
                                 s.bytes_to_send.begin(),
                                 s.bytes_to_send.end());
      }
      ASSERT_TRUE(progressed);
    }
    tsi_peer peer;
    EXPECT_EQ(tsi_handshaker_result_extract_peer(client_result_, &peer),
              TSI_OK);
    tsi_peer_destruct(&peer);
    EXPECT_EQ(tsi_handshaker_result_extract_peer(server_result_, &peer),
              TSI_OK);
    tsi_peer_destruct(&peer);
  }

  tsi_handshaker* client_hs() { return client_hs_; }
  tsi_handshaker* server_hs() { return server_hs_; }
  tsi_handshaker_result* client_result() { return client_result_; }
  tsi_handshaker_result* server_result() { return server_result_; }

 private:
  grpc_event_engine::experimental::FuzzingEventEngine* event_engine_;
  tsi_ssl_client_handshaker_factory* client_factory_ = nullptr;
  tsi_ssl_server_handshaker_factory* server_factory_ = nullptr;
  tsi_handshaker* client_hs_ = nullptr;
  tsi_handshaker* server_hs_ = nullptr;
  tsi_handshaker_result* client_result_ = nullptr;
  tsi_handshaker_result* server_result_ = nullptr;
};

TEST_P(CertSelectionOffloadTest, ServerReleasesKeySignerBeforeClientFinished) {
  std::string server_key =
      GetFileContents(absl::StrCat(kTestCredsRelativePath, "server1.key"));
  std::string server_cert =
      GetFileContents(absl::StrCat(kTestCredsRelativePath, "server1.pem"));
  auto shared_mu = std::make_shared<std::mutex>();
  std::atomic<bool> cancelled{false};
  std::atomic<bool> destroyed{false};
  std::weak_ptr<PrivateKeySigner> signer_weak;

  auto selector = std::make_shared<FactorySignerCertificateSelector>(
      server_cert,
      [&, this]() {
        auto inner = std::make_shared<AsyncTestPrivateKeySigner>(server_key,
                                                                 event_engine_);
        return std::make_shared<TrackingPrivateKeySigner>(
            std::move(inner), shared_mu, &cancelled, &destroyed);
      },
      event_engine_, shared_mu, /*is_async=*/true, &signer_weak);
  SteppedCertSelectorSession session(GetParam(), std::move(selector),
                                     event_engine_.get());

  StepResult client_hello = session.Step(session.client_hs(), {});
  ASSERT_EQ(client_hello.status, TSI_OK);
  ASSERT_FALSE(client_hello.bytes_to_send.empty());

  // Server produces ServerHello..ServerFinished (or ServerHelloDone) and waits
  // for ClientFinished/ClientKeyExchange. The key signer must already be
  // released and destroyed outside handshaker::mu before peer response arrives.
  StepResult server_flight =
      session.Step(session.server_hs(), client_hello.bytes_to_send);
  ASSERT_EQ(server_flight.status, TSI_OK);
  EXPECT_EQ(server_flight.handshaker_result, nullptr);
  EXPECT_FALSE(server_flight.bytes_to_send.empty());
  EXPECT_TRUE(signer_weak.expired());
  EXPECT_TRUE(destroyed.load());
  EXPECT_FALSE(cancelled.load());

  session.DriveRemainingHandshake(std::move(server_flight.bytes_to_send));
}

TEST_P(CertSelectionOffloadTest,
       HandshakeHintsWithKeySignerReleasesSignerEarly) {
  std::string server_key =
      GetFileContents(absl::StrCat(kTestCredsRelativePath, "server1.key"));
  std::string server_cert =
      GetFileContents(absl::StrCat(kTestCredsRelativePath, "server1.pem"));
  std::weak_ptr<PrivateKeySigner> signer_weak;
  auto selector = std::make_shared<HandshakeHintsCertificateSelector>(
      server_cert, server_key, GetParam(), &signer_weak);
  SteppedCertSelectorSession session(GetParam(), std::move(selector),
                                     event_engine_.get());

  StepResult client_hello = session.Step(session.client_hs(), {});
  ASSERT_EQ(client_hello.status, TSI_OK);
  ASSERT_FALSE(client_hello.bytes_to_send.empty());

  // With handshake hints, PrivateKeySigner::Sign() is skipped; verify the
  // signer is still released right after ServerFinished is produced.
  StepResult server_flight =
      session.Step(session.server_hs(), client_hello.bytes_to_send);
  ASSERT_EQ(server_flight.status, TSI_OK);
  EXPECT_EQ(server_flight.handshaker_result, nullptr);
  EXPECT_FALSE(server_flight.bytes_to_send.empty());
  EXPECT_TRUE(signer_weak.expired());

  session.DriveRemainingHandshake(std::move(server_flight.bytes_to_send));
}

TEST_P(CertSelectionOffloadTest,
       ShutdownReleasesKeySignerWithoutSigningHandle) {
  std::string server_cert =
      GetFileContents(absl::StrCat(kTestCredsRelativePath, "server1.pem"));
  std::atomic<bool> cancelled{false};
  std::atomic<bool> destroyed{false};
  std::weak_ptr<PrivateKeySigner> signer_weak;

  // Synchronous signer failure fails SSL_do_handshake before
  // SSL_can_release_private_key() turns true and leaves signing_handle null.
  auto selector = std::make_shared<FactorySignerCertificateSelector>(
      server_cert,
      [&]() {
        auto inner = std::make_shared<SyncTestPrivateKeySigner>(
            "", SyncTestPrivateKeySigner::Mode::kError);
        return std::make_shared<TrackingPrivateKeySigner>(
            std::move(inner), nullptr, &cancelled, &destroyed);
      },
      event_engine_, nullptr, /*is_async=*/false, &signer_weak);
  SteppedCertSelectorSession session(GetParam(), std::move(selector),
                                     event_engine_.get());

  StepResult client_hello = session.Step(session.client_hs(), {});
  ASSERT_EQ(client_hello.status, TSI_OK);

  StepResult server_flight =
      session.Step(session.server_hs(), client_hello.bytes_to_send);
  EXPECT_NE(server_flight.status, TSI_OK);
  EXPECT_EQ(server_flight.handshaker_result, nullptr);
  EXPECT_FALSE(signer_weak.expired());
  EXPECT_FALSE(destroyed.load());

  // Shutdown must release key_signer even when signing_handle is null, without
  // waiting for tsi_handshaker_destroy().
  tsi_handshaker_shutdown(session.server_hs(), /*peer_closed=*/false);
  EXPECT_TRUE(signer_weak.expired());
  EXPECT_TRUE(destroyed.load());
  EXPECT_FALSE(cancelled.load());
}

TEST_P(CertSelectionOffloadTest,
       ShutdownWhileAsyncSignPendingCancelsAndReleasesKeySigner) {
  std::string server_key =
      GetFileContents(absl::StrCat(kTestCredsRelativePath, "server1.key"));
  std::string server_cert =
      GetFileContents(absl::StrCat(kTestCredsRelativePath, "server1.pem"));
  auto shared_mu = std::make_shared<std::mutex>();
  std::atomic<bool> cancelled{false};
  std::atomic<bool> destroyed{false};
  std::weak_ptr<PrivateKeySigner> signer_weak;

  auto selector = std::make_shared<FactorySignerCertificateSelector>(
      server_cert,
      [&, this]() {
        auto inner = std::make_shared<AsyncTestPrivateKeySigner>(
            server_key, event_engine_,
            AsyncTestPrivateKeySigner::Mode::kCancellation);
        return std::make_shared<TrackingPrivateKeySigner>(
            std::move(inner), shared_mu, &cancelled, &destroyed);
      },
      event_engine_, shared_mu, /*is_async=*/true, &signer_weak);
  SteppedCertSelectorSession session(GetParam(), std::move(selector),
                                     event_engine_.get());

  StepResult client_hello = session.Step(session.client_hs(), {});
  ASSERT_EQ(client_hello.status, TSI_OK);

  PendingStepState server_state;
  const unsigned char* out_bytes = nullptr;
  size_t out_bytes_size = 0;
  tsi_handshaker_result* hs_result = nullptr;
  ASSERT_EQ(tsi_handshaker_next(
                session.server_hs(), client_hello.bytes_to_send.data(),
                client_hello.bytes_to_send.size(), &out_bytes, &out_bytes_size,
                &hs_result, &OnSteppedNextDone, &server_state),
            TSI_ASYNC);
  event_engine_->TickUntilIdle();
  EXPECT_FALSE(signer_weak.expired());
  EXPECT_FALSE(destroyed.load());

  tsi_handshaker_shutdown(session.server_hs(), /*peer_closed=*/false);
  EXPECT_TRUE(cancelled.load());
  EXPECT_TRUE(signer_weak.expired());
  EXPECT_TRUE(destroyed.load());

  event_engine_->TickUntilIdle();
  std::unique_lock<std::mutex> lock(server_state.mu);
  server_state.cv.wait(lock, [&server_state]() { return server_state.done; });
  EXPECT_EQ(server_state.step.status, TSI_HANDSHAKE_SHUTDOWN);
  EXPECT_EQ(server_state.step.handshaker_result, nullptr);
}

TEST_P(CertSelectionOffloadTest,
       CompleteHandshakeReleasesKeySignerWithoutShutdown) {
  std::string server_key =
      GetFileContents(absl::StrCat(kTestCredsRelativePath, "server1.key"));
  std::string server_cert =
      GetFileContents(absl::StrCat(kTestCredsRelativePath, "server1.pem"));
  std::atomic<bool> cancelled{false};
  std::atomic<bool> destroyed{false};
  std::weak_ptr<PrivateKeySigner> signer_weak;

  auto selector = std::make_shared<FactorySignerCertificateSelector>(
      server_cert,
      [&, this]() {
        auto inner = std::make_shared<AsyncTestPrivateKeySigner>(server_key,
                                                                 event_engine_);
        return std::make_shared<TrackingPrivateKeySigner>(
            std::move(inner), nullptr, &cancelled, &destroyed);
      },
      event_engine_, nullptr, /*is_async=*/false, &signer_weak);
  SteppedCertSelectorSession session(GetParam(), std::move(selector),
                                     event_engine_.get());

  StepResult client_hello = session.Step(session.client_hs(), {});
  ASSERT_EQ(client_hello.status, TSI_OK);
  session.DriveRemainingHandshake({}, std::move(client_hello.bytes_to_send));
  // Both server_hs and client_hs remain alive inside |session| until teardown,
  // yet the key_signer is already destroyed without needing shutdown().
  EXPECT_NE(session.server_result(), nullptr);
  EXPECT_NE(session.client_result(), nullptr);
  EXPECT_TRUE(signer_weak.expired());
  EXPECT_TRUE(destroyed.load());
  EXPECT_FALSE(cancelled.load());
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

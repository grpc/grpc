/*
 *
 * Copyright 2018 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "src/core/lib/security/security_connector/tls/tls_security_connector.h"

#include <gmock/gmock.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <gtest/gtest.h>
#include <stdlib.h>
#include <string.h>

#include "src/core/lib/iomgr/load_file.h"
#include "src/core/lib/security/credentials/tls/grpc_tls_certificate_provider.h"
#include "src/core/lib/security/credentials/tls/tls_credentials.h"
#include "src/core/tsi/transport_security.h"
#include "test/core/util/test_config.h"

#define CA_CERT_PATH "src/core/tsi/test_creds/ca.pem"
#define CLIENT_CERT_PATH "src/core/tsi/test_creds/multi-domain.pem"
#define SERVER_CERT_PATH_0 "src/core/tsi/test_creds/server0.pem"
#define SERVER_KEY_PATH_0 "src/core/tsi/test_creds/server0.key"
#define SERVER_CERT_PATH_1 "src/core/tsi/test_creds/server1.pem"
#define SERVER_KEY_PATH_1 "src/core/tsi/test_creds/server1.key"

namespace grpc {
namespace testing {

constexpr const char* kRootCertName = "root_cert_name";
constexpr const char* kIdentityCertName = "identity_cert_name";
constexpr const char* kErrorMessage = "error_message";
constexpr const char* kTargetName = "foo.bar.com:443";

class TlsSecurityConnectorTest : public ::testing::Test {
 protected:
  TlsSecurityConnectorTest() {}
  void SetUp() override {
    grpc_slice ca_slice_1, ca_slice_0, cert_slice_1, key_slice_1, cert_slice_0,
        key_slice_0;
    GPR_ASSERT(GRPC_LOG_IF_ERROR("load_file",
                                 grpc_load_file(CA_CERT_PATH, 1, &ca_slice_1)));
    GPR_ASSERT(GRPC_LOG_IF_ERROR(
        "load_file", grpc_load_file(CLIENT_CERT_PATH, 1, &ca_slice_0)));
    GPR_ASSERT(GRPC_LOG_IF_ERROR(
        "load_file", grpc_load_file(SERVER_CERT_PATH_1, 1, &cert_slice_1)));
    GPR_ASSERT(GRPC_LOG_IF_ERROR(
        "load_file", grpc_load_file(SERVER_KEY_PATH_1, 1, &key_slice_1)));
    GPR_ASSERT(GRPC_LOG_IF_ERROR(
        "load_file", grpc_load_file(SERVER_CERT_PATH_0, 1, &cert_slice_0)));
    GPR_ASSERT(GRPC_LOG_IF_ERROR(
        "load_file", grpc_load_file(SERVER_KEY_PATH_0, 1, &key_slice_0)));
    root_cert_1_ = std::string(grpc_core::StringViewFromSlice(ca_slice_1));
    root_cert_0_ = std::string(grpc_core::StringViewFromSlice(ca_slice_0));
    std::string identity_key_1 =
        std::string(grpc_core::StringViewFromSlice(key_slice_1));
    std::string identity_key_0 =
        std::string(grpc_core::StringViewFromSlice(key_slice_0));
    std::string identity_cert_1 =
        std::string(grpc_core::StringViewFromSlice(cert_slice_1));
    std::string identity_cert_0 =
        std::string(grpc_core::StringViewFromSlice(cert_slice_0));
    identity_pairs_1_.emplace_back(identity_key_1, identity_cert_1);
    identity_pairs_0_.emplace_back(identity_key_0, identity_cert_0);
    grpc_slice_unref(ca_slice_1);
    grpc_slice_unref(ca_slice_0);
    grpc_slice_unref(cert_slice_1);
    grpc_slice_unref(key_slice_1);
    grpc_slice_unref(cert_slice_0);
    grpc_slice_unref(key_slice_0);
  }

  void TearDown() override {
    gpr_log(GPR_ERROR, "7");
    grpc_tls_certificate_verifier::CertificateVerificationRequestDestroy(
        &request_);
    gpr_log(GPR_ERROR, "8");
    if (external_certificate_verifier_ != nullptr) {
      delete external_certificate_verifier_;
    }
    gpr_log(GPR_ERROR, "9");
  }

  void CreateExternalCertificateVerifier(
      int (*verify)(grpc_tls_certificate_verifier_external* external_verifier,
                    grpc_tls_custom_verification_check_request* request,
                    grpc_tls_on_custom_verification_check_done_cb callback,
                    void* callback_arg),
      void (*cancel)(grpc_tls_certificate_verifier_external* external_verifier,
                     grpc_tls_custom_verification_check_request* request),
      void (*destruct)(
          grpc_tls_certificate_verifier_external* external_verifier)) {
    grpc_tls_certificate_verifier_external* external_verifier =
        new grpc_tls_certificate_verifier_external();
    external_verifier->verify = verify;
    external_verifier->cancel = cancel;
    external_verifier->destruct = destruct;
    // Takes the ownership of external_verifier.
    external_certificate_verifier_ =
        new grpc_core::ExternalCertificateVerifier(external_verifier);
  }

  static void grpcClosureCb(void* arg, grpc_error* error) {
    grpc_error* expected_error = static_cast<grpc_error*>(arg);
    if (error == GRPC_ERROR_NONE) {
      EXPECT_EQ(expected_error, GRPC_ERROR_NONE);
    } else {
      EXPECT_STREQ(GetErrorMsg(expected_error).c_str(),
                   GetErrorMsg(error).c_str());
    }
    GRPC_ERROR_UNREF(expected_error);
  }

  static std::string GetErrorMsg(grpc_error* error) {
    grpc_slice error_slice;
    GPR_ASSERT(
        grpc_error_get_str(error, GRPC_ERROR_STR_DESCRIPTION, &error_slice));
    return std::string(grpc_core::StringViewFromSlice(error_slice));
  }

  static int SyncExternalVerifierGoodVerify(
      grpc_tls_certificate_verifier_external* external_verifier,
      grpc_tls_custom_verification_check_request* request,
      grpc_tls_on_custom_verification_check_done_cb callback,
      void* callback_arg) {
    request->status = GRPC_STATUS_OK;
    return false;
  }

  static int SyncExternalVerifierBadVerify(
      grpc_tls_certificate_verifier_external* external_verifier,
      grpc_tls_custom_verification_check_request* request,
      grpc_tls_on_custom_verification_check_done_cb callback,
      void* callback_arg) {
    request->status = GRPC_STATUS_UNAUTHENTICATED;
    request->error_details = gpr_strdup("SyncExternalVerifierBadVerify failed");
    return false;
  }

  struct ThreadArgs {
    grpc_tls_custom_verification_check_request* request;
    grpc_tls_on_custom_verification_check_done_cb callback;
    void* callback_arg;
  };

  static int AsyncExternalVerifierBadVerify(
      grpc_tls_certificate_verifier_external* external_verifier,
      grpc_tls_custom_verification_check_request* request,
      grpc_tls_on_custom_verification_check_done_cb callback,
      void* callback_arg) {
    ThreadArgs* thread_args = new ThreadArgs();
    thread_args->request = request;
    thread_args->callback = callback;
    thread_args->callback_arg = callback_arg;
    grpc_core::Thread thread = grpc_core::Thread(
        "AsyncExternalVerifierBadVerify", &AsyncExternalVerifierBadVerifyCb,
        static_cast<void*>(thread_args));
    thread.Start();
    // Question: This still behaves like a sync operation. I wanted to return
    // true first, and then at the main thread or TearDown function we join the
    // thread. Is it possible?
    thread.Join();
    return true;
  }

  static void AsyncExternalVerifierBadVerifyCb(void* args) {
    ThreadArgs* thread_args = static_cast<ThreadArgs*>(args);
    grpc_tls_custom_verification_check_request* request = thread_args->request;
    grpc_tls_on_custom_verification_check_done_cb callback =
        thread_args->callback;
    void* callback_arg = thread_args->callback_arg;
    request->status = GRPC_STATUS_UNAUTHENTICATED;
    request->error_details =
        gpr_strdup("AsyncExternalVerifierBadVerify failed");
    gpr_log(GPR_ERROR, "this is called0");
    callback(request, callback_arg);
    delete thread_args;
  }

  std::string root_cert_1_;
  std::string root_cert_0_;
  grpc_core::PemKeyCertPairList identity_pairs_1_;
  grpc_core::PemKeyCertPairList identity_pairs_0_;
  grpc_tls_custom_verification_check_request request_;
  grpc_core::ExternalCertificateVerifier* external_certificate_verifier_ =
      nullptr;
  grpc_core::HostNameCertificateVerifier hostname_certificate_verifier_;
};

class TlsTestCertificateProvider : public ::grpc_tls_certificate_provider {
 public:
  explicit TlsTestCertificateProvider(
      grpc_core::RefCountedPtr<grpc_tls_certificate_distributor> distributor)
      : distributor_(std::move(distributor)) {}
  ~TlsTestCertificateProvider() override {}
  grpc_core::RefCountedPtr<grpc_tls_certificate_distributor> distributor()
      const override {
    return distributor_;
  }

 private:
  grpc_core::RefCountedPtr<grpc_tls_certificate_distributor> distributor_;
};

//
// Tests for Certificate Providers in ChannelSecurityConnector.
//

TEST_F(TlsSecurityConnectorTest,
       RootAndIdentityCertsObtainedWhenCreateChannelSecurityConnector) {
  grpc_core::RefCountedPtr<grpc_tls_certificate_distributor> distributor =
      grpc_core::MakeRefCounted<grpc_tls_certificate_distributor>();
  distributor->SetKeyMaterials(kRootCertName, root_cert_0_, absl::nullopt);
  distributor->SetKeyMaterials(kIdentityCertName, absl::nullopt,
                               identity_pairs_0_);
  grpc_core::RefCountedPtr<::grpc_tls_certificate_provider> provider =
      grpc_core::MakeRefCounted<TlsTestCertificateProvider>(distributor);
  grpc_core::RefCountedPtr<grpc_tls_credentials_options> options =
      grpc_core::MakeRefCounted<grpc_tls_credentials_options>();
  options->set_certificate_provider(provider);
  options->set_watch_root_cert(true);
  options->set_watch_identity_pair(true);
  options->set_root_cert_name(kRootCertName);
  options->set_identity_cert_name(kIdentityCertName);
  grpc_core::RefCountedPtr<TlsCredentials> credential =
      grpc_core::MakeRefCounted<TlsCredentials>(options);
  grpc_channel_args* new_args = nullptr;
  grpc_core::RefCountedPtr<grpc_channel_security_connector> connector =
      credential->create_security_connector(nullptr, kTargetName, nullptr,
                                            &new_args);
  EXPECT_NE(connector, nullptr);
  grpc_core::TlsChannelSecurityConnector* tls_connector =
      static_cast<grpc_core::TlsChannelSecurityConnector*>(connector.get());
  EXPECT_NE(tls_connector->ClientHandshakerFactoryForTesting(), nullptr);
  EXPECT_EQ(tls_connector->RootCertsForTesting(), root_cert_0_);
  EXPECT_EQ(tls_connector->KeyCertPairListForTesting(), identity_pairs_0_);
  distributor->SetKeyMaterials(kRootCertName, root_cert_1_, absl::nullopt);
  distributor->SetKeyMaterials(kIdentityCertName, absl::nullopt,
                               identity_pairs_1_);
  EXPECT_NE(tls_connector->ClientHandshakerFactoryForTesting(), nullptr);
  EXPECT_EQ(tls_connector->RootCertsForTesting(), root_cert_1_);
  EXPECT_EQ(tls_connector->KeyCertPairListForTesting(), identity_pairs_1_);
  grpc_channel_args_destroy(new_args);
}

TEST_F(TlsSecurityConnectorTest,
       SystemRootsWhenCreateChannelSecurityConnector) {
  // Create options watching for no certificates.
  grpc_core::RefCountedPtr<grpc_tls_credentials_options> root_options =
      grpc_core::MakeRefCounted<grpc_tls_credentials_options>();
  grpc_core::RefCountedPtr<TlsCredentials> root_credential =
      grpc_core::MakeRefCounted<TlsCredentials>(root_options);
  grpc_channel_args* root_new_args = nullptr;
  grpc_core::RefCountedPtr<grpc_channel_security_connector> root_connector =
      root_credential->create_security_connector(nullptr, "some_target",
                                                 nullptr, &root_new_args);
  EXPECT_NE(root_connector, nullptr);
  grpc_core::TlsChannelSecurityConnector* tls_root_connector =
      static_cast<grpc_core::TlsChannelSecurityConnector*>(
          root_connector.get());
  EXPECT_NE(tls_root_connector->ClientHandshakerFactoryForTesting(), nullptr);
  grpc_channel_args_destroy(root_new_args);
}

TEST_F(TlsSecurityConnectorTest,
       SystemRootsAndIdentityCertsObtainedWhenCreateChannelSecurityConnector) {
  grpc_core::RefCountedPtr<grpc_tls_certificate_distributor> distributor =
      grpc_core::MakeRefCounted<grpc_tls_certificate_distributor>();
  distributor->SetKeyMaterials(kIdentityCertName, absl::nullopt,
                               identity_pairs_0_);
  grpc_core::RefCountedPtr<::grpc_tls_certificate_provider> provider =
      grpc_core::MakeRefCounted<TlsTestCertificateProvider>(distributor);
  // Create options only watching for identity certificates.
  grpc_core::RefCountedPtr<grpc_tls_credentials_options> root_options =
      grpc_core::MakeRefCounted<grpc_tls_credentials_options>();
  root_options->set_certificate_provider(provider);
  root_options->set_watch_identity_pair(true);
  root_options->set_identity_cert_name(kIdentityCertName);
  grpc_core::RefCountedPtr<TlsCredentials> root_credential =
      grpc_core::MakeRefCounted<TlsCredentials>(root_options);
  grpc_channel_args* root_new_args = nullptr;
  grpc_core::RefCountedPtr<grpc_channel_security_connector> root_connector =
      root_credential->create_security_connector(nullptr, "some_target",
                                                 nullptr, &root_new_args);
  EXPECT_NE(root_connector, nullptr);
  grpc_core::TlsChannelSecurityConnector* tls_root_connector =
      static_cast<grpc_core::TlsChannelSecurityConnector*>(
          root_connector.get());
  EXPECT_NE(tls_root_connector->ClientHandshakerFactoryForTesting(), nullptr);
  EXPECT_EQ(tls_root_connector->KeyCertPairListForTesting(), identity_pairs_0_);
  // If we have a root update, we shouldn't receive them in security connector,
  // since we claimed to use default system roots.
  distributor->SetKeyMaterials(kRootCertName, root_cert_1_, absl::nullopt);
  EXPECT_NE(tls_root_connector->ClientHandshakerFactoryForTesting(), nullptr);
  EXPECT_NE(tls_root_connector->RootCertsForTesting(), root_cert_1_);
  grpc_channel_args_destroy(root_new_args);
}

TEST_F(TlsSecurityConnectorTest,
       RootCertsObtainedWhenCreateChannelSecurityConnector) {
  grpc_core::RefCountedPtr<grpc_tls_certificate_distributor> distributor =
      grpc_core::MakeRefCounted<grpc_tls_certificate_distributor>();
  distributor->SetKeyMaterials(kRootCertName, root_cert_0_, absl::nullopt);
  distributor->SetKeyMaterials(kIdentityCertName, absl::nullopt,
                               identity_pairs_0_);
  grpc_core::RefCountedPtr<::grpc_tls_certificate_provider> provider =
      grpc_core::MakeRefCounted<TlsTestCertificateProvider>(distributor);
  // Create options only watching for root certificates.
  grpc_core::RefCountedPtr<grpc_tls_credentials_options> root_options =
      grpc_core::MakeRefCounted<grpc_tls_credentials_options>();
  root_options->set_certificate_provider(provider);
  root_options->set_watch_root_cert(true);
  root_options->set_root_cert_name(kRootCertName);
  grpc_core::RefCountedPtr<TlsCredentials> root_credential =
      grpc_core::MakeRefCounted<TlsCredentials>(root_options);
  grpc_channel_args* root_new_args = nullptr;
  grpc_core::RefCountedPtr<grpc_channel_security_connector> root_connector =
      root_credential->create_security_connector(nullptr, "some_target",
                                                 nullptr, &root_new_args);
  EXPECT_NE(root_connector, nullptr);
  grpc_core::TlsChannelSecurityConnector* tls_root_connector =
      static_cast<grpc_core::TlsChannelSecurityConnector*>(
          root_connector.get());
  EXPECT_NE(tls_root_connector->ClientHandshakerFactoryForTesting(), nullptr);
  EXPECT_EQ(tls_root_connector->RootCertsForTesting(), root_cert_0_);
  distributor->SetKeyMaterials(kRootCertName, root_cert_1_, absl::nullopt);
  EXPECT_NE(tls_root_connector->ClientHandshakerFactoryForTesting(), nullptr);
  EXPECT_EQ(tls_root_connector->RootCertsForTesting(), root_cert_1_);
  grpc_channel_args_destroy(root_new_args);
}

TEST_F(TlsSecurityConnectorTest,
       CertPartiallyObtainedWhenCreateChannelSecurityConnector) {
  grpc_core::RefCountedPtr<grpc_tls_certificate_distributor> distributor =
      grpc_core::MakeRefCounted<grpc_tls_certificate_distributor>();
  distributor->SetKeyMaterials(kRootCertName, root_cert_0_, absl::nullopt);
  grpc_core::RefCountedPtr<::grpc_tls_certificate_provider> provider =
      grpc_core::MakeRefCounted<TlsTestCertificateProvider>(distributor);
  // Registered the options watching both certs, but only root certs are
  // available at distributor right now.
  grpc_core::RefCountedPtr<grpc_tls_credentials_options> options =
      grpc_core::MakeRefCounted<grpc_tls_credentials_options>();
  options->set_certificate_provider(provider);
  options->set_watch_root_cert(true);
  options->set_watch_identity_pair(true);
  options->set_root_cert_name(kRootCertName);
  options->set_identity_cert_name(kIdentityCertName);
  grpc_core::RefCountedPtr<TlsCredentials> credential =
      grpc_core::MakeRefCounted<TlsCredentials>(options);
  grpc_channel_args* new_args = nullptr;
  grpc_core::RefCountedPtr<grpc_channel_security_connector> connector =
      credential->create_security_connector(nullptr, kTargetName, nullptr,
                                            &new_args);
  EXPECT_NE(connector, nullptr);
  grpc_core::TlsChannelSecurityConnector* tls_connector =
      static_cast<grpc_core::TlsChannelSecurityConnector*>(connector.get());
  // The client_handshaker_factory_ shouldn't be updated.
  EXPECT_EQ(tls_connector->ClientHandshakerFactoryForTesting(), nullptr);
  EXPECT_EQ(tls_connector->RootCertsForTesting(), root_cert_0_);
  // After updating the root certs, the client_handshaker_factory_ should be
  // updated.
  distributor->SetKeyMaterials(kIdentityCertName, absl::nullopt,
                               identity_pairs_0_);
  EXPECT_NE(tls_connector->ClientHandshakerFactoryForTesting(), nullptr);
  EXPECT_EQ(tls_connector->RootCertsForTesting(), root_cert_0_);
  EXPECT_EQ(tls_connector->KeyCertPairListForTesting(), identity_pairs_0_);
  grpc_channel_args_destroy(new_args);
}

TEST_F(TlsSecurityConnectorTest,
       DistributorHasErrorForChannelSecurityConnector) {
  grpc_core::RefCountedPtr<grpc_tls_certificate_distributor> distributor =
      grpc_core::MakeRefCounted<grpc_tls_certificate_distributor>();
  distributor->SetKeyMaterials(kRootCertName, root_cert_0_, absl::nullopt);
  distributor->SetKeyMaterials(kIdentityCertName, absl::nullopt,
                               identity_pairs_0_);
  grpc_core::RefCountedPtr<::grpc_tls_certificate_provider> provider =
      grpc_core::MakeRefCounted<TlsTestCertificateProvider>(distributor);
  grpc_core::RefCountedPtr<grpc_tls_credentials_options> options =
      grpc_core::MakeRefCounted<grpc_tls_credentials_options>();
  options->set_certificate_provider(provider);
  options->set_watch_root_cert(true);
  options->set_watch_identity_pair(true);
  options->set_root_cert_name(kRootCertName);
  options->set_identity_cert_name(kIdentityCertName);
  grpc_core::RefCountedPtr<TlsCredentials> credential =
      grpc_core::MakeRefCounted<TlsCredentials>(options);
  grpc_channel_args* new_args = nullptr;
  grpc_core::RefCountedPtr<grpc_channel_security_connector> connector =
      credential->create_security_connector(nullptr, kTargetName, nullptr,
                                            &new_args);
  EXPECT_NE(connector, nullptr);
  grpc_core::TlsChannelSecurityConnector* tls_connector =
      static_cast<grpc_core::TlsChannelSecurityConnector*>(connector.get());
  EXPECT_NE(tls_connector->ClientHandshakerFactoryForTesting(), nullptr);
  EXPECT_EQ(tls_connector->RootCertsForTesting(), root_cert_0_);
  EXPECT_EQ(tls_connector->KeyCertPairListForTesting(), identity_pairs_0_);
  // Calling SetErrorForCert on distributor shouldn't invalidate the previous
  // valid credentials.
  distributor->SetErrorForCert(
      kRootCertName, GRPC_ERROR_CREATE_FROM_STATIC_STRING(kErrorMessage),
      absl::nullopt);
  distributor->SetErrorForCert(
      kIdentityCertName, absl::nullopt,
      GRPC_ERROR_CREATE_FROM_STATIC_STRING(kErrorMessage));
  EXPECT_NE(tls_connector->ClientHandshakerFactoryForTesting(), nullptr);
  EXPECT_EQ(tls_connector->RootCertsForTesting(), root_cert_0_);
  EXPECT_EQ(tls_connector->KeyCertPairListForTesting(), identity_pairs_0_);
  grpc_channel_args_destroy(new_args);
}

TEST_F(TlsSecurityConnectorTest,
       CreateChannelSecurityConnectorFailNoTargetName) {
  grpc_core::RefCountedPtr<grpc_tls_credentials_options> options =
      grpc_core::MakeRefCounted<grpc_tls_credentials_options>();
  grpc_core::RefCountedPtr<TlsCredentials> credential =
      grpc_core::MakeRefCounted<TlsCredentials>(options);
  grpc_channel_args* new_args = nullptr;
  grpc_core::RefCountedPtr<grpc_channel_security_connector> connector =
      credential->create_security_connector(nullptr, nullptr, nullptr,
                                            &new_args);
  EXPECT_EQ(connector, nullptr);
}

TEST_F(TlsSecurityConnectorTest,
       CreateChannelSecurityConnectorFailNoCredentials) {
  auto connector =
      grpc_core::TlsChannelSecurityConnector::CreateTlsChannelSecurityConnector(
          nullptr, grpc_core::MakeRefCounted<grpc_tls_credentials_options>(),
          nullptr, kTargetName, nullptr, nullptr);
  EXPECT_EQ(connector, nullptr);
}

TEST_F(TlsSecurityConnectorTest, CreateChannelSecurityConnectorFailNoOptions) {
  grpc_core::RefCountedPtr<grpc_tls_credentials_options> options =
      grpc_core::MakeRefCounted<grpc_tls_credentials_options>();
  grpc_core::RefCountedPtr<TlsCredentials> credential =
      grpc_core::MakeRefCounted<TlsCredentials>(options);
  auto connector =
      grpc_core::TlsChannelSecurityConnector::CreateTlsChannelSecurityConnector(
          credential, nullptr, nullptr, kTargetName, nullptr, nullptr);
  EXPECT_EQ(connector, nullptr);
}

//
// Tests for Certificate Verifier in ChannelSecurityConnector.
//

TEST_F(TlsSecurityConnectorTest,
       ChannelSCWithSyncGoodExternalVerifierAndGoodRequestParam) {
  CreateExternalCertificateVerifier(SyncExternalVerifierGoodVerify, nullptr,
                                    nullptr);
  grpc_core::RefCountedPtr<grpc_tls_credentials_options> options =
      grpc_core::MakeRefCounted<grpc_tls_credentials_options>();
  options->set_verify_server_cert(true);
  options->set_certificate_verifier(external_certificate_verifier_->Ref());
  grpc_core::RefCountedPtr<TlsCredentials> credential =
      grpc_core::MakeRefCounted<TlsCredentials>(options);
  grpc_channel_args* new_args = nullptr;
  grpc_core::RefCountedPtr<grpc_channel_security_connector> connector =
      credential->create_security_connector(nullptr, kTargetName, nullptr,
                                            &new_args);
  EXPECT_NE(connector, nullptr);
  grpc_core::TlsChannelSecurityConnector* tls_connector =
      static_cast<grpc_core::TlsChannelSecurityConnector*>(connector.get());
  EXPECT_NE(tls_connector->ClientHandshakerFactoryForTesting(), nullptr);
  // Construct a basic TSI Peer.
  tsi_peer peer;
  GPR_ASSERT(tsi_construct_peer(2, &peer) == TSI_OK);
  GPR_ASSERT(tsi_construct_string_peer_property(TSI_SSL_ALPN_SELECTED_PROTOCOL,
                                                "grpc", strlen("grpc"),
                                                &peer.properties[0]) == TSI_OK);
  GPR_ASSERT(tsi_construct_string_peer_property_from_cstring(
                 TSI_X509_SUBJECT_COMMON_NAME_PEER_PROPERTY, "foo.bar.com",
                 &peer.properties[1]) == TSI_OK);
  grpc_core::RefCountedPtr<grpc_auth_context> auth_context;
  grpc_error* expected_error = GRPC_ERROR_NONE;
  grpc_core::ExecCtx exec_ctx;
  grpc_closure* on_peer_checked = GRPC_CLOSURE_CREATE(
      grpcClosureCb, (void*)expected_error, grpc_schedule_on_exec_ctx);
  tls_connector->check_peer(peer, nullptr, &auth_context, on_peer_checked);
  grpc_channel_args_destroy(new_args);
}

TEST_F(TlsSecurityConnectorTest,
       ChannelSCWithAsyncBadExternalVerifierAndGoodRequestParam) {
  CreateExternalCertificateVerifier(AsyncExternalVerifierBadVerify, nullptr,
                                    nullptr);
  grpc_core::RefCountedPtr<grpc_tls_credentials_options> options =
      grpc_core::MakeRefCounted<grpc_tls_credentials_options>();
  options->set_verify_server_cert(true);
  options->set_certificate_verifier(external_certificate_verifier_->Ref());
  grpc_core::RefCountedPtr<TlsCredentials> credential =
      grpc_core::MakeRefCounted<TlsCredentials>(options);
  grpc_channel_args* new_args = nullptr;
  grpc_core::RefCountedPtr<grpc_channel_security_connector> connector =
      credential->create_security_connector(nullptr, kTargetName, nullptr,
                                            &new_args);
  EXPECT_NE(connector, nullptr);
  grpc_core::TlsChannelSecurityConnector* tls_connector =
      static_cast<grpc_core::TlsChannelSecurityConnector*>(connector.get());
  EXPECT_NE(tls_connector->ClientHandshakerFactoryForTesting(), nullptr);
  // Construct a basic TSI Peer.
  tsi_peer peer;
  GPR_ASSERT(tsi_construct_peer(2, &peer) == TSI_OK);
  GPR_ASSERT(tsi_construct_string_peer_property(TSI_SSL_ALPN_SELECTED_PROTOCOL,
                                                "grpc", strlen("grpc"),
                                                &peer.properties[0]) == TSI_OK);
  GPR_ASSERT(tsi_construct_string_peer_property_from_cstring(
                 TSI_X509_SUBJECT_COMMON_NAME_PEER_PROPERTY, "foo.bar.com",
                 &peer.properties[1]) == TSI_OK);
  grpc_core::RefCountedPtr<grpc_auth_context> auth_context;
  grpc_error* expected_error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(
      "Custom verification check failed with error: "
      "AsyncExternalVerifierBadVerify failed");
  grpc_core::ExecCtx exec_ctx;
  grpc_closure* on_peer_checked = GRPC_CLOSURE_CREATE(
      grpcClosureCb, (void*)expected_error, grpc_schedule_on_exec_ctx);
  tls_connector->check_peer(peer, nullptr, &auth_context, on_peer_checked);
  grpc_channel_args_destroy(new_args);
  /*GRPC_ERROR_UNREF(expected_error);*/
}

TEST_F(TlsSecurityConnectorTest,
       ChannelSCWithHostnameVerifierAndGoodRequestParamOnGoodMatch) {
  grpc_core::RefCountedPtr<grpc_tls_credentials_options> options =
      grpc_core::MakeRefCounted<grpc_tls_credentials_options>();
  options->set_verify_server_cert(true);
  options->set_certificate_verifier(hostname_certificate_verifier_.Ref());
  grpc_core::RefCountedPtr<TlsCredentials> credential =
      grpc_core::MakeRefCounted<TlsCredentials>(options);
  grpc_channel_args* new_args = nullptr;
  grpc_core::RefCountedPtr<grpc_channel_security_connector> connector =
      credential->create_security_connector(nullptr, kTargetName, nullptr,
                                            &new_args);
  EXPECT_NE(connector, nullptr);
  grpc_core::TlsChannelSecurityConnector* tls_connector =
      static_cast<grpc_core::TlsChannelSecurityConnector*>(connector.get());
  EXPECT_NE(tls_connector->ClientHandshakerFactoryForTesting(), nullptr);
  // Construct a full TSI Peer.
  tsi_peer peer;
  GPR_ASSERT(tsi_construct_peer(7, &peer) == TSI_OK);
  GPR_ASSERT(tsi_construct_string_peer_property(TSI_SSL_ALPN_SELECTED_PROTOCOL,
                                                "grpc", strlen("grpc"),
                                                &peer.properties[0]) == TSI_OK);
  GPR_ASSERT(tsi_construct_string_peer_property_from_cstring(
                 TSI_X509_SUBJECT_COMMON_NAME_PEER_PROPERTY, "foo.bar.com",
                 &peer.properties[1]) == TSI_OK);
  GPR_ASSERT(tsi_construct_string_peer_property_from_cstring(
                 TSI_X509_PEM_CERT_PROPERTY, "pem_cert", &peer.properties[2]) ==
             TSI_OK);
  GPR_ASSERT(tsi_construct_string_peer_property_from_cstring(
                 TSI_SECURITY_LEVEL_PEER_PROPERTY,
                 tsi_security_level_to_string(TSI_PRIVACY_AND_INTEGRITY),
                 &peer.properties[3]) == TSI_OK);
  GPR_ASSERT(tsi_construct_string_peer_property_from_cstring(
                 TSI_X509_PEM_CERT_CHAIN_PROPERTY, "pem_cert_chain",
                 &peer.properties[4]) == TSI_OK);
  GPR_ASSERT(tsi_construct_string_peer_property_from_cstring(
                 TSI_X509_URI_PEER_PROPERTY, "foo.bar.com",
                 &peer.properties[5]) == TSI_OK);
  GPR_ASSERT(tsi_construct_string_peer_property_from_cstring(
                 TSI_X509_URI_PEER_PROPERTY, "foo.baz.com",
                 &peer.properties[6]) == TSI_OK);
  grpc_core::RefCountedPtr<grpc_auth_context> auth_context;
  grpc_error* expected_error = GRPC_ERROR_NONE;
  grpc_core::ExecCtx exec_ctx;
  grpc_closure* on_peer_checked = GRPC_CLOSURE_CREATE(
      grpcClosureCb, (void*)expected_error, grpc_schedule_on_exec_ctx);
  tls_connector->check_peer(peer, nullptr, &auth_context, on_peer_checked);
  grpc_channel_args_destroy(new_args);
}

TEST_F(TlsSecurityConnectorTest,
       ChannelSCWithHostnameVerifierAndGoodRequestParamOnBadMatch) {
  grpc_core::RefCountedPtr<grpc_tls_credentials_options> options =
      grpc_core::MakeRefCounted<grpc_tls_credentials_options>();
  options->set_verify_server_cert(true);
  options->set_certificate_verifier(hostname_certificate_verifier_.Ref());
  grpc_core::RefCountedPtr<TlsCredentials> credential =
      grpc_core::MakeRefCounted<TlsCredentials>(options);
  grpc_channel_args* new_args = nullptr;
  grpc_core::RefCountedPtr<grpc_channel_security_connector> connector =
      credential->create_security_connector(nullptr, kTargetName, nullptr,
                                            &new_args);
  EXPECT_NE(connector, nullptr);
  grpc_core::TlsChannelSecurityConnector* tls_connector =
      static_cast<grpc_core::TlsChannelSecurityConnector*>(connector.get());
  EXPECT_NE(tls_connector->ClientHandshakerFactoryForTesting(), nullptr);
  // Construct a full TSI Peer.
  tsi_peer peer;
  GPR_ASSERT(tsi_construct_peer(7, &peer) == TSI_OK);
  GPR_ASSERT(tsi_construct_string_peer_property(TSI_SSL_ALPN_SELECTED_PROTOCOL,
                                                "grpc", strlen("grpc"),
                                                &peer.properties[0]) == TSI_OK);
  GPR_ASSERT(tsi_construct_string_peer_property_from_cstring(
                 TSI_X509_SUBJECT_COMMON_NAME_PEER_PROPERTY, "foo.com",
                 &peer.properties[1]) == TSI_OK);
  GPR_ASSERT(tsi_construct_string_peer_property_from_cstring(
                 TSI_X509_PEM_CERT_PROPERTY, "pem_cert", &peer.properties[2]) ==
             TSI_OK);
  GPR_ASSERT(tsi_construct_string_peer_property_from_cstring(
                 TSI_SECURITY_LEVEL_PEER_PROPERTY,
                 tsi_security_level_to_string(TSI_PRIVACY_AND_INTEGRITY),
                 &peer.properties[3]) == TSI_OK);
  GPR_ASSERT(tsi_construct_string_peer_property_from_cstring(
                 TSI_X509_PEM_CERT_CHAIN_PROPERTY, "pem_cert_chain",
                 &peer.properties[4]) == TSI_OK);
  GPR_ASSERT(tsi_construct_string_peer_property_from_cstring(
                 TSI_X509_URI_PEER_PROPERTY, "*.com", &peer.properties[5]) ==
             TSI_OK);
  GPR_ASSERT(tsi_construct_string_peer_property_from_cstring(
                 TSI_X509_URI_PEER_PROPERTY, "foo.baz.com",
                 &peer.properties[6]) == TSI_OK);
  grpc_core::RefCountedPtr<grpc_auth_context> auth_context;
  grpc_error* expected_error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(
      "Custom verification check failed with error: Hostname Verification "
      "Check failed.");
  grpc_core::ExecCtx exec_ctx;
  grpc_closure* on_peer_checked = GRPC_CLOSURE_CREATE(
      grpcClosureCb, (void*)expected_error, grpc_schedule_on_exec_ctx);
  tls_connector->check_peer(peer, nullptr, &auth_context, on_peer_checked);
  gpr_log(GPR_ERROR, "5");
  grpc_channel_args_destroy(new_args);
/*
  GRPC_ERROR_UNREF(expected_error);
*/
  gpr_log(GPR_ERROR, "6");
}

//
// Tests for Certificate Providers in ServerSecurityConnector.
//

TEST_F(TlsSecurityConnectorTest,
       RootAndIdentityCertsObtainedWhenCreateServerSecurityConnector) {
  grpc_core::RefCountedPtr<grpc_tls_certificate_distributor> distributor =
      grpc_core::MakeRefCounted<grpc_tls_certificate_distributor>();
  distributor->SetKeyMaterials(kRootCertName, root_cert_0_, absl::nullopt);
  distributor->SetKeyMaterials(kIdentityCertName, absl::nullopt,
                               identity_pairs_0_);
  grpc_core::RefCountedPtr<::grpc_tls_certificate_provider> provider =
      grpc_core::MakeRefCounted<TlsTestCertificateProvider>(distributor);
  grpc_core::RefCountedPtr<grpc_tls_credentials_options> options =
      grpc_core::MakeRefCounted<grpc_tls_credentials_options>();
  options->set_certificate_provider(provider);
  options->set_watch_root_cert(true);
  options->set_watch_identity_pair(true);
  options->set_root_cert_name(kRootCertName);
  options->set_identity_cert_name(kIdentityCertName);
  grpc_core::RefCountedPtr<TlsServerCredentials> credential =
      grpc_core::MakeRefCounted<TlsServerCredentials>(options);
  grpc_core::RefCountedPtr<grpc_server_security_connector> connector =
      credential->create_security_connector(nullptr);
  EXPECT_NE(connector, nullptr);
  grpc_core::TlsServerSecurityConnector* tls_connector =
      static_cast<grpc_core::TlsServerSecurityConnector*>(connector.get());
  EXPECT_NE(tls_connector->ServerHandshakerFactoryForTesting(), nullptr);
  EXPECT_EQ(tls_connector->RootCertsForTesting(), root_cert_0_);
  EXPECT_EQ(tls_connector->KeyCertPairListForTesting(), identity_pairs_0_);
  distributor->SetKeyMaterials(kRootCertName, root_cert_1_, absl::nullopt);
  distributor->SetKeyMaterials(kIdentityCertName, absl::nullopt,
                               identity_pairs_1_);
  EXPECT_NE(tls_connector->ServerHandshakerFactoryForTesting(), nullptr);
  EXPECT_EQ(tls_connector->RootCertsForTesting(), root_cert_1_);
  EXPECT_EQ(tls_connector->KeyCertPairListForTesting(), identity_pairs_1_);
}

// Note that on server side, we don't have tests watching root certs only,
// because in TLS, the identity certs should always be presented. If we don't
// provide, it will try to load certs from some default system locations, and
// will hence fail on some systems.
TEST_F(TlsSecurityConnectorTest,
       IdentityCertsObtainedWhenCreateServerSecurityConnector) {
  grpc_core::RefCountedPtr<grpc_tls_certificate_distributor> distributor =
      grpc_core::MakeRefCounted<grpc_tls_certificate_distributor>();
  distributor->SetKeyMaterials(kRootCertName, root_cert_0_, absl::nullopt);
  distributor->SetKeyMaterials(kIdentityCertName, absl::nullopt,
                               identity_pairs_0_);
  grpc_core::RefCountedPtr<::grpc_tls_certificate_provider> provider =
      grpc_core::MakeRefCounted<TlsTestCertificateProvider>(distributor);
  // Create options only watching for identity certificates.
  grpc_core::RefCountedPtr<grpc_tls_credentials_options> identity_options =
      grpc_core::MakeRefCounted<grpc_tls_credentials_options>();
  identity_options->set_certificate_provider(provider);
  identity_options->set_watch_identity_pair(true);
  identity_options->set_identity_cert_name(kIdentityCertName);
  grpc_core::RefCountedPtr<TlsServerCredentials> identity_credential =
      grpc_core::MakeRefCounted<TlsServerCredentials>(identity_options);
  grpc_core::RefCountedPtr<grpc_server_security_connector> identity_connector =
      identity_credential->create_security_connector(nullptr);
  EXPECT_NE(identity_connector, nullptr);
  grpc_core::TlsServerSecurityConnector* tls_identity_connector =
      static_cast<grpc_core::TlsServerSecurityConnector*>(
          identity_connector.get());
  EXPECT_NE(tls_identity_connector->ServerHandshakerFactoryForTesting(),
            nullptr);
  EXPECT_EQ(tls_identity_connector->KeyCertPairListForTesting(),
            identity_pairs_0_);
  distributor->SetKeyMaterials(kIdentityCertName, absl::nullopt,
                               identity_pairs_1_);
  EXPECT_NE(tls_identity_connector->ServerHandshakerFactoryForTesting(),
            nullptr);
  EXPECT_EQ(tls_identity_connector->KeyCertPairListForTesting(),
            identity_pairs_1_);
}

TEST_F(TlsSecurityConnectorTest,
       CertPartiallyObtainedWhenCreateServerSecurityConnector) {
  grpc_core::RefCountedPtr<grpc_tls_certificate_distributor> distributor =
      grpc_core::MakeRefCounted<grpc_tls_certificate_distributor>();
  distributor->SetKeyMaterials(kIdentityCertName, absl::nullopt,
                               identity_pairs_0_);
  grpc_core::RefCountedPtr<::grpc_tls_certificate_provider> provider =
      grpc_core::MakeRefCounted<TlsTestCertificateProvider>(distributor);
  // Registered the options watching both certs, but only root certs are
  // available at distributor right now.
  grpc_core::RefCountedPtr<grpc_tls_credentials_options> options =
      grpc_core::MakeRefCounted<grpc_tls_credentials_options>();
  options->set_certificate_provider(provider);
  options->set_watch_root_cert(true);
  options->set_watch_identity_pair(true);
  options->set_root_cert_name(kRootCertName);
  options->set_identity_cert_name(kIdentityCertName);
  grpc_core::RefCountedPtr<TlsServerCredentials> credential =
      grpc_core::MakeRefCounted<TlsServerCredentials>(options);
  grpc_core::RefCountedPtr<grpc_server_security_connector> connector =
      credential->create_security_connector(nullptr);
  EXPECT_NE(connector, nullptr);
  grpc_core::TlsServerSecurityConnector* tls_connector =
      static_cast<grpc_core::TlsServerSecurityConnector*>(connector.get());
  // The server_handshaker_factory_ shouldn't be updated.
  EXPECT_EQ(tls_connector->ServerHandshakerFactoryForTesting(), nullptr);
  EXPECT_EQ(tls_connector->KeyCertPairListForTesting(), identity_pairs_0_);
  // After updating the root certs, the server_handshaker_factory_ should be
  // updated.
  distributor->SetKeyMaterials(kRootCertName, root_cert_0_, absl::nullopt);
  EXPECT_NE(tls_connector->ServerHandshakerFactoryForTesting(), nullptr);
  EXPECT_EQ(tls_connector->RootCertsForTesting(), root_cert_0_);
  EXPECT_EQ(tls_connector->KeyCertPairListForTesting(), identity_pairs_0_);
}

TEST_F(TlsSecurityConnectorTest,
       DistributorHasErrorForServerSecurityConnector) {
  grpc_core::RefCountedPtr<grpc_tls_certificate_distributor> distributor =
      grpc_core::MakeRefCounted<grpc_tls_certificate_distributor>();
  distributor->SetKeyMaterials(kRootCertName, root_cert_0_, absl::nullopt);
  distributor->SetKeyMaterials(kIdentityCertName, absl::nullopt,
                               identity_pairs_0_);
  grpc_core::RefCountedPtr<::grpc_tls_certificate_provider> provider =
      grpc_core::MakeRefCounted<TlsTestCertificateProvider>(distributor);
  grpc_core::RefCountedPtr<grpc_tls_credentials_options> options =
      grpc_core::MakeRefCounted<grpc_tls_credentials_options>();
  options->set_certificate_provider(provider);
  options->set_watch_root_cert(true);
  options->set_watch_identity_pair(true);
  options->set_root_cert_name(kRootCertName);
  options->set_identity_cert_name(kIdentityCertName);
  grpc_core::RefCountedPtr<TlsServerCredentials> credential =
      grpc_core::MakeRefCounted<TlsServerCredentials>(options);
  grpc_core::RefCountedPtr<grpc_server_security_connector> connector =
      credential->create_security_connector(nullptr);
  EXPECT_NE(connector, nullptr);
  grpc_core::TlsServerSecurityConnector* tls_connector =
      static_cast<grpc_core::TlsServerSecurityConnector*>(connector.get());
  EXPECT_NE(tls_connector->ServerHandshakerFactoryForTesting(), nullptr);
  EXPECT_EQ(tls_connector->RootCertsForTesting(), root_cert_0_);
  EXPECT_EQ(tls_connector->KeyCertPairListForTesting(), identity_pairs_0_);
  // Calling SetErrorForCert on distributor shouldn't invalidate the previous
  // valid credentials.
  distributor->SetErrorForCert(
      kRootCertName, GRPC_ERROR_CREATE_FROM_STATIC_STRING(kErrorMessage),
      absl::nullopt);
  distributor->SetErrorForCert(
      kIdentityCertName, absl::nullopt,
      GRPC_ERROR_CREATE_FROM_STATIC_STRING(kErrorMessage));
  EXPECT_NE(tls_connector->ServerHandshakerFactoryForTesting(), nullptr);
  EXPECT_EQ(tls_connector->RootCertsForTesting(), root_cert_0_);
  EXPECT_EQ(tls_connector->KeyCertPairListForTesting(), identity_pairs_0_);
}

TEST_F(TlsSecurityConnectorTest,
       CreateServerSecurityConnectorFailNoCredentials) {
  auto connector =
      grpc_core::TlsServerSecurityConnector::CreateTlsServerSecurityConnector(
          nullptr, grpc_core::MakeRefCounted<grpc_tls_credentials_options>());
  EXPECT_EQ(connector, nullptr);
}

TEST_F(TlsSecurityConnectorTest, CreateServerSecurityConnectorFailNoOptions) {
  grpc_core::RefCountedPtr<grpc_tls_credentials_options> options =
      grpc_core::MakeRefCounted<grpc_tls_credentials_options>();
  grpc_core::RefCountedPtr<TlsServerCredentials> credential =
      grpc_core::MakeRefCounted<TlsServerCredentials>(options);
  auto connector =
      grpc_core::TlsServerSecurityConnector::CreateTlsServerSecurityConnector(
          credential, nullptr);
  EXPECT_EQ(connector, nullptr);
}

//
// Tests for Certificate Verifier in ServerSecurityConnector.

TEST_F(TlsSecurityConnectorTest,
       ServerSCWithSyncGoodExternalVerifierAndGoodRequestParam) {
  CreateExternalCertificateVerifier(SyncExternalVerifierGoodVerify, nullptr,
                                    nullptr);
  grpc_core::RefCountedPtr<grpc_tls_credentials_options> options =
      grpc_core::MakeRefCounted<grpc_tls_credentials_options>();
  options->set_cert_request_type(GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE);
  options->set_certificate_verifier(external_certificate_verifier_->Ref());
  auto provider =
      grpc_core::MakeRefCounted<grpc_core::StaticDataCertificateProvider>(
          "", grpc_core::PemKeyCertPairList());
  options->set_certificate_provider(std::move(provider));
  options->set_watch_identity_pair(true);
  auto credentials = grpc_core::MakeRefCounted<TlsServerCredentials>(options);
  auto connector = credentials->create_security_connector(nullptr);
  // Construct a basic TSI Peer.
  tsi_peer peer;
  GPR_ASSERT(tsi_construct_peer(2, &peer) == TSI_OK);
  GPR_ASSERT(tsi_construct_string_peer_property(TSI_SSL_ALPN_SELECTED_PROTOCOL,
                                                "grpc", strlen("grpc"),
                                                &peer.properties[0]) == TSI_OK);
  GPR_ASSERT(tsi_construct_string_peer_property_from_cstring(
                 TSI_X509_SUBJECT_COMMON_NAME_PEER_PROPERTY, "foo.bar.com",
                 &peer.properties[1]) == TSI_OK);
  grpc_core::RefCountedPtr<grpc_auth_context> auth_context;
  grpc_error* expected_error = GRPC_ERROR_NONE;
  grpc_core::ExecCtx exec_ctx;
  grpc_closure* on_peer_checked = GRPC_CLOSURE_CREATE(
      grpcClosureCb, (void*)expected_error, grpc_schedule_on_exec_ctx);
  connector->check_peer(peer, nullptr, &auth_context, on_peer_checked);
}

TEST_F(TlsSecurityConnectorTest,
       ServerSCWithAsyncBadExternalVerifierAndGoodRequestParam) {
  CreateExternalCertificateVerifier(AsyncExternalVerifierBadVerify, nullptr,
                                    nullptr);
  grpc_core::RefCountedPtr<grpc_tls_credentials_options> options =
      grpc_core::MakeRefCounted<grpc_tls_credentials_options>();
  options->set_cert_request_type(GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE);
  options->set_certificate_verifier(external_certificate_verifier_->Ref());
  auto provider =
      grpc_core::MakeRefCounted<grpc_core::StaticDataCertificateProvider>(
          "", grpc_core::PemKeyCertPairList());
  options->set_certificate_provider(std::move(provider));
  options->set_watch_identity_pair(true);
  auto credentials = grpc_core::MakeRefCounted<TlsServerCredentials>(options);
  auto connector = credentials->create_security_connector(nullptr);
  // Construct a basic TSI Peer.
  tsi_peer peer;
  GPR_ASSERT(tsi_construct_peer(2, &peer) == TSI_OK);
  GPR_ASSERT(tsi_construct_string_peer_property(TSI_SSL_ALPN_SELECTED_PROTOCOL,
                                                "grpc", strlen("grpc"),
                                                &peer.properties[0]) == TSI_OK);
  GPR_ASSERT(tsi_construct_string_peer_property_from_cstring(
                 TSI_X509_SUBJECT_COMMON_NAME_PEER_PROPERTY, "foo.bar.com",
                 &peer.properties[1]) == TSI_OK);
  grpc_core::RefCountedPtr<grpc_auth_context> auth_context;
  grpc_error* expected_error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(
      "Custom verification check failed with error: "
      "AsyncExternalVerifierBadVerify failed");
  grpc_core::ExecCtx exec_ctx;
  grpc_closure* on_peer_checked = GRPC_CLOSURE_CREATE(
      grpcClosureCb, (void*)expected_error, grpc_schedule_on_exec_ctx);
  connector->check_peer(peer, nullptr, &auth_context, on_peer_checked);
/*
  GRPC_ERROR_UNREF(expected_error);
*/
}

}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  GPR_GLOBAL_CONFIG_SET(grpc_default_ssl_roots_file_path, CA_CERT_PATH);
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}

//
// Copyright 2021 gRPC authors.
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

#include "src/core/lib/security/credentials/tls/grpc_tls_certificate_verifier.h"

#include <gmock/gmock.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <gtest/gtest.h>

#include <deque>
#include <list>

#include "src/core/lib/gpr/tmpfile.h"
#include "src/core/lib/iomgr/load_file.h"
#include "src/core/lib/security/security_connector/tls/tls_security_connector.h"
#include "src/core/lib/slice/slice_internal.h"
#include "test/core/util/test_config.h"
#include "test/core/util/tls_utils.h"

namespace grpc_core {

namespace testing {

class GrpcTlsCertificateVerifierTest : public ::testing::Test {
 protected:
  void SetUp() override {
    TlsChannelSecurityConnector::CertificateVerificationRequestInit(
        &client_request_);
    TlsServerSecurityConnector::CertificateVerificationRequestInit(
        &server_request_);
  }

  void TearDown() override {
    TlsChannelSecurityConnector::CertificateVerificationRequestDestroy(
        &client_request_);
    TlsServerSecurityConnector::CertificateVerificationRequestDestroy(
        &server_request_);
    if (external_certificate_verifier_ != nullptr) {
      delete external_certificate_verifier_;
    }
  }

  void CreateExternalCertificateVerifier(
      int (*verify)(void* user_data,
                    grpc_tls_custom_verification_check_request* request,
                    grpc_tls_on_custom_verification_check_done_cb callback,
                    void* callback_arg),
      void (*cancel)(void* user_data,
                     grpc_tls_custom_verification_check_request* request),
      void (*destruct)(void* user_data)) {
    grpc_tls_certificate_verifier_external* external_verifier =
        new grpc_tls_certificate_verifier_external();
    external_verifier->verify = verify;
    external_verifier->cancel = cancel;
    external_verifier->destruct = destruct;
    // Takes the ownership of external_verifier.
    external_certificate_verifier_ =
        new ExternalCertificateVerifier(external_verifier);
  }

  static int SyncExternalVerifierGoodVerify(
      void* user_data, grpc_tls_custom_verification_check_request* request,
      grpc_tls_on_custom_verification_check_done_cb callback,
      void* callback_arg) {
    request->status = GRPC_STATUS_OK;
    return false;
  }

  static int SyncExternalVerifierBadVerify(
      void* user_data, grpc_tls_custom_verification_check_request* request,
      grpc_tls_on_custom_verification_check_done_cb callback,
      void* callback_arg) {
    request->status = GRPC_STATUS_UNAUTHENTICATED;
    request->error_details = gpr_strdup("SyncExternalVerifierBadVerify failed");
    return false;
  }

  // For Async external verifier, we will take two extra parameters, compared to
  // the sync verifier:
  // 1. thread_ptr_ptr: main test thread will create a dummy thread object, and
  // later in the aysnc function, we will replace it with the real thread we
  // want to run checks. this is the pointer of the thread object pointer. After
  // the actual thread is run, we will need to join the thread in main test
  // thread.
  // 2. event_ptr: when the thread object is replaced, we will signal a event to
  // let the main test thread know the thread object is replaced with the actual
  // thread, and is hence join-able.
  void CreateASyncExternalCertificateVerifier(
      int (*verify)(void* user_data,
                    grpc_tls_custom_verification_check_request* request,
                    grpc_tls_on_custom_verification_check_done_cb callback,
                    void* callback_arg),
      void (*cancel)(void* user_data,
                     grpc_tls_custom_verification_check_request* request),
      void (*destruct)(void* user_data), gpr_event* event_ptr,
      grpc_core::Thread** thread_ptr_ptr) {
    grpc_tls_certificate_verifier_external* external_verifier =
        new grpc_tls_certificate_verifier_external();
    external_verifier->verify = verify;
    external_verifier->cancel = cancel;
    external_verifier->destruct = destruct;
    // We store the information we want to pass to external_verifier->verify in
    // the user_data_args.
    ExternalVerifierUserDataArgs* user_data_args =
        new ExternalVerifierUserDataArgs();
    user_data_args->thread_ptr_ptr = thread_ptr_ptr;
    user_data_args->event_ptr = event_ptr;
    external_verifier->user_data = (void*)user_data_args;
    // Takes the ownership of external_verifier.
    external_certificate_verifier_ =
        new ExternalCertificateVerifier(external_verifier);
  }

  // This is the arg we will pass in when creating the thread, and retrieve it
  // later in the thread callback.
  struct ThreadArgs {
    grpc_tls_custom_verification_check_request* request = nullptr;
    grpc_tls_on_custom_verification_check_done_cb callback;
    void* callback_arg = nullptr;
  };

  // This is the arg we will pass in when creating the external verifier, and
  // retrieve it later in the its verifier functions.
  struct ExternalVerifierUserDataArgs {
    grpc_core::Thread** thread_ptr_ptr = nullptr;
    gpr_event* event_ptr = nullptr;
  };

  static int AsyncExternalVerifierGoodVerify(
      void* user_data, grpc_tls_custom_verification_check_request* request,
      grpc_tls_on_custom_verification_check_done_cb callback,
      void* callback_arg) {
    ExternalVerifierUserDataArgs* user_data_args =
        static_cast<ExternalVerifierUserDataArgs*>(user_data);
    grpc_core::Thread** thread_ptr_ptr = user_data_args->thread_ptr_ptr;
    gpr_event* event_ptr = user_data_args->event_ptr;
    // Creates the thread args we use when creating the thread.
    ThreadArgs* thread_args = new ThreadArgs();
    thread_args->request = request;
    thread_args->callback = callback;
    thread_args->callback_arg = callback_arg;
    // Replaces the thread object with the actual thread object we want to run.
    // First we delete the dummy thread object we set before.
    delete *thread_ptr_ptr;
    *thread_ptr_ptr = new grpc_core::Thread("AsyncExternalVerifierGoodVerify",
                                            &AsyncExternalVerifierGoodVerifyCb,
                                            static_cast<void*>(thread_args));
    (**thread_ptr_ptr).Start();
    // Now we can notify the main thread that the thread object is set.
    gpr_event_set(event_ptr, reinterpret_cast<void*>(1));
    return true;
  }

  static void AsyncExternalVerifierGoodVerifyCb(void* args) {
    ThreadArgs* thread_args = static_cast<ThreadArgs*>(args);
    grpc_tls_custom_verification_check_request* request = thread_args->request;
    grpc_tls_on_custom_verification_check_done_cb callback =
        thread_args->callback;
    void* callback_arg = thread_args->callback_arg;
    request->status = GRPC_STATUS_OK;
    callback(request, callback_arg);
    delete thread_args;
  }

  static int AsyncExternalVerifierBadVerify(
      void* user_data, grpc_tls_custom_verification_check_request* request,
      grpc_tls_on_custom_verification_check_done_cb callback,
      void* callback_arg) {
    ExternalVerifierUserDataArgs* user_data_args =
        static_cast<ExternalVerifierUserDataArgs*>(user_data);
    grpc_core::Thread** thread_ptr_ptr = user_data_args->thread_ptr_ptr;
    gpr_event* event_ptr = user_data_args->event_ptr;
    // Creates the thread args we use when creating the thread.
    ThreadArgs* thread_args = new ThreadArgs();
    thread_args->request = request;
    thread_args->callback = callback;
    thread_args->callback_arg = callback_arg;
    // Replaces the thread object with the actual thread object we want to run.
    // First we delete the dummy thread object we set before.
    delete *thread_ptr_ptr;
    *thread_ptr_ptr = new grpc_core::Thread("AsyncExternalVerifierBadVerify",
                                            &AsyncExternalVerifierBadVerifyCb,
                                            static_cast<void*>(thread_args));
    (**thread_ptr_ptr).Start();
    // Now we can notify the main thread that the thread object is set.
    gpr_event_set(event_ptr, reinterpret_cast<void*>(1));
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
    callback(request, callback_arg);
    delete thread_args;
  }

  static void AsyncExternalVerifierDestruct(void* user_data) {
    ExternalVerifierUserDataArgs* user_data_args =
        static_cast<ExternalVerifierUserDataArgs*>(user_data);
    delete user_data_args;
  }

  grpc_tls_custom_verification_check_request client_request_;
  grpc_tls_custom_verification_check_request server_request_;
  ExternalCertificateVerifier* external_certificate_verifier_ = nullptr;
  HostNameCertificateVerifier hostname_certificate_verifier_;
};

TEST_F(GrpcTlsCertificateVerifierTest, SyncExternalVerifierGood) {
  CreateExternalCertificateVerifier(SyncExternalVerifierGoodVerify, nullptr,
                                    nullptr);
  external_certificate_verifier_->Verify(&server_request_, [] {});
  EXPECT_EQ(server_request_.status, GRPC_STATUS_OK);
}

TEST_F(GrpcTlsCertificateVerifierTest, SyncExternalVerifierBad) {
  CreateExternalCertificateVerifier(SyncExternalVerifierBadVerify, nullptr,
                                    nullptr);
  external_certificate_verifier_->Verify(&server_request_, [] {});
  EXPECT_EQ(server_request_.status, GRPC_STATUS_UNAUTHENTICATED);
  EXPECT_STREQ(server_request_.error_details,
               "SyncExternalVerifierBadVerify failed");
}

TEST_F(GrpcTlsCertificateVerifierTest, AsyncExternalVerifierGood) {
  // Indicates if the thread pointer returned from
  // CreateASyncExternalCertificateVerifier is filled with the real object
  // asynchronously.
  gpr_event event;
  gpr_event_init(&event);
  // Creates a dummy thread object that will be replaced with the actual thread
  // object later in the async verifier.
  grpc_core::Thread* thread_ptr = new grpc_core::Thread();
  gpr_log(GPR_ERROR, "thread pointer address: %p", thread_ptr);
  CreateASyncExternalCertificateVerifier(AsyncExternalVerifierGoodVerify,
                                         nullptr, AsyncExternalVerifierDestruct,
                                         &event, &thread_ptr);
  external_certificate_verifier_->Verify(
      &server_request_, [] { gpr_log(GPR_INFO, "Callback is invoked."); });
  // Wait for the thread object to be set.
  void* value = gpr_event_wait(
      &event, gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                           gpr_time_from_seconds(5, GPR_TIMESPAN)));
  EXPECT_NE(value, nullptr);
  // Join the thread otherwise there will be memory leaks.
  (*thread_ptr).Join();
  // release the thread object we run.
  delete thread_ptr;
  EXPECT_EQ(server_request_.status, GRPC_STATUS_OK);
}

TEST_F(GrpcTlsCertificateVerifierTest, AsyncExternalVerifierBad) {
  // Indicates if the thread pointer returned from
  // CreateASyncExternalCertificateVerifier is filled with the real object
  // asynchronously.
  gpr_event event;
  gpr_event_init(&event);
  // Creates a dummy thread object that will be replaced with the actual thread
  // object later in the async verifier.
  grpc_core::Thread* thread_ptr = new grpc_core::Thread();
  gpr_log(GPR_ERROR, "thread pointer address: %p", thread_ptr);
  CreateASyncExternalCertificateVerifier(AsyncExternalVerifierBadVerify,
                                         nullptr, AsyncExternalVerifierDestruct,
                                         &event, &thread_ptr);
  external_certificate_verifier_->Verify(
      &server_request_, [] { gpr_log(GPR_INFO, "Callback is invoked."); });
  // Wait for the thread object to be set.
  void* value = gpr_event_wait(
      &event, gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                           gpr_time_from_seconds(5, GPR_TIMESPAN)));
  EXPECT_NE(value, nullptr);
  // Join the thread otherwise there will be memory leaks.
  (*thread_ptr).Join();
  // release the thread object we run.
  delete thread_ptr;
  EXPECT_EQ(server_request_.status, GRPC_STATUS_UNAUTHENTICATED);
  EXPECT_STREQ(server_request_.error_details,
               "AsyncExternalVerifierBadVerify failed");
}

TEST_F(GrpcTlsCertificateVerifierTest, HostnameVerifierNullTargetName) {
  hostname_certificate_verifier_.Verify(&client_request_, [] {});
  EXPECT_EQ(client_request_.status, GRPC_STATUS_UNAUTHENTICATED);
  EXPECT_STREQ(client_request_.error_details, "Target name is not specified.");
}

TEST_F(GrpcTlsCertificateVerifierTest, HostnameVerifierInvalidTargetName) {
  client_request_.target_name = gpr_strdup("[foo.com@443");
  hostname_certificate_verifier_.Verify(&client_request_, [] {});
  EXPECT_EQ(client_request_.status, GRPC_STATUS_UNAUTHENTICATED);
  EXPECT_STREQ(client_request_.error_details,
               "Failed to split hostname and port.");
}

TEST_F(GrpcTlsCertificateVerifierTest, HostnameVerifierDNSExactCheckGood) {
  client_request_.target_name = gpr_strdup("foo.com:443");
  client_request_.peer_info.san_names.dns_names = new char*[1];
  client_request_.peer_info.san_names.dns_names[0] = gpr_strdup("foo.com");
  client_request_.peer_info.san_names.dns_names_size = 1;
  hostname_certificate_verifier_.Verify(&client_request_, [] {});
  EXPECT_EQ(client_request_.status, GRPC_STATUS_OK);
}

TEST_F(GrpcTlsCertificateVerifierTest, HostnameVerifierDNSWildcardCheckGood) {
  client_request_.target_name = gpr_strdup("foo.bar.com:443");
  client_request_.peer_info.san_names.dns_names = new char*[1];
  client_request_.peer_info.san_names.dns_names[0] = gpr_strdup("*.bar.com");
  client_request_.peer_info.san_names.dns_names_size = 1;
  hostname_certificate_verifier_.Verify(&client_request_, [] {});
  EXPECT_EQ(client_request_.status, GRPC_STATUS_OK);
}

TEST_F(GrpcTlsCertificateVerifierTest, HostnameVerifierDNSTopWildcardCheckBad) {
  client_request_.target_name = gpr_strdup("foo.com:443");
  client_request_.peer_info.san_names.dns_names = new char*[1];
  client_request_.peer_info.san_names.dns_names[0] = gpr_strdup("*.com");
  client_request_.peer_info.san_names.dns_names_size = 1;
  hostname_certificate_verifier_.Verify(&client_request_, [] {});
  EXPECT_EQ(client_request_.status, GRPC_STATUS_UNAUTHENTICATED);
  EXPECT_STREQ(client_request_.error_details,
               "Hostname Verification Check failed.");
}

TEST_F(GrpcTlsCertificateVerifierTest, HostnameVerifierDNSExactCheckBad) {
  client_request_.target_name = gpr_strdup("foo.com:443");
  client_request_.peer_info.san_names.dns_names = new char*[1];
  client_request_.peer_info.san_names.dns_names[0] = gpr_strdup("bar.com");
  client_request_.peer_info.san_names.dns_names_size = 1;
  hostname_certificate_verifier_.Verify(&client_request_, [] {});
  EXPECT_EQ(client_request_.status, GRPC_STATUS_UNAUTHENTICATED);
  EXPECT_STREQ(client_request_.error_details,
               "Hostname Verification Check failed.");
}

TEST_F(GrpcTlsCertificateVerifierTest, HostnameVerifierIpCheckGood) {
  client_request_.target_name = gpr_strdup("192.168.0.1:443");
  client_request_.peer_info.san_names.ip_names = new char*[1];
  client_request_.peer_info.san_names.ip_names[0] = gpr_strdup("192.168.0.1");
  client_request_.peer_info.san_names.ip_names_size = 1;
  hostname_certificate_verifier_.Verify(&client_request_, [] {});
  EXPECT_EQ(client_request_.status, GRPC_STATUS_OK);
}

TEST_F(GrpcTlsCertificateVerifierTest, HostnameVerifierIpCheckBad) {
  client_request_.target_name = gpr_strdup("192.168.0.1:443");
  client_request_.peer_info.san_names.ip_names = new char*[1];
  client_request_.peer_info.san_names.ip_names[0] = gpr_strdup("192.168.1.1");
  client_request_.peer_info.san_names.ip_names_size = 1;
  hostname_certificate_verifier_.Verify(&client_request_, [] {});
  EXPECT_EQ(client_request_.status, GRPC_STATUS_UNAUTHENTICATED);
  EXPECT_STREQ(client_request_.error_details,
               "Hostname Verification Check failed.");
}

TEST_F(GrpcTlsCertificateVerifierTest, HostnameVerifierCommonNameCheckGood) {
  client_request_.target_name = gpr_strdup("foo.com:443");
  client_request_.peer_info.common_name = gpr_strdup("foo.com");
  hostname_certificate_verifier_.Verify(&client_request_, [] {});
  EXPECT_EQ(client_request_.status, GRPC_STATUS_OK);
}

TEST_F(GrpcTlsCertificateVerifierTest, HostnameVerifierCommonNameCheckBad) {
  client_request_.target_name = gpr_strdup("foo.com:443");
  client_request_.peer_info.common_name = gpr_strdup("bar.com");
  hostname_certificate_verifier_.Verify(&client_request_, [] {});
  EXPECT_EQ(client_request_.status, GRPC_STATUS_UNAUTHENTICATED);
  EXPECT_STREQ(client_request_.error_details,
               "Hostname Verification Check failed.");
}

}  // namespace testing

}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}

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
#include "src/core/lib/slice/slice_internal.h"
#include "test/core/util/test_config.h"
#include "test/core/util/tls_utils.h"

namespace grpc_core {

namespace testing {

class GrpcTlsCertificateVerifierTest : public ::testing::Test {
 protected:
  void SetUp() override {}

  void TearDown() override {
    grpc_tls_certificate_verifier::CertificateVerificationRequestDestroy(
        &request_);
    if (external_certificate_verifier_ != nullptr) {
      delete external_certificate_verifier_;
    }
  }

  void CreateExternalCertificateVerifier(
      int (*verify)(grpc_tls_certificate_verifier_external* external_verifier,
                    grpc_tls_custom_verification_check_request* request,
                    grpc_tls_on_custom_verification_check_done_cb callback,
                    void* callback_arg, void* user_data),
      void (*cancel)(grpc_tls_certificate_verifier_external* external_verifier,
                     grpc_tls_custom_verification_check_request* request, void* user_data),
      void (*destruct)(
          grpc_tls_certificate_verifier_external* external_verifier, void* user_data)) {
    grpc_tls_certificate_verifier_external* external_verifier =
        new grpc_tls_certificate_verifier_external();
    external_verifier->verify = verify;
    external_verifier->cancel = cancel;
    external_verifier->destruct = destruct;
    // Takes the ownership of external_verifier.
    external_certificate_verifier_ =
        new ExternalCertificateVerifier(external_verifier);
  }

  struct ThreadArgs {
    grpc_tls_custom_verification_check_request* request = nullptr;
    grpc_tls_on_custom_verification_check_done_cb callback;
    void* callback_arg = nullptr;
  };

  struct ExternalVerifierUserDataArgs {
    grpc_core::Thread** thread_ptr_ptr = nullptr;
    gpr_event* event_ptr  = nullptr;
  };

  static int AsyncExternalVerifierGoodVerify(
      grpc_tls_certificate_verifier_external* external_verifier,
      grpc_tls_custom_verification_check_request* request,
      grpc_tls_on_custom_verification_check_done_cb callback,
      void* callback_arg, void* user_data) {
    gpr_log(GPR_ERROR, "in AsyncExternalVerifierGoodVerify");
    ExternalVerifierUserDataArgs* user_data_args = static_cast<ExternalVerifierUserDataArgs*>(user_data);
    grpc_core::Thread** thread_ptr_ptr = user_data_args->thread_ptr_ptr;
    //grpc_core::Thread* thread_ptr = *thread_ptr_ptr;
    gpr_event* event_ptr = user_data_args->event_ptr;

    ThreadArgs* thread_args = new ThreadArgs();
    thread_args->request = request;
    thread_args->callback = callback;
    thread_args->callback_arg = callback_arg;

    delete *thread_ptr_ptr;
    *thread_ptr_ptr = new grpc_core::Thread(
        "AsyncExternalVerifierGoodVerify", &AsyncExternalVerifierGoodVerifyCb,
        static_cast<void*>(thread_args));
    (**thread_ptr_ptr).Start();

    // Now we can notify the main thread that the thread object is set.
    gpr_event_set(event_ptr, reinterpret_cast<void*>(1));

    /*ThreadArgs* thread_args = new ThreadArgs();
    thread_args->request = request;
    thread_args->callback = callback;
    thread_args->callback_arg = callback_arg;
    grpc_core::Thread thread = grpc_core::Thread(
        "AsyncExternalVerifierGoodVerify", &AsyncExternalVerifierGoodVerifyCb,
        static_cast<void*>(thread_args));
    thread.Start();
    // Question: This still behaves like a sync operation. I wanted to return
    // true first, and then at the main thread or TearDown function we join the
    // thread. Is it possible?
    thread.Join();*/
    gpr_log(GPR_ERROR, "out AsyncExternalVerifierGoodVerify");
    return true;
  }

  static void AsyncExternalVerifierGoodVerifyCb(void* args) {
    gpr_log(GPR_ERROR, "in AsyncExternalVerifierGoodVerifyCb");
    ThreadArgs* thread_args = static_cast<ThreadArgs*>(args);
    grpc_tls_custom_verification_check_request* request = thread_args->request;
    grpc_tls_on_custom_verification_check_done_cb callback =
        thread_args->callback;
    void* callback_arg = thread_args->callback_arg;
    request->status = GRPC_STATUS_OK;
    callback(request, callback_arg);
    gpr_log(GPR_ERROR, "out AsyncExternalVerifierGoodVerifyCb");
    delete thread_args;
  }

  static void AsyncExternalVerifierGoodDestruct(
          grpc_tls_certificate_verifier_external* external_verifier, void* user_data) {
    ExternalVerifierUserDataArgs* user_data_args = static_cast<ExternalVerifierUserDataArgs*>(user_data);
    grpc_core::Thread** thread_ptr_ptr = user_data_args->thread_ptr_ptr;
    //grpc_core::Thread* thread_ptr = *thread_ptr_ptr;
    // gpr_event* event_ptr = user_data_args->event_ptr;
    gpr_log(GPR_ERROR, "in AsyncExternalVerifierGoodDestruct");
    delete *thread_ptr_ptr;
    // event_ptr is created on stack, so no need to free it.
    delete user_data_args;
    gpr_log(GPR_ERROR, "out AsyncExternalVerifierGoodDestruct");
  }

 /* void CreateASyncExternalCertificateVerifier(
      int (*verify)(grpc_tls_certificate_verifier_external* external_verifier,
                    grpc_tls_custom_verification_check_request* request,
                    grpc_tls_on_custom_verification_check_done_cb callback,
                    void* callback_arg, void* user_data),
      void (*cancel)(grpc_tls_certificate_verifier_external* external_verifier,
                     grpc_tls_custom_verification_check_request* request, void* user_data),
      void (*destruct)(
          grpc_tls_certificate_verifier_external* external_verifier, void* user_data),
      gpr_event* event_ptr,
      grpc_core::Thread* thread_ptr) {
    grpc_tls_certificate_verifier_external* external_verifier =
        new grpc_tls_certificate_verifier_external();
    external_verifier->verify = verify;
    external_verifier->cancel = cancel;
    external_verifier->destruct = destruct;

    grpc_core::Thread* thread_ptr = new grpc_core::Thread();
    ExternalVerifierUserDataArgs* user_data_args = new ExternalVerifierUserDataArgs();
    user_data_args->thread_ptr = thread_ptr;
    user_data_args->event_ptr = event_ptr;
    external_verifier->user_data = (void *)user_data_args;
    // Takes the ownership of external_verifier.
    external_certificate_verifier_ =
        new ExternalCertificateVerifier(external_verifier);
    return thread_ptr;
  }*/

  static int SyncExternalVerifierGoodVerify(
      grpc_tls_certificate_verifier_external* external_verifier,
      grpc_tls_custom_verification_check_request* request,
      grpc_tls_on_custom_verification_check_done_cb callback,
      void* callback_arg, void* user_data) {
    request->status = GRPC_STATUS_OK;
    return false;
  }

  static int SyncExternalVerifierBadVerify(
      grpc_tls_certificate_verifier_external* external_verifier,
      grpc_tls_custom_verification_check_request* request,
      grpc_tls_on_custom_verification_check_done_cb callback,
      void* callback_arg, void* user_data) {
    request->status = GRPC_STATUS_UNAUTHENTICATED;
    request->error_details = gpr_strdup("SyncExternalVerifierBadVerify failed");
    return false;
  }

  grpc_tls_custom_verification_check_request request_;
  ExternalCertificateVerifier* external_certificate_verifier_ = nullptr;
  HostNameCertificateVerifier hostname_certificate_verifier_;
};

/*TEST_F(GrpcTlsCertificateVerifierTest, SyncExternalVerifierGood) {
  CreateExternalCertificateVerifier(SyncExternalVerifierGoodVerify, nullptr,
                                    nullptr);
  external_certificate_verifier_->Verify(&request_, [] {});
  EXPECT_EQ(request_.status, GRPC_STATUS_OK);
}

TEST_F(GrpcTlsCertificateVerifierTest, SyncExternalVerifierBad) {
  CreateExternalCertificateVerifier(SyncExternalVerifierBadVerify, nullptr,
                                    nullptr);
  external_certificate_verifier_->Verify(&request_, [] {});
  EXPECT_EQ(request_.status, GRPC_STATUS_UNAUTHENTICATED);
  EXPECT_STREQ(request_.error_details, "SyncExternalVerifierBadVerify failed");
}*/

TEST_F(GrpcTlsCertificateVerifierTest, AsyncExternalVerifierGood) {
  // Indicates if the thread pointer returned from CreateASyncExternalCertificateVerifier is filled with the real object asynchronously.
  gpr_event event;
  gpr_event_init(&event);

  /*void* user_data = CreateASyncExternalCertificateVerifier(AsyncExternalVerifierGoodVerify, nullptr,
                                    AsyncExternalVerifierGoodDestruct, &event);*/
  grpc_tls_certificate_verifier_external* external_verifier =
      new grpc_tls_certificate_verifier_external();
  external_verifier->verify = AsyncExternalVerifierGoodVerify;
  external_verifier->cancel = nullptr;
  external_verifier->destruct = AsyncExternalVerifierGoodDestruct;

  grpc_core::Thread* thread_ptr = new grpc_core::Thread();
  gpr_log(GPR_ERROR, "thread pointer address: %p", thread_ptr);
  ExternalVerifierUserDataArgs* user_data_args = new ExternalVerifierUserDataArgs();
  user_data_args->thread_ptr_ptr = &thread_ptr;
  user_data_args->event_ptr = &event;
  external_verifier->user_data = (void *)user_data_args;
  // Takes the ownership of external_verifier.
  external_certificate_verifier_ =
      new ExternalCertificateVerifier(external_verifier);




  external_certificate_verifier_->Verify(
      &request_, [] {
        gpr_log(GPR_ERROR, "in lambda");
        gpr_log(GPR_INFO, "Callback is invoked."); });
  gpr_log(GPR_ERROR, "external_certificate_verifier_->Verify is called");
  // This happens probably because at the time this is checked, the value is not set yet...
  // might need some way of signal...
  void* value = gpr_event_wait(
      &event,
      gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                      gpr_time_from_seconds(5, GPR_TIMESPAN)));
  gpr_log(GPR_ERROR, "gpr_event_wait is received");
  EXPECT_NE(value, nullptr);
  gpr_log(GPR_ERROR, "thread pointer address: %p", thread_ptr);
  (*thread_ptr).Join();
  gpr_log(GPR_ERROR, "After joining");
  EXPECT_EQ(request_.status, GRPC_STATUS_OK);
}

/*TEST_F(GrpcTlsCertificateVerifierTest, HostnameVerifierNullTargetName) {
  hostname_certificate_verifier_.Verify(&request_, [] {});
  EXPECT_EQ(request_.status, GRPC_STATUS_UNAUTHENTICATED);
  EXPECT_STREQ(request_.error_details, "Target name is not specified.");
}

TEST_F(GrpcTlsCertificateVerifierTest, HostnameVerifierInvalidTargetName) {
  request_.target_name = gpr_strdup("[foo.com@443");
  hostname_certificate_verifier_.Verify(&request_, [] {});
  EXPECT_EQ(request_.status, GRPC_STATUS_UNAUTHENTICATED);
  EXPECT_STREQ(request_.error_details, "Failed to split hostname and port.");
}

TEST_F(GrpcTlsCertificateVerifierTest, HostnameVerifierURIExactCheckGood) {
  request_.target_name = gpr_strdup("foo.com:443");
  request_.peer_info.san_names.uri_names = new char*[1];
  request_.peer_info.san_names.uri_names[0] = gpr_strdup("foo.com");
  request_.peer_info.san_names.uri_names_size = 1;
  hostname_certificate_verifier_.Verify(&request_, [] {});
  EXPECT_EQ(request_.status, GRPC_STATUS_OK);
}

TEST_F(GrpcTlsCertificateVerifierTest, HostnameVerifierURIWildcardCheckGood) {
  request_.target_name = gpr_strdup("foo.bar.com:443");
  request_.peer_info.san_names.uri_names = new char*[1];
  request_.peer_info.san_names.uri_names[0] = gpr_strdup("*.bar.com");
  request_.peer_info.san_names.uri_names_size = 1;
  hostname_certificate_verifier_.Verify(&request_, [] {});
  EXPECT_EQ(request_.status, GRPC_STATUS_OK);
}

TEST_F(GrpcTlsCertificateVerifierTest, HostnameVerifierURITopWildcardCheckBad) {
  request_.target_name = gpr_strdup("foo.com:443");
  request_.peer_info.san_names.uri_names = new char*[1];
  request_.peer_info.san_names.uri_names[0] = gpr_strdup("*.com");
  request_.peer_info.san_names.uri_names_size = 1;
  hostname_certificate_verifier_.Verify(&request_, [] {});
  EXPECT_EQ(request_.status, GRPC_STATUS_UNAUTHENTICATED);
  EXPECT_STREQ(request_.error_details, "Hostname Verification Check failed.");
}

TEST_F(GrpcTlsCertificateVerifierTest, HostnameVerifierURIExactCheckBad) {
  request_.target_name = gpr_strdup("foo.com:443");
  request_.peer_info.san_names.uri_names = new char*[1];
  request_.peer_info.san_names.uri_names[0] = gpr_strdup("bar.com");
  request_.peer_info.san_names.uri_names_size = 1;
  hostname_certificate_verifier_.Verify(&request_, [] {});
  EXPECT_EQ(request_.status, GRPC_STATUS_UNAUTHENTICATED);
  EXPECT_STREQ(request_.error_details, "Hostname Verification Check failed.");
}

TEST_F(GrpcTlsCertificateVerifierTest, HostnameVerifierIpCheckGood) {
  request_.target_name = gpr_strdup("192.168.0.1:443");
  request_.peer_info.san_names.ip_names = new char*[1];
  request_.peer_info.san_names.ip_names[0] = gpr_strdup("192.168.0.1");
  request_.peer_info.san_names.ip_names_size = 1;
  hostname_certificate_verifier_.Verify(&request_, [] {});
  EXPECT_EQ(request_.status, GRPC_STATUS_OK);
}

TEST_F(GrpcTlsCertificateVerifierTest, HostnameVerifierIpCheckBad) {
  request_.target_name = gpr_strdup("192.168.0.1:443");
  request_.peer_info.san_names.ip_names = new char*[1];
  request_.peer_info.san_names.ip_names[0] = gpr_strdup("192.168.1.1");
  request_.peer_info.san_names.ip_names_size = 1;
  hostname_certificate_verifier_.Verify(&request_, [] {});
  EXPECT_EQ(request_.status, GRPC_STATUS_UNAUTHENTICATED);
  EXPECT_STREQ(request_.error_details, "Hostname Verification Check failed.");
}

TEST_F(GrpcTlsCertificateVerifierTest, HostnameVerifierCommonNameCheckGood) {
  request_.target_name = gpr_strdup("foo.com:443");
  request_.peer_info.common_name = gpr_strdup("foo.com");
  hostname_certificate_verifier_.Verify(&request_, [] {});
  EXPECT_EQ(request_.status, GRPC_STATUS_OK);
}

TEST_F(GrpcTlsCertificateVerifierTest, HostnameVerifierCommonNameCheckBad) {
  request_.target_name = gpr_strdup("foo.com:443");
  request_.peer_info.common_name = gpr_strdup("bar.com");
  hostname_certificate_verifier_.Verify(&request_, [] {});
  EXPECT_EQ(request_.status, GRPC_STATUS_UNAUTHENTICATED);
  EXPECT_STREQ(request_.error_details, "Hostname Verification Check failed.");
}*/

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

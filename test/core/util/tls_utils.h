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

#include "src/core/lib/gprpp/thd.h"
#include "src/core/lib/security/security_connector/ssl_utils.h"

namespace grpc_core {

namespace testing {

class TmpFile {
 public:
  // Create a temporary file with |credential_data| written in.
  explicit TmpFile(absl::string_view credential_data);

  ~TmpFile();

  const std::string& name() { return name_; }

  // Rewrite |credential_data| to the temporary file, in an atomic way.
  void RewriteFile(absl::string_view credential_data);

 private:
  std::string CreateTmpFileAndWriteData(absl::string_view credential_data);

  std::string name_;
};

PemKeyCertPairList MakeCertKeyPairs(absl::string_view private_key,
                                    absl::string_view certs);

std::string GetFileContents(const char* path);

class SyncExternalVerifier {
 public:
  SyncExternalVerifier(bool is_good);

  struct UserData {
    SyncExternalVerifier* self = nullptr;
    bool is_good = false;
  };

  grpc_tls_certificate_verifier_external* base() { return &base_; }

 private:
  static int Verify(void* user_data,
                    grpc_tls_custom_verification_check_request* request,
                    grpc_tls_on_custom_verification_check_done_cb callback,
                    void* callback_arg);

  static void Cancel(void* user_data,
                     grpc_tls_custom_verification_check_request* request) {}

  static void Destruct(void* user_data);

  grpc_tls_certificate_verifier_external base_;
};

class AsyncExternalVerifier {
 public:
  AsyncExternalVerifier(bool is_good);

  struct UserData {
    AsyncExternalVerifier* self = nullptr;
    grpc_core::Thread* thread = nullptr;
    bool is_good = false;
  };

  // This is the arg we will pass in when creating the thread, and retrieve it
  // later in the thread callback.
  struct ThreadArgs {
    grpc_tls_custom_verification_check_request* request = nullptr;
    grpc_tls_on_custom_verification_check_done_cb callback;
    void* callback_arg = nullptr;
  };

  grpc_tls_certificate_verifier_external* base() { return &base_; }

 private:
  static int Verify(void* user_data,
                    grpc_tls_custom_verification_check_request* request,
                    grpc_tls_on_custom_verification_check_done_cb callback,
                    void* callback_arg);

  static void Cancel(void* user_data,
                     grpc_tls_custom_verification_check_request* request) {}

  static void Destruct(void* user_data);

  static void AsyncExternalVerifierGoodVerifyCb(void* args);

  static void AsyncExternalVerifierBadVerifyCb(void* args);

  grpc_tls_certificate_verifier_external base_;
};

}  // namespace testing

}  // namespace grpc_core

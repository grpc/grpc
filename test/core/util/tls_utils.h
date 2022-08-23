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

#ifndef TLS_UTILS_H
#define TLS_UTILS_H

#include <deque>
#include <string>

#include "absl/base/thread_annotations.h"
#include "absl/strings/string_view.h"

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/status.h>

#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/thd.h"
#include "src/core/lib/security/security_connector/ssl_utils.h"

namespace grpc_core {

namespace testing {

class TmpFile {
 public:
  // Create a temporary file with |data| written in.
  explicit TmpFile(absl::string_view data);

  ~TmpFile();

  const std::string& name() { return name_; }

  // Rewrite |data| to the temporary file, in an atomic way.
  void RewriteFile(absl::string_view data);

 private:
  std::string CreateTmpFileAndWriteData(absl::string_view data);

  std::string name_;
};

PemKeyCertPairList MakeCertKeyPairs(absl::string_view private_key,
                                    absl::string_view certs);

std::string GetFileContents(const char* path);

// A synchronous external verifier implementation that simply returns
// verification results based on users' inputs. Note that it will delete itself
// in Destruct(), so create it like
// ```
// auto* sync_verifier_ = new SyncExternalVerifier(false);
// ```
// and no need to delete it later. This is basically to keep consistent with the
// semantics in AsyncExternalVerifier.
class SyncExternalVerifier {
 public:
  explicit SyncExternalVerifier(bool success)
      : success_(success), base_{this, Verify, Cancel, Destruct} {}

  grpc_tls_certificate_verifier_external* base() { return &base_; }

 private:
  static int Verify(void* user_data,
                    grpc_tls_custom_verification_check_request* request,
                    grpc_tls_on_custom_verification_check_done_cb callback,
                    void* callback_arg, grpc_status_code* sync_status,
                    char** sync_error_details);

  static void Cancel(void*, grpc_tls_custom_verification_check_request*) {}

  static void Destruct(void* user_data);

  bool success_ = false;
  grpc_tls_certificate_verifier_external base_;
};

// An asynchronous external verifier implementation that runs a thread and
// process each request received from the verifier sequentially. Note that it
// will delete itself in Destruct(), so create it like
// ```
// auto* async_verifier = new AsyncExternalVerifier(true, &event);
// auto* core_external_verifier =
//       new ExternalCertificateVerifier(async_verifier->base());
// ```
// and no need to delete it later.
// We delete AsyncExternalVerifier in Destruct() instead of its dtor because we
// wanted AsyncExternalVerifier to outlive the underlying core
// ExternalCertificateVerifier implementation.
class AsyncExternalVerifier {
 public:
  explicit AsyncExternalVerifier(bool success)
      : success_(success),
        thread_("AsyncExternalVerifierWorkerThread", WorkerThread, this),
        base_{this, Verify, Cancel, Destruct} {
    grpc_init();
    thread_.Start();
  }

  ~AsyncExternalVerifier();

  grpc_tls_certificate_verifier_external* base() { return &base_; }

 private:
  // A request to pass to the worker thread.
  struct Request {
    grpc_tls_custom_verification_check_request* request;
    grpc_tls_on_custom_verification_check_done_cb callback;
    void* callback_arg;
    bool shutdown;  // If true, thread will exit.
  };

  static int Verify(void* user_data,
                    grpc_tls_custom_verification_check_request* request,
                    grpc_tls_on_custom_verification_check_done_cb callback,
                    void* callback_arg, grpc_status_code* sync_status,
                    char** sync_error_details);

  static void Cancel(void*, grpc_tls_custom_verification_check_request*) {}

  static void Destruct(void* user_data);

  static void WorkerThread(void* arg);

  bool success_ = false;
  Thread thread_;
  grpc_tls_certificate_verifier_external base_;
  Mutex mu_;
  std::deque<Request> queue_ ABSL_GUARDED_BY(mu_);
};

}  // namespace testing

}  // namespace grpc_core

#endif

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

#include "test/cpp/util/tls_test_utils.h"

#include <memory>

#include "src/core/lib/gprpp/thd.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

using ::grpc::experimental::TlsCustomVerificationCheckRequest;

namespace grpc {
namespace testing {

bool SyncCertificateVerifier::Verify(TlsCustomVerificationCheckRequest*,
                                     std::function<void(grpc::Status)>,
                                     grpc::Status* sync_status) {
  if (!success_) {
    *sync_status = grpc::Status(grpc::StatusCode::UNAUTHENTICATED,
                                "SyncCertificateVerifier failed");
  } else {
    *sync_status = grpc::Status(grpc::StatusCode::OK, "");
  }
  return true;
}

AsyncCertificateVerifier::AsyncCertificateVerifier(bool success)
    : success_(success),
      thread_("AsyncCertificateVerifierWorkerThread", WorkerThread, this) {
  thread_.Start();
}

AsyncCertificateVerifier::~AsyncCertificateVerifier() {
  // Tell the thread to shut down.
  {
    internal::MutexLock lock(&mu_);
    queue_.push_back(Request{nullptr, nullptr, true});
  }
  // Wait for thread to exit.
  thread_.Join();
}

bool AsyncCertificateVerifier::Verify(
    TlsCustomVerificationCheckRequest* request,
    std::function<void(grpc::Status)> callback, grpc::Status*) {
  internal::MutexLock lock(&mu_);
  queue_.push_back(Request{request, std::move(callback), false});
  return false;  // Asynchronous call
}

void AsyncCertificateVerifier::WorkerThread(void* arg) {
  auto* self = static_cast<AsyncCertificateVerifier*>(arg);
  while (true) {
    // Check queue for work.
    bool got_request = false;
    Request request;
    {
      internal::MutexLock lock(&self->mu_);
      if (!self->queue_.empty()) {
        got_request = true;
        request = self->queue_.front();
        self->queue_.pop_front();
      }
    }
    // If nothing found in the queue, sleep for a bit and try again.
    if (!got_request) {
      gpr_sleep_until(grpc_timeout_milliseconds_to_deadline(100));
      continue;
    }
    // If we're being told to shut down, return.
    if (request.shutdown) return;
    auto return_status = grpc::Status(grpc::StatusCode::OK, "");
    // Process the request.
    if (!self->success_) {
      return_status = grpc::Status(grpc::StatusCode::UNAUTHENTICATED,
                                   "AsyncCertificateVerifier failed");
    }
    request.callback(return_status);
  }
}

bool VerifiedRootCertSubjectVerifier::Verify(
    TlsCustomVerificationCheckRequest* request,
    std::function<void(grpc::Status)>, grpc::Status* sync_status) {
  if (request->verified_root_cert_subject() != expected_subject_) {
    *sync_status = grpc::Status(grpc::StatusCode::UNAUTHENTICATED,
                                "VerifiedRootCertSubjectVerifier failed");
  } else {
    *sync_status = grpc::Status::OK;
  }
  return true;
}

}  // namespace testing
}  // namespace grpc

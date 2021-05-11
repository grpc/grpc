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

namespace grpc {
namespace testing {

bool SyncCertificateVerifier::Verify(TlsCustomVerificationCheckRequest* request,
                                     std::function<void(grpc::Status)> callback,
                                     grpc::Status* sync_status) {
  bool is_done = hostname_verifier_->Verify(
      request,
      [this, callback](grpc::Status async_status) {
        AdditionalSyncCheck(success_, &async_status);
        callback(async_status);
      },
      sync_status);
  if (is_done) {
    AdditionalSyncCheck(success_, sync_status);
  }
  return is_done;
}

void SyncCertificateVerifier::AdditionalSyncCheck(bool success,
                                                  grpc::Status* sync_status) {
  if (!success) {
    if (!sync_status->ok()) {
      *sync_status =
          grpc::Status(sync_status->error_code(),
                       "SyncCertificateVerifier is marked unsuccessful: " +
                           sync_status->error_message());
    } else {
      *sync_status =
          grpc::Status(grpc::StatusCode::UNAUTHENTICATED,
                       "SyncCertificateVerifier is marked unsuccessful");
    }
  }
}

AsyncCertificateVerifier::~AsyncCertificateVerifier() {
  while (true) {
    if (request_map_.empty()) {
      break;
    }
    gpr_sleep_until(grpc_timeout_milliseconds_to_deadline(100));
  }
}

bool AsyncCertificateVerifier::Verify(
    TlsCustomVerificationCheckRequest* request,
    std::function<void(grpc::Status)> callback, grpc::Status* sync_status) {
  {
    const std::lock_guard<std::mutex> lock(mu_);
    request_map_.emplace(request, std::move(callback));
  }
  ThreadArgs* thread_args = new ThreadArgs();
  thread_args->self = this;
  thread_args->request = request;
  bool is_done = hostname_verifier_->Verify(
      request,
      [thread_args](grpc::Status async_status) {
        thread_args->status = async_status;
        grpc_core::Thread thread = grpc_core::Thread(
            "AsyncCertificateVerifierThread", AdditionalAsyncCheck, thread_args,
            nullptr, grpc_core::Thread::Options().set_joinable(false));
        thread.Start();
      },
      sync_status);
  if (is_done) {
    thread_args->status = *sync_status;
    grpc_core::Thread thread = grpc_core::Thread(
        "AsyncCertificateVerifierThread", AdditionalAsyncCheck, thread_args,
        nullptr, grpc_core::Thread::Options().set_joinable(false));
    thread.Start();
  }
  // Our additional check is async here, so the composed check would anyways be
  // an async operation.
  return false;
}

void AsyncCertificateVerifier::AdditionalAsyncCheck(void* arg) {
  auto* thread_args = static_cast<ThreadArgs*>(arg);
  auto* self = thread_args->self;
  auto* request = thread_args->request;
  std::function<void(grpc::Status)> callback;
  {
    const std::lock_guard<std::mutex> lock(self->mu_);
    auto it = self->request_map_.find(request);
    if (it != self->request_map_.end()) {
      callback = std::move(it->second);
    }
  }
  if (callback != nullptr) {
    grpc::Status return_status;
    if (!self->success_) {
      if (!thread_args->status.ok()) {
        return_status =
            grpc::Status(thread_args->status.error_code(),
                         "AsyncCertificateVerifier is marked unsuccessful: " +
                             thread_args->status.error_message());
      } else {
        return_status =
            grpc::Status(thread_args->status.error_code(),
                         "AsyncCertificateVerifier is marked unsuccessful");
      }
    } else {
      return_status = thread_args->status;
    }
    callback(return_status);
  }
  {
    const std::lock_guard<std::mutex> lock(self->mu_);
    auto it = self->request_map_.find(request);
    if (it != self->request_map_.end()) {
      self->request_map_.erase(it);
    }
  }
  delete thread_args;
}

}  // namespace testing
}  // namespace grpc

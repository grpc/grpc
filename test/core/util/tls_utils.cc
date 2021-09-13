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

#include "test/core/util/tls_utils.h"

#include <grpc/support/log.h>

#include "src/core/lib/gpr/tmpfile.h"
#include "src/core/lib/iomgr/load_file.h"
#include "src/core/lib/slice/slice_internal.h"

namespace grpc_core {

namespace testing {

TmpFile::TmpFile(absl::string_view credential_data) {
  name_ = CreateTmpFileAndWriteData(credential_data);
  GPR_ASSERT(!name_.empty());
}

TmpFile::~TmpFile() { GPR_ASSERT(remove(name_.c_str()) == 0); }

void TmpFile::RewriteFile(absl::string_view credential_data) {
  // Create a new file containing new data.
  std::string new_name = CreateTmpFileAndWriteData(credential_data);
  GPR_ASSERT(!new_name.empty());
  // Remove the old file.
  GPR_ASSERT(remove(name_.c_str()) == 0);
  // Rename the new file to the original name.
  GPR_ASSERT(rename(new_name.c_str(), name_.c_str()) == 0);
}

std::string TmpFile::CreateTmpFileAndWriteData(
    absl::string_view credential_data) {
  char* name = nullptr;
  FILE* file_descriptor = gpr_tmpfile("GrpcTlsCertificateProviderTest", &name);
  GPR_ASSERT(fwrite(credential_data.data(), 1, credential_data.size(),
                    file_descriptor) == credential_data.size());
  GPR_ASSERT(fclose(file_descriptor) == 0);
  GPR_ASSERT(file_descriptor != nullptr);
  GPR_ASSERT(name != nullptr);
  std::string name_to_return = name;
  gpr_free(name);
  return name_to_return;
}

PemKeyCertPairList MakeCertKeyPairs(absl::string_view private_key,
                                    absl::string_view certs) {
  if (private_key.empty() && certs.empty()) {
    return {};
  }
  return PemKeyCertPairList{PemKeyCertPair(private_key, certs)};
}

std::string GetFileContents(const char* path) {
  grpc_slice slice = grpc_empty_slice();
  GPR_ASSERT(GRPC_LOG_IF_ERROR("load_file", grpc_load_file(path, 0, &slice)));
  std::string credential = std::string(StringViewFromSlice(slice));
  grpc_slice_unref(slice);
  return credential;
}

int SyncExternalVerifier::Verify(
    void* user_data, grpc_tls_custom_verification_check_request* request,
    grpc_tls_on_custom_verification_check_done_cb callback, void* callback_arg,
    grpc_status_code* sync_status, char** sync_error_details) {
  auto* self = static_cast<SyncExternalVerifier*>(user_data);
  if (self->success_) {
    *sync_status = GRPC_STATUS_OK;
    return true;  // Synchronous call
  }
  *sync_status = GRPC_STATUS_UNAUTHENTICATED;
  *sync_error_details = gpr_strdup("SyncExternalVerifier failed");
  return true;  // Synchronous call
}

void SyncExternalVerifier::Destruct(void* user_data) {
  auto* self = static_cast<SyncExternalVerifier*>(user_data);
  delete self;
}

AsyncExternalVerifier::~AsyncExternalVerifier() {
  // Tell the thread to shut down.
  {
    MutexLock lock(&mu_);
    queue_.push_back(Request{nullptr, nullptr, nullptr, true});
  }
  // Wait for thread to exit.
  thread_.Join();
}

int AsyncExternalVerifier::Verify(
    void* user_data, grpc_tls_custom_verification_check_request* request,
    grpc_tls_on_custom_verification_check_done_cb callback, void* callback_arg,
    grpc_status_code* sync_status, char** sync_error_details) {
  auto* self = static_cast<AsyncExternalVerifier*>(user_data);
  // Add request to queue to be picked up by worker thread.
  MutexLock lock(&self->mu_);
  self->queue_.push_back(Request{request, callback, callback_arg, false});
  return false;  // Asynchronous call
}

void AsyncExternalVerifier::Destruct(void* user_data) {
  auto* self = static_cast<AsyncExternalVerifier*>(user_data);
  delete self;
}

void AsyncExternalVerifier::WorkerThread(void* arg) {
  auto* self = static_cast<AsyncExternalVerifier*>(arg);
  while (true) {
    // Check queue for work.
    bool got_request = false;
    Request request;
    {
      MutexLock lock(&self->mu_);
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
    // Process the request.
    if (self->success_) {
      request.callback(request.request, request.callback_arg, GRPC_STATUS_OK,
                       "");
    } else {
      request.callback(request.request, request.callback_arg,
                       GRPC_STATUS_UNAUTHENTICATED,
                       "AsyncExternalVerifier failed");
    }
  }
}

}  // namespace testing

}  // namespace grpc_core

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

SyncExternalVerifier::SyncExternalVerifier(bool is_good) {
  auto* user_data = new UserData();
  user_data->self = this;
  user_data->is_good = is_good;
  base_.user_data = user_data;
  base_.verify = Verify;
  base_.cancel = Cancel;
  base_.destruct = Destruct;
}

int SyncExternalVerifier::Verify(
    void* user_data, grpc_tls_custom_verification_check_request* request,
    grpc_tls_on_custom_verification_check_done_cb callback,
    void* callback_arg, grpc_status_code* sync_status, char** sync_error_details) {
  auto* data = static_cast<UserData*>(user_data);
  if (data->is_good) {
    return true;  // Synchronous call
  }
  *sync_status = GRPC_STATUS_UNAUTHENTICATED;
  gpr_free(*sync_error_details);
  *sync_error_details = gpr_strdup("SyncExternalVerifierBadVerify failed");
  return true;  // Synchronous call
}

void SyncExternalVerifier::Destruct(void* user_data) {
  auto* data = static_cast<UserData*>(user_data);
  delete data->self;
  delete data;
}

AsyncExternalVerifier::AsyncExternalVerifier(bool is_good,
                                             gpr_event* event_ptr) {
  auto* user_data = new UserData();
  user_data->self = this;
  user_data->is_good = is_good;
  user_data->event_ptr = event_ptr;
  base_.user_data = user_data;
  base_.verify = Verify;
  base_.cancel = Cancel;
  base_.destruct = Destruct;
}

int AsyncExternalVerifier::Verify(
    void* user_data, grpc_tls_custom_verification_check_request* request,
    grpc_tls_on_custom_verification_check_done_cb callback,
    void* callback_arg, grpc_status_code* sync_status, char** sync_error_details) {
  auto* data = static_cast<UserData*>(user_data);
  // Creates the thread args we use when creating the thread.
  ThreadArgs* thread_args = new ThreadArgs();
  thread_args->request = request;
  thread_args->callback = callback;
  thread_args->callback_arg = callback_arg;
  thread_args->event_ptr = data->event_ptr;
  thread_args->is_good = data->is_good;
  data->thread = new grpc_core::Thread("AsyncExternalVerifierVerify",
                                       &AsyncExternalVerifierVerifyCb,
                                       static_cast<void*>(thread_args), nullptr,
                                       Thread::Options().set_joinable(false));
  data->thread->Start();
  return false;  // Asynchronous call
}

void AsyncExternalVerifier::Destruct(void* user_data) {
  auto* data = static_cast<UserData*>(user_data);
  if (data->thread != nullptr) {
    delete data->thread;
  }
  if (data->self != nullptr) {
    delete data->self;
  }
  delete data;
}

void AsyncExternalVerifier::AsyncExternalVerifierVerifyCb(void* args) {
  ThreadArgs* thread_args = static_cast<ThreadArgs*>(args);
  grpc_tls_custom_verification_check_request* request = thread_args->request;
  grpc_tls_on_custom_verification_check_done_cb callback =
      thread_args->callback;
  void* callback_arg = thread_args->callback_arg;
  if (thread_args->is_good) {
    callback(request, callback_arg, GRPC_STATUS_OK, "");
  } else {
    callback(request, callback_arg, GRPC_STATUS_UNAUTHENTICATED,
             "AsyncExternalVerifierBadVerify failed");
  }
  // Now we can notify the main testing thread that the thread object is set.
  if (thread_args->event_ptr != nullptr) {
    gpr_event_set(thread_args->event_ptr, reinterpret_cast<void*>(1));
  }
  delete thread_args;
}

}  // namespace testing

}  // namespace grpc_core

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
#include "test/core/test_util/tls_utils.h"

#include <grpc/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>
#include <stdio.h>

#include "absl/log/check.h"
#include "absl/strings/str_cat.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/util/load_file.h"
#include "src/core/util/tmpfile.h"
#include "test/core/test_util/test_config.h"

namespace grpc_core {

namespace testing {

TmpFile::TmpFile(absl::string_view data) {
  name_ = CreateTmpFileAndWriteData(data);
  CHECK(!name_.empty());
}

TmpFile::~TmpFile() { CHECK_EQ(remove(name_.c_str()), 0); }

void TmpFile::RewriteFile(absl::string_view data) {
  // Create a new file containing new data.
  std::string new_name = CreateTmpFileAndWriteData(data);
  CHECK(!new_name.empty());
#ifdef GPR_WINDOWS
  // Remove the old file.
  // On Windows rename requires that the new name not exist, whereas
  // on posix systems rename does an atomic replacement of the new
  // name.
  CHECK_EQ(remove(name_.c_str()), 0);
#endif
  // Rename the new file to the original name.
  CHECK_EQ(rename(new_name.c_str(), name_.c_str()), 0);
}

std::string TmpFile::CreateTmpFileAndWriteData(absl::string_view data) {
  char* name = nullptr;
  FILE* file_descriptor = gpr_tmpfile("test", &name);
  CHECK(fwrite(data.data(), 1, data.size(), file_descriptor) == data.size());
  CHECK_EQ(fclose(file_descriptor), 0);
  CHECK_NE(file_descriptor, nullptr);
  CHECK_NE(name, nullptr);
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

std::string GetFileContents(const std::string& path) {
  auto slice = LoadFile(path, /*add_null_terminator=*/false);
  if (!slice.ok()) {
    Crash(absl::StrCat("error loading file ", path, ": ",
                       slice.status().ToString()));
  }
  return std::string(slice->as_string_view());
}

int SyncExternalVerifier::Verify(void* user_data,
                                 grpc_tls_custom_verification_check_request*,
                                 grpc_tls_on_custom_verification_check_done_cb,
                                 void*, grpc_status_code* sync_status,
                                 char** sync_error_details) {
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
  grpc_shutdown();
}

int AsyncExternalVerifier::Verify(
    void* user_data, grpc_tls_custom_verification_check_request* request,
    grpc_tls_on_custom_verification_check_done_cb callback, void* callback_arg,
    grpc_status_code*, char**) {
  auto* self = static_cast<AsyncExternalVerifier*>(user_data);
  // Add request to queue to be picked up by worker thread.
  MutexLock lock(&self->mu_);
  self->queue_.push_back(Request{request, callback, callback_arg, false});
  return false;  // Asynchronous call
}

namespace {

void DestroyExternalVerifier(void* arg) {
  auto* verifier = static_cast<AsyncExternalVerifier*>(arg);
  delete verifier;
}

}  // namespace

void AsyncExternalVerifier::Destruct(void* user_data) {
  auto* self = static_cast<AsyncExternalVerifier*>(user_data);
  // Spawn a detached thread to destroy the verifier, to make sure that we
  // don't try to join the worker thread from within the worker thread.
  Thread destroy_thread("DestroyExternalVerifier", DestroyExternalVerifier,
                        self, nullptr, Thread::Options().set_joinable(false));
  destroy_thread.Start();
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
    if (request.shutdown) {
      return;
    }
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

int PeerPropertyExternalVerifier::Verify(
    void* user_data, grpc_tls_custom_verification_check_request* request,
    grpc_tls_on_custom_verification_check_done_cb, void*,
    grpc_status_code* sync_status, char** sync_error_details) {
  auto* self = static_cast<PeerPropertyExternalVerifier*>(user_data);
  if (request->peer_info.verified_root_cert_subject !=
      self->expected_verified_root_cert_subject_) {
    *sync_status = GRPC_STATUS_UNAUTHENTICATED;
    *sync_error_details = gpr_strdup("PeerPropertyExternalVerifier failed");
    return true;
  } else {
    *sync_status = GRPC_STATUS_OK;
    return true;  // Synchronous call
  }
  return true;  // Synchronous call
}

void PeerPropertyExternalVerifier::Destruct(void* user_data) {
  auto* self = static_cast<PeerPropertyExternalVerifier*>(user_data);
  delete self;
}

}  // namespace testing

}  // namespace grpc_core

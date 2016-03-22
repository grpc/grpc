/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef GRPC_INTERNAL_CPP_SERVER_SECURE_SERVER_CREDENTIALS_H
#define GRPC_INTERNAL_CPP_SERVER_SECURE_SERVER_CREDENTIALS_H

#include <memory>

#include <grpc++/security/server_credentials.h>

#include <grpc/grpc_security.h>

#include "src/cpp/server/thread_pool_interface.h"

namespace grpc {

class AuthMetadataProcessorAyncWrapper GRPC_FINAL {
 public:
  static void Destroy(void* wrapper);

  static void Process(void* wrapper, grpc_auth_context* context,
                      const grpc_metadata* md, size_t num_md,
                      grpc_process_auth_metadata_done_cb cb, void* user_data);

  AuthMetadataProcessorAyncWrapper(
      const std::shared_ptr<AuthMetadataProcessor>& processor)
      : thread_pool_(CreateDefaultThreadPool()), processor_(processor) {}

 private:
  void InvokeProcessor(grpc_auth_context* context, const grpc_metadata* md,
                       size_t num_md, grpc_process_auth_metadata_done_cb cb,
                       void* user_data);
  std::unique_ptr<ThreadPoolInterface> thread_pool_;
  std::shared_ptr<AuthMetadataProcessor> processor_;
};

class SecureServerCredentials GRPC_FINAL : public ServerCredentials {
 public:
  explicit SecureServerCredentials(grpc_server_credentials* creds)
      : creds_(creds) {}
  ~SecureServerCredentials() GRPC_OVERRIDE {
    grpc_server_credentials_release(creds_);
  }

  int AddPortToServer(const grpc::string& addr,
                      grpc_server* server) GRPC_OVERRIDE;

  void SetAuthMetadataProcessor(
      const std::shared_ptr<AuthMetadataProcessor>& processor) GRPC_OVERRIDE;

 private:
  grpc_server_credentials* creds_;
  std::unique_ptr<AuthMetadataProcessorAyncWrapper> processor_;
};

}  // namespace grpc

#endif  // GRPC_INTERNAL_CPP_SERVER_SECURE_SERVER_CREDENTIALS_H

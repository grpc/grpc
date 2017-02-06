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

#ifndef GRPC_INTERNAL_CPP_CLIENT_SECURE_CREDENTIALS_H
#define GRPC_INTERNAL_CPP_CLIENT_SECURE_CREDENTIALS_H

#include <grpc/grpc_security.h>

#include <grpc++/security/credentials.h>
#include <grpc++/support/config.h>

#include "src/cpp/server/thread_pool_interface.h"

namespace grpc {

class SecureChannelCredentials final : public ChannelCredentials {
 public:
  explicit SecureChannelCredentials(grpc_channel_credentials* c_creds);
  ~SecureChannelCredentials() { grpc_channel_credentials_release(c_creds_); }
  grpc_channel_credentials* GetRawCreds() { return c_creds_; }

  std::shared_ptr<grpc::Channel> CreateChannel(
      const string& target, const grpc::ChannelArguments& args) override;
  SecureChannelCredentials* AsSecureCredentials() override { return this; }

 private:
  grpc_channel_credentials* const c_creds_;
};

class SecureCallCredentials final : public CallCredentials {
 public:
  explicit SecureCallCredentials(grpc_call_credentials* c_creds);
  ~SecureCallCredentials() { grpc_call_credentials_release(c_creds_); }
  grpc_call_credentials* GetRawCreds() { return c_creds_; }

  bool ApplyToCall(grpc_call* call) override;
  SecureCallCredentials* AsSecureCredentials() override { return this; }

 private:
  grpc_call_credentials* const c_creds_;
};

class MetadataCredentialsPluginWrapper final : private GrpcLibraryCodegen {
 public:
  static void Destroy(void* wrapper);
  static void GetMetadata(void* wrapper, grpc_auth_metadata_context context,
                          grpc_credentials_plugin_metadata_cb cb,
                          void* user_data);

  explicit MetadataCredentialsPluginWrapper(
      std::unique_ptr<MetadataCredentialsPlugin> plugin);

 private:
  void InvokePlugin(grpc_auth_metadata_context context,
                    grpc_credentials_plugin_metadata_cb cb, void* user_data);
  std::unique_ptr<ThreadPoolInterface> thread_pool_;
  std::unique_ptr<MetadataCredentialsPlugin> plugin_;
};

}  // namespace grpc

#endif  // GRPC_INTERNAL_CPP_CLIENT_SECURE_CREDENTIALS_H

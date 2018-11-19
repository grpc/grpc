/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef GRPC_INTERNAL_CPP_CLIENT_SECURE_CREDENTIALS_H
#define GRPC_INTERNAL_CPP_CLIENT_SECURE_CREDENTIALS_H

#include <grpc/grpc_security.h>

#include <grpcpp/security/credentials.h>
#include <grpcpp/support/config.h>

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
  std::shared_ptr<grpc::Channel> CreateChannelWithInterceptors(
      const string& target, const grpc::ChannelArguments& args,
      std::vector<
          std::unique_ptr<experimental::ClientInterceptorFactoryInterface>>
          interceptor_creators) override;
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
  static int GetMetadata(
      void* wrapper, grpc_auth_metadata_context context,
      grpc_credentials_plugin_metadata_cb cb, void* user_data,
      grpc_metadata creds_md[GRPC_METADATA_CREDENTIALS_PLUGIN_SYNC_MAX],
      size_t* num_creds_md, grpc_status_code* status,
      const char** error_details);

  explicit MetadataCredentialsPluginWrapper(
      std::unique_ptr<MetadataCredentialsPlugin> plugin);

 private:
  void InvokePlugin(
      grpc_auth_metadata_context context,
      grpc_credentials_plugin_metadata_cb cb, void* user_data,
      grpc_metadata creds_md[GRPC_METADATA_CREDENTIALS_PLUGIN_SYNC_MAX],
      size_t* num_creds_md, grpc_status_code* status_code,
      const char** error_details);
  std::unique_ptr<ThreadPoolInterface> thread_pool_;
  std::unique_ptr<MetadataCredentialsPlugin> plugin_;
};

}  // namespace grpc

#endif  // GRPC_INTERNAL_CPP_CLIENT_SECURE_CREDENTIALS_H

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

#include <stddef.h>

#include <memory>
#include <string>
#include <vector>

#include "absl/strings/str_cat.h"

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/impl/codegen/grpc_types.h>
#include <grpc/status.h>
#include <grpcpp/channel.h>
#include <grpcpp/impl/grpc_library.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/support/channel_arguments.h>
#include <grpcpp/support/client_interceptor.h>
#include <grpcpp/support/config.h>
// TODO(yashykt): We shouldn't be including "src/core" headers.
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/cpp/server/thread_pool_interface.h"

namespace grpc {

class Channel;

class SecureChannelCredentials final : public ChannelCredentials {
 public:
  explicit SecureChannelCredentials(grpc_channel_credentials* c_creds);
  ~SecureChannelCredentials() override {
    grpc_core::ExecCtx exec_ctx;
    if (c_creds_ != nullptr) c_creds_->Unref();
  }
  grpc_channel_credentials* GetRawCreds() { return c_creds_; }

  std::shared_ptr<Channel> CreateChannelImpl(
      const std::string& target, const ChannelArguments& args) override;

  SecureChannelCredentials* AsSecureCredentials() override { return this; }

 private:
  std::shared_ptr<Channel> CreateChannelWithInterceptors(
      const std::string& target, const ChannelArguments& args,
      std::vector<std::unique_ptr<
          grpc::experimental::ClientInterceptorFactoryInterface>>
          interceptor_creators) override;
  grpc_channel_credentials* const c_creds_;
};

class SecureCallCredentials final : public CallCredentials {
 public:
  explicit SecureCallCredentials(grpc_call_credentials* c_creds);
  ~SecureCallCredentials() override {
    grpc_core::ExecCtx exec_ctx;
    if (c_creds_ != nullptr) c_creds_->Unref();
  }
  grpc_call_credentials* GetRawCreds() { return c_creds_; }

  bool ApplyToCall(grpc_call* call) override;
  SecureCallCredentials* AsSecureCredentials() override { return this; }
  std::string DebugString() override {
    return absl::StrCat("SecureCallCredentials{",
                        std::string(c_creds_->debug_string()), "}");
  }

 private:
  grpc_call_credentials* const c_creds_;
};

namespace internal {

std::shared_ptr<ChannelCredentials> WrapChannelCredentials(
    grpc_channel_credentials* creds);

}  // namespace internal

namespace experimental {

// Transforms C++ STS Credentials options to core options. The pointers of the
// resulting core options point to the memory held by the C++ options so C++
// options need to be kept alive until after the core credentials creation.
grpc_sts_credentials_options StsCredentialsCppToCoreOptions(
    const StsCredentialsOptions& options);

}  // namespace experimental

class MetadataCredentialsPluginWrapper final : private GrpcLibraryCodegen {
 public:
  static void Destroy(void* wrapper);
  static int GetMetadata(
      void* wrapper, grpc_auth_metadata_context context,
      grpc_credentials_plugin_metadata_cb cb, void* user_data,
      grpc_metadata creds_md[GRPC_METADATA_CREDENTIALS_PLUGIN_SYNC_MAX],
      size_t* num_creds_md, grpc_status_code* status,
      const char** error_details);
  static char* DebugString(void* wrapper);

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

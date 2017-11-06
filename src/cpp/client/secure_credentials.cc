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

#include "src/cpp/client/secure_credentials.h"
#include <grpc++/channel.h>
#include <grpc++/impl/grpc_library.h>
#include <grpc++/support/channel_arguments.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include "src/cpp/client/create_channel_internal.h"
#include "src/cpp/common/secure_auth_context.h"

namespace grpc {

static internal::GrpcLibraryInitializer g_gli_initializer;
SecureChannelCredentials::SecureChannelCredentials(
    grpc_channel_credentials* c_creds)
    : c_creds_(c_creds) {
  g_gli_initializer.summon();
}

std::shared_ptr<grpc::Channel> SecureChannelCredentials::CreateChannel(
    const string& target, const grpc::ChannelArguments& args) {
  grpc_channel_args channel_args;
  args.SetChannelArgs(&channel_args);
  return CreateChannelInternal(
      args.GetSslTargetNameOverride(),
      grpc_secure_channel_create(c_creds_, target.c_str(), &channel_args,
                                 nullptr));
}

SecureCallCredentials::SecureCallCredentials(grpc_call_credentials* c_creds)
    : c_creds_(c_creds) {
  g_gli_initializer.summon();
}

bool SecureCallCredentials::ApplyToCall(grpc_call* call) {
  return grpc_call_set_credentials(call, c_creds_) == GRPC_CALL_OK;
}

namespace {
std::shared_ptr<ChannelCredentials> WrapChannelCredentials(
    grpc_channel_credentials* creds) {
  return creds == nullptr ? nullptr
                          : std::shared_ptr<ChannelCredentials>(
                                new SecureChannelCredentials(creds));
}

std::shared_ptr<CallCredentials> WrapCallCredentials(
    grpc_call_credentials* creds) {
  return creds == nullptr ? nullptr
                          : std::shared_ptr<CallCredentials>(
                                new SecureCallCredentials(creds));
}
}  // namespace

std::shared_ptr<ChannelCredentials> GoogleDefaultCredentials() {
  GrpcLibraryCodegen init;  // To call grpc_init().
  return WrapChannelCredentials(grpc_google_default_credentials_create());
}

// Builds SSL Credentials given SSL specific options
std::shared_ptr<ChannelCredentials> SslCredentials(
    const SslCredentialsOptions& options) {
  GrpcLibraryCodegen init;  // To call grpc_init().
  grpc_ssl_pem_key_cert_pair pem_key_cert_pair = {
      options.pem_private_key.c_str(), options.pem_cert_chain.c_str()};

  grpc_channel_credentials* c_creds = grpc_ssl_credentials_create(
      options.pem_root_certs.empty() ? nullptr : options.pem_root_certs.c_str(),
      options.pem_private_key.empty() ? nullptr : &pem_key_cert_pair, nullptr);
  return WrapChannelCredentials(c_creds);
}

// Builds credentials for use when running in GCE
std::shared_ptr<CallCredentials> GoogleComputeEngineCredentials() {
  GrpcLibraryCodegen init;  // To call grpc_init().
  return WrapCallCredentials(
      grpc_google_compute_engine_credentials_create(nullptr));
}

// Builds JWT credentials.
std::shared_ptr<CallCredentials> ServiceAccountJWTAccessCredentials(
    const grpc::string& json_key, long token_lifetime_seconds) {
  GrpcLibraryCodegen init;  // To call grpc_init().
  if (token_lifetime_seconds <= 0) {
    gpr_log(GPR_ERROR,
            "Trying to create JWTCredentials with non-positive lifetime");
    return WrapCallCredentials(nullptr);
  }
  gpr_timespec lifetime =
      gpr_time_from_seconds(token_lifetime_seconds, GPR_TIMESPAN);
  return WrapCallCredentials(grpc_service_account_jwt_access_credentials_create(
      json_key.c_str(), lifetime, nullptr));
}

// Builds refresh token credentials.
std::shared_ptr<CallCredentials> GoogleRefreshTokenCredentials(
    const grpc::string& json_refresh_token) {
  GrpcLibraryCodegen init;  // To call grpc_init().
  return WrapCallCredentials(grpc_google_refresh_token_credentials_create(
      json_refresh_token.c_str(), nullptr));
}

// Builds access token credentials.
std::shared_ptr<CallCredentials> AccessTokenCredentials(
    const grpc::string& access_token) {
  GrpcLibraryCodegen init;  // To call grpc_init().
  return WrapCallCredentials(
      grpc_access_token_credentials_create(access_token.c_str(), nullptr));
}

// Builds IAM credentials.
std::shared_ptr<CallCredentials> GoogleIAMCredentials(
    const grpc::string& authorization_token,
    const grpc::string& authority_selector) {
  GrpcLibraryCodegen init;  // To call grpc_init().
  return WrapCallCredentials(grpc_google_iam_credentials_create(
      authorization_token.c_str(), authority_selector.c_str(), nullptr));
}

// Combines one channel credentials and one call credentials into a channel
// composite credentials.
std::shared_ptr<ChannelCredentials> CompositeChannelCredentials(
    const std::shared_ptr<ChannelCredentials>& channel_creds,
    const std::shared_ptr<CallCredentials>& call_creds) {
  // Note that we are not saving shared_ptrs to the two credentials passed in
  // here. This is OK because the underlying C objects (i.e., channel_creds and
  // call_creds) into grpc_composite_credentials_create will see their refcounts
  // incremented.
  SecureChannelCredentials* s_channel_creds =
      channel_creds->AsSecureCredentials();
  SecureCallCredentials* s_call_creds = call_creds->AsSecureCredentials();
  if (s_channel_creds && s_call_creds) {
    return WrapChannelCredentials(grpc_composite_channel_credentials_create(
        s_channel_creds->GetRawCreds(), s_call_creds->GetRawCreds(), nullptr));
  }
  return nullptr;
}

std::shared_ptr<CallCredentials> CompositeCallCredentials(
    const std::shared_ptr<CallCredentials>& creds1,
    const std::shared_ptr<CallCredentials>& creds2) {
  SecureCallCredentials* s_creds1 = creds1->AsSecureCredentials();
  SecureCallCredentials* s_creds2 = creds2->AsSecureCredentials();
  if (s_creds1 != nullptr && s_creds2 != nullptr) {
    return WrapCallCredentials(grpc_composite_call_credentials_create(
        s_creds1->GetRawCreds(), s_creds2->GetRawCreds(), nullptr));
  }
  return nullptr;
}

void MetadataCredentialsPluginWrapper::Destroy(void* wrapper) {
  if (wrapper == nullptr) return;
  MetadataCredentialsPluginWrapper* w =
      reinterpret_cast<MetadataCredentialsPluginWrapper*>(wrapper);
  delete w;
}

int MetadataCredentialsPluginWrapper::GetMetadata(
    void* wrapper, grpc_auth_metadata_context context,
    grpc_credentials_plugin_metadata_cb cb, void* user_data,
    grpc_metadata creds_md[GRPC_METADATA_CREDENTIALS_PLUGIN_SYNC_MAX],
    size_t* num_creds_md, grpc_status_code* status,
    const char** error_details) {
  GPR_ASSERT(wrapper);
  MetadataCredentialsPluginWrapper* w =
      reinterpret_cast<MetadataCredentialsPluginWrapper*>(wrapper);
  if (!w->plugin_) {
    *num_creds_md = 0;
    *status = GRPC_STATUS_OK;
    *error_details = nullptr;
    return true;
  }
  if (w->plugin_->IsBlocking()) {
    // Asynchronous return.
    w->thread_pool_->Add(
        std::bind(&MetadataCredentialsPluginWrapper::InvokePlugin, w, context,
                  cb, user_data, nullptr, nullptr, nullptr, nullptr));
    return 0;
  } else {
    // Synchronous return.
    w->InvokePlugin(context, cb, user_data, creds_md, num_creds_md, status,
                    error_details);
    return 1;
  }
}

namespace {

void UnrefMetadata(const std::vector<grpc_metadata>& md) {
  for (auto it = md.begin(); it != md.end(); ++it) {
    grpc_slice_unref(it->key);
    grpc_slice_unref(it->value);
  }
}

}  // namespace

void MetadataCredentialsPluginWrapper::InvokePlugin(
    grpc_auth_metadata_context context, grpc_credentials_plugin_metadata_cb cb,
    void* user_data, grpc_metadata creds_md[4], size_t* num_creds_md,
    grpc_status_code* status_code, const char** error_details) {
  std::multimap<grpc::string, grpc::string> metadata;

  // const_cast is safe since the SecureAuthContext does not take owndership and
  // the object is passed as a const ref to plugin_->GetMetadata.
  SecureAuthContext cpp_channel_auth_context(
      const_cast<grpc_auth_context*>(context.channel_auth_context), false);

  Status status = plugin_->GetMetadata(context.service_url, context.method_name,
                                       cpp_channel_auth_context, &metadata);
  std::vector<grpc_metadata> md;
  for (auto it = metadata.begin(); it != metadata.end(); ++it) {
    grpc_metadata md_entry;
    md_entry.key = SliceFromCopiedString(it->first);
    md_entry.value = SliceFromCopiedString(it->second);
    md_entry.flags = 0;
    md.push_back(md_entry);
  }
  if (creds_md != nullptr) {
    // Synchronous return.
    if (md.size() > GRPC_METADATA_CREDENTIALS_PLUGIN_SYNC_MAX) {
      *num_creds_md = 0;
      *status_code = GRPC_STATUS_INTERNAL;
      *error_details = gpr_strdup(
          "blocking plugin credentials returned too many metadata keys");
      UnrefMetadata(md);
    } else {
      for (const auto& elem : md) {
        creds_md[*num_creds_md].key = elem.key;
        creds_md[*num_creds_md].value = elem.value;
        creds_md[*num_creds_md].flags = elem.flags;
        ++(*num_creds_md);
      }
      *status_code = static_cast<grpc_status_code>(status.error_code());
      *error_details =
          status.ok() ? nullptr : gpr_strdup(status.error_message().c_str());
    }
  } else {
    // Asynchronous return.
    cb(user_data, md.empty() ? nullptr : &md[0], md.size(),
       static_cast<grpc_status_code>(status.error_code()),
       status.error_message().c_str());
    UnrefMetadata(md);
  }
}

MetadataCredentialsPluginWrapper::MetadataCredentialsPluginWrapper(
    std::unique_ptr<MetadataCredentialsPlugin> plugin)
    : thread_pool_(CreateDefaultThreadPool()), plugin_(std::move(plugin)) {}

std::shared_ptr<CallCredentials> MetadataCredentialsFromPlugin(
    std::unique_ptr<MetadataCredentialsPlugin> plugin) {
  GrpcLibraryCodegen init;  // To call grpc_init().
  const char* type = plugin->GetType();
  MetadataCredentialsPluginWrapper* wrapper =
      new MetadataCredentialsPluginWrapper(std::move(plugin));
  grpc_metadata_credentials_plugin c_plugin = {
      MetadataCredentialsPluginWrapper::GetMetadata,
      MetadataCredentialsPluginWrapper::Destroy, wrapper, type};
  return WrapCallCredentials(
      grpc_metadata_credentials_create_from_plugin(c_plugin, nullptr));
}

}  // namespace grpc

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

#include <grpc/impl/codegen/slice.h>
#include <grpc/slice.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpcpp/channel.h>
#include <grpcpp/impl/codegen/status.h>
#include <grpcpp/impl/grpc_library.h>
#include <grpcpp/support/channel_arguments.h>

#include "src/core/lib/gpr/env.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/iomgr/load_file.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/security/transport/auth_filters.h"
#include "src/core/lib/security/util/json_util.h"
#include "src/cpp/client/create_channel_internal.h"
#include "src/cpp/common/secure_auth_context.h"

namespace grpc_impl {

static grpc::internal::GrpcLibraryInitializer g_gli_initializer;
SecureChannelCredentials::SecureChannelCredentials(
    grpc_channel_credentials* c_creds)
    : c_creds_(c_creds) {
  g_gli_initializer.summon();
}

std::shared_ptr<Channel> SecureChannelCredentials::CreateChannelImpl(
    const grpc::string& target, const ChannelArguments& args) {
  return CreateChannelWithInterceptors(
      target, args,
      std::vector<std::unique_ptr<
          grpc::experimental::ClientInterceptorFactoryInterface>>());
}

std::shared_ptr<Channel>
SecureChannelCredentials::CreateChannelWithInterceptors(
    const grpc::string& target, const ChannelArguments& args,
    std::vector<
        std::unique_ptr<grpc::experimental::ClientInterceptorFactoryInterface>>
        interceptor_creators) {
  grpc_channel_args channel_args;
  args.SetChannelArgs(&channel_args);
  return ::grpc::CreateChannelInternal(
      args.GetSslTargetNameOverride(),
      grpc_secure_channel_create(c_creds_, target.c_str(), &channel_args,
                                 nullptr),
      std::move(interceptor_creators));
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
  grpc::GrpcLibraryCodegen init;  // To call grpc_init().
  return WrapChannelCredentials(grpc_google_default_credentials_create());
}

// Builds SSL Credentials given SSL specific options
std::shared_ptr<ChannelCredentials> SslCredentials(
    const SslCredentialsOptions& options) {
  grpc::GrpcLibraryCodegen init;  // To call grpc_init().
  grpc_ssl_pem_key_cert_pair pem_key_cert_pair = {
      options.pem_private_key.c_str(), options.pem_cert_chain.c_str()};

  grpc_channel_credentials* c_creds = grpc_ssl_credentials_create(
      options.pem_root_certs.empty() ? nullptr : options.pem_root_certs.c_str(),
      options.pem_private_key.empty() ? nullptr : &pem_key_cert_pair, nullptr,
      nullptr);
  return WrapChannelCredentials(c_creds);
}

namespace experimental {

namespace {

void ClearStsCredentialsOptions(StsCredentialsOptions* options) {
  if (options == nullptr) return;
  options->token_exchange_service_uri.clear();
  options->resource.clear();
  options->audience.clear();
  options->scope.clear();
  options->requested_token_type.clear();
  options->subject_token_path.clear();
  options->subject_token_type.clear();
  options->actor_token_path.clear();
  options->actor_token_type.clear();
}

}  // namespace

// Builds STS credentials options from JSON.
grpc::Status StsCredentialsOptionsFromJson(const grpc::string& json_string,
                                           StsCredentialsOptions* options) {
  if (options == nullptr) {
    return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                        "options cannot be nullptr.");
  }
  ClearStsCredentialsOptions(options);
  grpc_error* error = GRPC_ERROR_NONE;
  grpc_core::Json json = grpc_core::Json::Parse(json_string.c_str(), &error);
  if (error != GRPC_ERROR_NONE ||
      json.type() != grpc_core::Json::Type::OBJECT) {
    GRPC_ERROR_UNREF(error);
    return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "Invalid json.");
  }

  // Required fields.
  const char* value = grpc_json_get_string_property(
      json, "token_exchange_service_uri", nullptr);
  if (value == nullptr) {
    ClearStsCredentialsOptions(options);
    return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                        "token_exchange_service_uri must be specified.");
  }
  options->token_exchange_service_uri.assign(value);
  value = grpc_json_get_string_property(json, "subject_token_path", nullptr);
  if (value == nullptr) {
    ClearStsCredentialsOptions(options);
    return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                        "subject_token_path must be specified.");
  }
  options->subject_token_path.assign(value);
  value = grpc_json_get_string_property(json, "subject_token_type", nullptr);
  if (value == nullptr) {
    ClearStsCredentialsOptions(options);
    return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                        "subject_token_type must be specified.");
  }
  options->subject_token_type.assign(value);

  // Optional fields.
  value = grpc_json_get_string_property(json, "resource", nullptr);
  if (value != nullptr) options->resource.assign(value);
  value = grpc_json_get_string_property(json, "audience", nullptr);
  if (value != nullptr) options->audience.assign(value);
  value = grpc_json_get_string_property(json, "scope", nullptr);
  if (value != nullptr) options->scope.assign(value);
  value = grpc_json_get_string_property(json, "requested_token_type", nullptr);
  if (value != nullptr) options->requested_token_type.assign(value);
  value = grpc_json_get_string_property(json, "actor_token_path", nullptr);
  if (value != nullptr) options->actor_token_path.assign(value);
  value = grpc_json_get_string_property(json, "actor_token_type", nullptr);
  if (value != nullptr) options->actor_token_type.assign(value);

  return grpc::Status();
}

// Builds STS credentials Options from the $STS_CREDENTIALS env var.
grpc::Status StsCredentialsOptionsFromEnv(StsCredentialsOptions* options) {
  if (options == nullptr) {
    return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                        "options cannot be nullptr.");
  }
  ClearStsCredentialsOptions(options);
  grpc_slice json_string = grpc_empty_slice();
  char* sts_creds_path = gpr_getenv("STS_CREDENTIALS");
  grpc_error* error = GRPC_ERROR_NONE;
  grpc::Status status;
  auto cleanup = [&json_string, &sts_creds_path, &error, &status]() {
    grpc_slice_unref_internal(json_string);
    gpr_free(sts_creds_path);
    GRPC_ERROR_UNREF(error);
    return status;
  };

  if (sts_creds_path == nullptr) {
    status = grpc::Status(grpc::StatusCode::NOT_FOUND,
                          "STS_CREDENTIALS environment variable not set.");
    return cleanup();
  }
  error = grpc_load_file(sts_creds_path, 1, &json_string);
  if (error != GRPC_ERROR_NONE) {
    status =
        grpc::Status(grpc::StatusCode::NOT_FOUND, grpc_error_string(error));
    return cleanup();
  }
  status = StsCredentialsOptionsFromJson(
      reinterpret_cast<const char*>(GRPC_SLICE_START_PTR(json_string)),
      options);
  return cleanup();
}

// C++ to Core STS Credentials options.
grpc_sts_credentials_options StsCredentialsCppToCoreOptions(
    const StsCredentialsOptions& options) {
  grpc_sts_credentials_options opts;
  memset(&opts, 0, sizeof(opts));
  opts.token_exchange_service_uri = options.token_exchange_service_uri.c_str();
  opts.resource = options.resource.c_str();
  opts.audience = options.audience.c_str();
  opts.scope = options.scope.c_str();
  opts.requested_token_type = options.requested_token_type.c_str();
  opts.subject_token_path = options.subject_token_path.c_str();
  opts.subject_token_type = options.subject_token_type.c_str();
  opts.actor_token_path = options.actor_token_path.c_str();
  opts.actor_token_type = options.actor_token_type.c_str();
  return opts;
}

// Builds STS credentials.
std::shared_ptr<CallCredentials> StsCredentials(
    const StsCredentialsOptions& options) {
  auto opts = StsCredentialsCppToCoreOptions(options);
  return WrapCallCredentials(grpc_sts_credentials_create(&opts, nullptr));
}

std::shared_ptr<CallCredentials> MetadataCredentialsFromPlugin(
    std::unique_ptr<MetadataCredentialsPlugin> plugin,
    grpc_security_level min_security_level) {
  grpc::GrpcLibraryCodegen init;  // To call grpc_init().
  const char* type = plugin->GetType();
  grpc::MetadataCredentialsPluginWrapper* wrapper =
      new grpc::MetadataCredentialsPluginWrapper(std::move(plugin));
  grpc_metadata_credentials_plugin c_plugin = {
      grpc::MetadataCredentialsPluginWrapper::GetMetadata,
      grpc::MetadataCredentialsPluginWrapper::Destroy, wrapper, type};
  return WrapCallCredentials(grpc_metadata_credentials_create_from_plugin(
      c_plugin, min_security_level, nullptr));
}

// Builds ALTS Credentials given ALTS specific options
std::shared_ptr<ChannelCredentials> AltsCredentials(
    const AltsCredentialsOptions& options) {
  grpc::GrpcLibraryCodegen init;  // To call grpc_init().
  grpc_alts_credentials_options* c_options =
      grpc_alts_credentials_client_options_create();
  for (const auto& service_account : options.target_service_accounts) {
    grpc_alts_credentials_client_options_add_target_service_account(
        c_options, service_account.c_str());
  }
  grpc_channel_credentials* c_creds = grpc_alts_credentials_create(c_options);
  grpc_alts_credentials_options_destroy(c_options);
  return WrapChannelCredentials(c_creds);
}

// Builds Local Credentials
std::shared_ptr<ChannelCredentials> LocalCredentials(
    grpc_local_connect_type type) {
  grpc::GrpcLibraryCodegen init;  // To call grpc_init().
  return WrapChannelCredentials(grpc_local_credentials_create(type));
}

// Builds TLS Credentials given TLS options.
std::shared_ptr<ChannelCredentials> TlsCredentials(
    const TlsCredentialsOptions& options) {
  return WrapChannelCredentials(
      grpc_tls_credentials_create(options.c_credentials_options()));
}

}  // namespace experimental

// Builds credentials for use when running in GCE
std::shared_ptr<CallCredentials> GoogleComputeEngineCredentials() {
  grpc::GrpcLibraryCodegen init;  // To call grpc_init().
  return WrapCallCredentials(
      grpc_google_compute_engine_credentials_create(nullptr));
}

// Builds JWT credentials.
std::shared_ptr<CallCredentials> ServiceAccountJWTAccessCredentials(
    const grpc::string& json_key, long token_lifetime_seconds) {
  grpc::GrpcLibraryCodegen init;  // To call grpc_init().
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
  grpc::GrpcLibraryCodegen init;  // To call grpc_init().
  return WrapCallCredentials(grpc_google_refresh_token_credentials_create(
      json_refresh_token.c_str(), nullptr));
}

// Builds access token credentials.
std::shared_ptr<CallCredentials> AccessTokenCredentials(
    const grpc::string& access_token) {
  grpc::GrpcLibraryCodegen init;  // To call grpc_init().
  return WrapCallCredentials(
      grpc_access_token_credentials_create(access_token.c_str(), nullptr));
}

// Builds IAM credentials.
std::shared_ptr<CallCredentials> GoogleIAMCredentials(
    const grpc::string& authorization_token,
    const grpc::string& authority_selector) {
  grpc::GrpcLibraryCodegen init;  // To call grpc_init().
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

std::shared_ptr<CallCredentials> MetadataCredentialsFromPlugin(
    std::unique_ptr<MetadataCredentialsPlugin> plugin) {
  grpc::GrpcLibraryCodegen init;  // To call grpc_init().
  const char* type = plugin->GetType();
  grpc::MetadataCredentialsPluginWrapper* wrapper =
      new grpc::MetadataCredentialsPluginWrapper(std::move(plugin));
  grpc_metadata_credentials_plugin c_plugin = {
      grpc::MetadataCredentialsPluginWrapper::GetMetadata,
      grpc::MetadataCredentialsPluginWrapper::Destroy, wrapper, type};
  return WrapCallCredentials(grpc_metadata_credentials_create_from_plugin(
      c_plugin, GRPC_PRIVACY_AND_INTEGRITY, nullptr));
}

}  // namespace grpc_impl

namespace grpc {
namespace {
void DeleteWrapper(void* wrapper, grpc_error* /*ignored*/) {
  MetadataCredentialsPluginWrapper* w =
      static_cast<MetadataCredentialsPluginWrapper*>(wrapper);
  delete w;
}
}  // namespace

void MetadataCredentialsPluginWrapper::Destroy(void* wrapper) {
  if (wrapper == nullptr) return;
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
  grpc_core::ExecCtx exec_ctx;
  grpc_core::Executor::Run(GRPC_CLOSURE_CREATE(DeleteWrapper, wrapper, nullptr),
                           GRPC_ERROR_NONE);
}

int MetadataCredentialsPluginWrapper::GetMetadata(
    void* wrapper, grpc_auth_metadata_context context,
    grpc_credentials_plugin_metadata_cb cb, void* user_data,
    grpc_metadata creds_md[GRPC_METADATA_CREDENTIALS_PLUGIN_SYNC_MAX],
    size_t* num_creds_md, grpc_status_code* status,
    const char** error_details) {
  GPR_ASSERT(wrapper);
  MetadataCredentialsPluginWrapper* w =
      static_cast<MetadataCredentialsPluginWrapper*>(wrapper);
  if (!w->plugin_) {
    *num_creds_md = 0;
    *status = GRPC_STATUS_OK;
    *error_details = nullptr;
    return 1;
  }
  if (w->plugin_->IsBlocking()) {
    // The internals of context may be destroyed if GetMetadata is cancelled.
    // Make a copy for InvokePlugin.
    grpc_auth_metadata_context context_copy = grpc_auth_metadata_context();
    grpc_auth_metadata_context_copy(&context, &context_copy);
    // Asynchronous return.
    w->thread_pool_->Add([w, context_copy, cb, user_data]() mutable {
      w->MetadataCredentialsPluginWrapper::InvokePlugin(
          context_copy, cb, user_data, nullptr, nullptr, nullptr, nullptr);
      grpc_auth_metadata_context_reset(&context_copy);
    });
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
  for (const auto& metadatum : md) {
    grpc_slice_unref(metadatum.key);
    grpc_slice_unref(metadatum.value);
  }
}

}  // namespace

void MetadataCredentialsPluginWrapper::InvokePlugin(
    grpc_auth_metadata_context context, grpc_credentials_plugin_metadata_cb cb,
    void* user_data, grpc_metadata creds_md[4], size_t* num_creds_md,
    grpc_status_code* status_code, const char** error_details) {
  std::multimap<grpc::string, grpc::string> metadata;

  // const_cast is safe since the SecureAuthContext only inc/dec the refcount
  // and the object is passed as a const ref to plugin_->GetMetadata.
  SecureAuthContext cpp_channel_auth_context(
      const_cast<grpc_auth_context*>(context.channel_auth_context));

  Status status = plugin_->GetMetadata(context.service_url, context.method_name,
                                       cpp_channel_auth_context, &metadata);
  std::vector<grpc_metadata> md;
  for (auto& metadatum : metadata) {
    grpc_metadata md_entry;
    md_entry.key = SliceFromCopiedString(metadatum.first);
    md_entry.value = SliceFromCopiedString(metadatum.second);
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

}  // namespace grpc

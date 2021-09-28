/*
 *
 * Copyright 2018 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/lib/security/credentials/tls/grpc_tls_credentials_options.h"

#include <stdlib.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/surface/api_trace.h"

/** -- gRPC TLS server authorization check API implementation. -- **/
grpc_tls_server_authorization_check_config::
    grpc_tls_server_authorization_check_config(
        const void* config_user_data,
        int (*schedule)(void* config_user_data,
                        grpc_tls_server_authorization_check_arg* arg),
        void (*cancel)(void* config_user_data,
                       grpc_tls_server_authorization_check_arg* arg),
        void (*destruct)(void* config_user_data))
    : config_user_data_(const_cast<void*>(config_user_data)),
      schedule_(schedule),
      cancel_(cancel),
      destruct_(destruct) {}

grpc_tls_server_authorization_check_config::
    ~grpc_tls_server_authorization_check_config() {
  if (destruct_ != nullptr) {
    destruct_(config_user_data_);
  }
}

int grpc_tls_server_authorization_check_config::Schedule(
    grpc_tls_server_authorization_check_arg* arg) const {
  if (schedule_ == nullptr) {
    gpr_log(GPR_ERROR, "schedule API is nullptr");
    if (arg != nullptr) {
      arg->status = GRPC_STATUS_NOT_FOUND;
      arg->error_details->set_error_details(
          "schedule API in server authorization check config is nullptr");
    }
    return 1;
  }
  if (arg != nullptr && context_ != nullptr) {
    arg->config = const_cast<grpc_tls_server_authorization_check_config*>(this);
  }
  return schedule_(config_user_data_, arg);
}

void grpc_tls_server_authorization_check_config::Cancel(
    grpc_tls_server_authorization_check_arg* arg) const {
  if (cancel_ == nullptr) {
    gpr_log(GPR_ERROR, "cancel API is nullptr.");
    if (arg != nullptr) {
      arg->status = GRPC_STATUS_NOT_FOUND;
      arg->error_details->set_error_details(
          "schedule API in server authorization check config is nullptr");
    }
    return;
  }
  if (arg != nullptr) {
    arg->config = const_cast<grpc_tls_server_authorization_check_config*>(this);
  }
  cancel_(config_user_data_, arg);
}

/** -- Wrapper APIs declared in grpc_security.h -- **/

grpc_tls_credentials_options* grpc_tls_credentials_options_create() {
  grpc_core::ExecCtx exec_ctx;
  return new grpc_tls_credentials_options();
}

void grpc_tls_credentials_options_set_cert_request_type(
    grpc_tls_credentials_options* options,
    grpc_ssl_client_certificate_request_type type) {
  GPR_ASSERT(options != nullptr);
  options->set_cert_request_type(type);
}

void grpc_tls_credentials_options_set_server_verification_option(
    grpc_tls_credentials_options* options,
    grpc_tls_server_verification_option server_verification_option) {
  GPR_ASSERT(options != nullptr);
  options->set_server_verification_option(server_verification_option);
}

void grpc_tls_credentials_options_set_certificate_provider(
    grpc_tls_credentials_options* options,
    grpc_tls_certificate_provider* provider) {
  GPR_ASSERT(options != nullptr);
  GPR_ASSERT(provider != nullptr);
  grpc_core::ExecCtx exec_ctx;
  options->set_certificate_provider(
      provider->Ref(DEBUG_LOCATION, "set_certificate_provider"));
}

void grpc_tls_credentials_options_watch_root_certs(
    grpc_tls_credentials_options* options) {
  GPR_ASSERT(options != nullptr);
  options->set_watch_root_cert(true);
}

void grpc_tls_credentials_options_set_root_cert_name(
    grpc_tls_credentials_options* options, const char* root_cert_name) {
  GPR_ASSERT(options != nullptr);
  options->set_root_cert_name(root_cert_name);
}

void grpc_tls_credentials_options_watch_identity_key_cert_pairs(
    grpc_tls_credentials_options* options) {
  GPR_ASSERT(options != nullptr);
  options->set_watch_identity_pair(true);
}

void grpc_tls_credentials_options_set_identity_cert_name(
    grpc_tls_credentials_options* options, const char* identity_cert_name) {
  GPR_ASSERT(options != nullptr);
  options->set_identity_cert_name(identity_cert_name);
}

void grpc_tls_credentials_options_set_server_authorization_check_config(
    grpc_tls_credentials_options* options,
    grpc_tls_server_authorization_check_config* config) {
  GPR_ASSERT(options != nullptr);
  GPR_ASSERT(config != nullptr);
  grpc_core::ExecCtx exec_ctx;
  options->set_server_authorization_check_config(config->Ref());
}

grpc_tls_server_authorization_check_config*
grpc_tls_server_authorization_check_config_create(
    const void* config_user_data,
    int (*schedule)(void* config_user_data,
                    grpc_tls_server_authorization_check_arg* arg),
    void (*cancel)(void* config_user_data,
                   grpc_tls_server_authorization_check_arg* arg),
    void (*destruct)(void* config_user_data)) {
  if (schedule == nullptr) {
    gpr_log(GPR_ERROR,
            "Schedule API is nullptr in creating TLS server authorization "
            "check config.");
    return nullptr;
  }
  grpc_core::ExecCtx exec_ctx;
  return new grpc_tls_server_authorization_check_config(
      config_user_data, schedule, cancel, destruct);
}

void grpc_tls_server_authorization_check_config_release(
    grpc_tls_server_authorization_check_config* config) {
  GRPC_API_TRACE(
      "grpc_tls_server_authorization_check_config_release(config=%p)", 1,
      (config));
  grpc_core::ExecCtx exec_ctx;
  if (config != nullptr) config->Unref();
}

grpc_tls_credentials_options::~grpc_tls_credentials_options() {
 if (tls_session_key_logger_) {
   reinterpret_cast<tsi::TlsSessionKeyLogger*>(
       tls_session_key_logger_)->Unref();
 }
}

void grpc_tls_credentials_options_set_tls_session_key_log_config(
    grpc_tls_credentials_options* options,
    struct grpc_tls_session_key_log_config* config) {
  if (!tsi_tls_session_key_logging_supported() ||
      options == nullptr || config == nullptr) {
    return;
  }
  GRPC_API_TRACE(
      "grpc_tls_credentials_options_set_tls_session_key_log_config(options=%p)", 1,
      (options));

  // Tls session key logging is assumed to be enabled if the specified log
  // file is non-empty.
  if (options->tls_session_key_logger() == nullptr &&
      !config->tls_session_key_log_file_path().empty()) {
    gpr_log(
        GPR_INFO, "Enabling TLS session key logging with keys stored at: %s",
        config->tls_session_key_log_file_path().c_str());
    auto tls_session_key_logger =
        tsi::TlsSessionKeyLoggerRegistry::CreateTlsSessionKeyLogger(
            config->get_tsi_config());
    GPR_DEBUG_ASSERT(tls_session_key_logger != nullptr);
    options->set_tls_session_key_logger(
        reinterpret_cast<grpc_tls_session_key_logger*>(tls_session_key_logger));
  }
}

grpc_tls_session_key_log_config* grpc_tls_session_key_log_config_create() {
  grpc_core::RefCountedPtr<grpc_tls_session_key_log_config> config =
      grpc_core::MakeRefCounted<grpc_tls_session_key_log_config>();
  return config.release();
}

void grpc_tls_session_key_log_config_release(
    grpc_tls_session_key_log_config* config) {
  if (config != nullptr) {
    config->Unref();
  }
}

void grpc_tls_session_key_log_config_set_log_format(
    grpc_tls_session_key_log_config* config,
    grpc_tls_session_key_log_format format) {
  if (config != nullptr) {
    config->set_tls_session_key_logging_format(format);
  }
}

void grpc_tls_session_key_log_config_set_log_path(
    grpc_tls_session_key_log_config* config, const char* path) {
  if (config != nullptr) {
    config->set_tls_session_key_log_file_path(path);
  }
}

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

#include "src/core/lib/security/credentials/tls/grpc_tls_credentials_options.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/string_util.h>
#include <stdlib.h>
#include <string.h>

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
    destruct_((void*)config_user_data_);
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
  return new grpc_tls_credentials_options();
}

void grpc_tls_credentials_options_release(
    grpc_tls_credentials_options* options) {
  GRPC_API_TRACE("grpc_tls_credentials_options_release(options=%p)", 1,
                 (options));
  grpc_core::ExecCtx exec_ctx;
  if (options != nullptr) options->Unref();
}

int grpc_tls_credentials_options_set_cert_request_type(
    grpc_tls_credentials_options* options,
    grpc_ssl_client_certificate_request_type type) {
  if (options == nullptr) {
    gpr_log(GPR_ERROR,
            "Invalid nullptr arguments to "
            "grpc_tls_credentials_options_set_cert_request_type()");
    return 0;
  }
  options->set_cert_request_type(type);
  return 1;
}

int grpc_tls_credentials_options_set_server_verification_option(
    grpc_tls_credentials_options* options,
    grpc_tls_server_verification_option server_verification_option) {
  if (options == nullptr) {
    gpr_log(GPR_ERROR,
            "Invalid nullptr arguments to "
            "grpc_tls_credentials_options_set_server_verification_option()");
    return 0;
  }
  if (server_verification_option != GRPC_TLS_SERVER_VERIFICATION &&
      options->server_authorization_check_config() == nullptr) {
    gpr_log(GPR_ERROR,
            "server_authorization_check_config needs to be specified when"
            "server_verification_option is not GRPC_TLS_SERVER_VERIFICATION");
    return 0;
  }
  options->set_server_verification_option(server_verification_option);
  return 1;
}

int grpc_tls_credentials_options_set_certificate_provider(
    grpc_tls_credentials_options* options,
    grpc_tls_certificate_provider* provider) {
  if (options == nullptr) {
    gpr_log(GPR_ERROR,
            "Invalid nullptr arguments to "
            "grpc_tls_credentials_options_set_certificate_provider()");
    return 0;
  }
  if (provider == nullptr) {
    gpr_log(GPR_ERROR,
            "Invalid nullptr arguments to "
            "grpc_tls_credentials_options_set_certificate_provider()");
    return 0;
  }
  options->set_certificate_provider(
      provider->Ref(DEBUG_LOCATION, "set_certificate_provider"));
  return 1;
}

int grpc_tls_credentials_options_watch_root_certs(
    grpc_tls_credentials_options* options) {
  if (options == nullptr) {
    gpr_log(GPR_ERROR,
            "Invalid nullptr arguments to "
            "grpc_tls_credentials_options_watch_root_certs()");
    return 0;
  }
  options->watch_root_certs();
  return 1;
}

int grpc_tls_credentials_options_set_root_cert_name(
    grpc_tls_credentials_options* options, const char* root_cert_name) {
  if (options == nullptr) {
    gpr_log(GPR_ERROR,
            "Invalid nullptr arguments to "
            "grpc_tls_credentials_options_set_root_cert_name()");
    return 0;
  }
  options->set_root_cert_name(root_cert_name);
  return 1;
}

int grpc_tls_credentials_options_watch_identity_key_cert_pairs(
    grpc_tls_credentials_options* options) {
  if (options == nullptr) {
    gpr_log(GPR_ERROR,
            "Invalid nullptr arguments to "
            "grpc_tls_credentials_options_watch_identity_certs()");
    return 0;
  }
  options->watch_identity_key_cert_pairs();
  return 1;
}

int grpc_tls_credentials_options_set_identity_cert_name(
    grpc_tls_credentials_options* options, const char* identity_cert_name) {
  if (options == nullptr) {
    gpr_log(GPR_ERROR,
            "Invalid nullptr arguments to "
            "grpc_tls_credentials_options_set_identity_cert_name()");
    return 0;
  }
  options->set_identity_cert_name(identity_cert_name);
  return 1;
}

int grpc_tls_credentials_options_set_server_authorization_check_config(
    grpc_tls_credentials_options* options,
    grpc_tls_server_authorization_check_config* config) {
  if (options == nullptr || config == nullptr) {
    gpr_log(
        GPR_ERROR,
        "Invalid nullptr arguments to "
        "grpc_tls_credentials_options_set_server_authorization_check_config()");
    return 0;
  }
  options->set_server_authorization_check_config(config->Ref());
  return 1;
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

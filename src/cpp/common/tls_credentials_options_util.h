/*
 *
 * Copyright 2019 gRPC authors.
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

#ifndef GRPC_INTERNAL_CPP_COMMON_TLS_CREDENTIALS_OPTIONS_UTIL_H
#define GRPC_INTERNAL_CPP_COMMON_TLS_CREDENTIALS_OPTIONS_UTIL_H

#include <grpc/grpc_security.h>
#include <grpcpp/security/tls_credentials_options.h>

#include "src/core/lib/security/credentials/tls/grpc_tls_credentials_options.h"

namespace grpc_impl {
namespace experimental {

/** The following 2 functions are exposed for testing purposes. **/
grpc_tls_key_materials_config* ConvertToCKeyMaterialsConfig(
    const std::shared_ptr<TlsKeyMaterialsConfig>& config);

std::shared_ptr<TlsKeyMaterialsConfig> ConvertToCppKeyMaterialsConfig(
    const grpc_tls_key_materials_config* config);

/** The following 4 functions convert the user-provided schedule or cancel
 * functions into C style schedule or cancel functions. **/
int TlsCredentialReloadConfigCSchedule(void* config_user_data,
                                       grpc_tls_credential_reload_arg* arg);

void TlsCredentialReloadConfigCCancel(void* config_user_data,
                                      grpc_tls_credential_reload_arg* arg);

int TlsServerAuthorizationCheckConfigCSchedule(
    void* config_user_data, grpc_tls_server_authorization_check_arg* arg);

void TlsServerAuthorizationCheckConfigCCancel(
    void* config_user_data, grpc_tls_server_authorization_check_arg* arg);

}  //  namespace experimental
}  // namespace grpc_impl

#endif  // GRPC_INTERNAL_CPP_COMMON_TLS_CREDENTIALS_OPTIONS_UTIL_H

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

#include "absl/container/inlined_vector.h"

#include <grpcpp/security/tls_credentials_options.h>

#include "src/cpp/common/tls_credentials_options_util.h"

namespace grpc {
namespace experimental {

/** The C schedule and cancel functions for the server authorization check
 * config. They populate a C server authorization check arg with the result
 * of a C++ server authorization check schedule/cancel API. **/
int TlsServerAuthorizationCheckConfigCSchedule(
    void* /*config_user_data*/, grpc_tls_server_authorization_check_arg* arg) {
  if (arg == nullptr || arg->config == nullptr ||
      arg->config->context() == nullptr) {
    gpr_log(GPR_ERROR,
            "server authorization check arg was not properly initialized");
    return 1;
  }
  TlsServerAuthorizationCheckConfig* cpp_config =
      static_cast<TlsServerAuthorizationCheckConfig*>(arg->config->context());
  TlsServerAuthorizationCheckArg* cpp_arg =
      new TlsServerAuthorizationCheckArg(arg);
  int schedule_result = cpp_config->Schedule(cpp_arg);
  return schedule_result;
}

void TlsServerAuthorizationCheckConfigCCancel(
    void* /*config_user_data*/, grpc_tls_server_authorization_check_arg* arg) {
  if (arg == nullptr || arg->config == nullptr ||
      arg->config->context() == nullptr) {
    gpr_log(GPR_ERROR,
            "server authorization check arg was not properly initialized");
    return;
  }
  if (arg->context == nullptr) {
    gpr_log(GPR_ERROR,
            "server authorization check arg schedule has already completed");
    return;
  }
  TlsServerAuthorizationCheckConfig* cpp_config =
      static_cast<TlsServerAuthorizationCheckConfig*>(arg->config->context());
  TlsServerAuthorizationCheckArg* cpp_arg =
      static_cast<TlsServerAuthorizationCheckArg*>(arg->context);
  cpp_config->Cancel(cpp_arg);
}

void TlsServerAuthorizationCheckArgDestroyContext(void* context) {
  if (context != nullptr) {
    TlsServerAuthorizationCheckArg* cpp_arg =
        static_cast<TlsServerAuthorizationCheckArg*>(context);
    delete cpp_arg;
  }
}

}  // namespace experimental
}  // namespace grpc

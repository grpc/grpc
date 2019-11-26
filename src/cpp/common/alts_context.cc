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

#include <grpc/grpc_security.h>
#include <grpcpp/alts_context.h>

#include "src/core/tsi/alts/handshaker/alts_tsi_handshaker.h"
#include "src/cpp/common/secure_auth_context.h"
#include "src/proto/grpc/gcp/altscontext.pb.h"

namespace grpc {
std::unique_ptr<gcp::AltsContext> GetAltsContextFromAuthContext(
    const AuthContext& auth_context) {
  std::vector<string_ref> ctx_vector =
      auth_context.FindPropertyValues(TSI_ALTS_CONTEXT);
  if (ctx_vector.size() != 1) {
    gpr_log(GPR_ERROR, "contains zero or more than one ALTS context.");
    return nullptr;
  }
  std::unique_ptr<gcp::AltsContext> uniq_ctx(new gcp::AltsContext());
  bool success = uniq_ctx.get()->ParseFromArray(ctx_vector[0].data(),
                                                ctx_vector[0].size());
  if (!success) {
    gpr_log(GPR_ERROR, "fails to parse ALTS context.");
    return nullptr;
  }
  return uniq_ctx;
}
}  // namespace grpc

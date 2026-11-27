//
// Copyright 2026 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#ifndef GRPC_SRC_CORE_XDS_GRPC_XDS_CEL_REQUEST_RESOLVER_H
#define GRPC_SRC_CORE_XDS_GRPC_XDS_CEL_REQUEST_RESOLVER_H

#include "cel-c/activation.h"
#include "src/core/xds/grpc/xds_matcher_context.h"

namespace grpc_core {

// CEL variable resolver that exposes the `request` variable defined in
// gRFC A106, backed by an RpcMatchContext.  All attributes are resolved
// lazily, when the expression accesses them.
//
// The resolver holds a reference to the context, which must outlive it and
// any values it returns.  Values returned to CEL are only valid for the
// duration of a single evaluation.  Copies are cheap and refer to the same
// context.
class CelRequestResolver : public cel_VariableResolver {
 public:
  explicit CelRequestResolver(const RpcMatchContext& context);

 private:
  static cel_Trilean Find(const cel_VariableResolver* resolver,
                          cel_StringView name, cel_Value* value,
                          cel_Arena* arena, cel_Status* status);

  const RpcMatchContext& context_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_XDS_GRPC_XDS_CEL_REQUEST_RESOLVER_H

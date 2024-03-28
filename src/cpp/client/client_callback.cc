//
// Copyright 2019 gRPC authors.
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
//

#include <grpc/grpc.h>
#include <grpcpp/support/client_callback.h>
#include <grpcpp/support/status.h>

#include "src/core/lib/surface/call.h"

namespace grpc {
namespace internal {

bool ClientReactor::InternalTrailersOnly(const grpc_call* call) const {
  return grpc_call_is_trailers_only(call);
}

}  // namespace internal
}  // namespace grpc

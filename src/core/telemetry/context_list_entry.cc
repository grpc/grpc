// Copyright 2025 gRPC authors.
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

#include "src/core/telemetry/context_list_entry.h"

namespace grpc_core {

namespace {
CopyContextFn g_get_copied_context_fn = nullptr;
DeleteContextFn g_delete_copied_context_fn = nullptr;
}  // namespace

void GrpcHttp2SetCopyContextFn(CopyContextFn fn) {
  g_get_copied_context_fn = fn;
}

void GrpcHttp2SetDeleteContextFn(DeleteContextFn fn) {
  g_delete_copied_context_fn = fn;
}

CopyContextFn GrpcHttp2GetCopyContextFn() { return g_get_copied_context_fn; }

DeleteContextFn GrpcHttp2GetDeleteContextFn() {
  return g_delete_copied_context_fn;
}

}  // namespace grpc_core

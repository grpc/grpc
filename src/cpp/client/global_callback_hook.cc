// Copyright 2024 gRPC authors.
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

#include <grpcpp/support/global_callback_hook.h>

#include <memory>

#include "absl/base/no_destructor.h"
#include "absl/log/check.h"

namespace grpc {

static absl::NoDestructor<std::shared_ptr<GlobalCallbackHook>> g_callback_hook(
    std::make_shared<DefaultGlobalCallbackHook>());

std::shared_ptr<GlobalCallbackHook> GetGlobalCallbackHook() {
  return *g_callback_hook;
}

void SetGlobalCallbackHook(GlobalCallbackHook* hook) {
  CHECK(hook != nullptr);
  CHECK(hook != (*g_callback_hook).get());
  *g_callback_hook = std::shared_ptr<GlobalCallbackHook>(hook);
}
}  // namespace grpc

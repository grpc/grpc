// Copyright 2023 The gRPC Authors
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
#include <grpc/support/port_platform.h>

#include "src/core/lib/event_engine/thread_local.h"

namespace grpc_event_engine {
namespace experimental {

namespace {
thread_local bool g_thread_local{false};
}  // namespace

void ThreadLocal::SetIsEventEngineThread(bool is) { g_thread_local = is; }
bool ThreadLocal::IsEventEngineThread() { return g_thread_local; }

}  // namespace experimental
}  // namespace grpc_event_engine

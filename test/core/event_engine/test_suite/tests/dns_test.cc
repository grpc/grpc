// Copyright 2022 gRPC authors.
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

#include <gtest/gtest.h>

#include "src/core/lib/iomgr/exec_ctx.h"
#include "test/core/event_engine/test_suite/event_engine_test_framework.h"

namespace grpc_event_engine {
namespace experimental {

void InitDNSTests() {}

}  // namespace experimental
}  // namespace grpc_event_engine

class EventEngineDNSTest : public EventEngineTest {};

// TODO(hork): establish meaningful tests
TEST_F(EventEngineDNSTest, TODO) { grpc_core::ExecCtx exec_ctx; }

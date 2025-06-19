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

#include "test/core/test_util/postmortem_emit.h"

#include "src/core/channelz/channelz_registry.h"
#include "src/core/telemetry/stats.h"

namespace grpc_core {

void PostMortemEmit() {
  std::cerr << "===========================================================\n";
  std::cerr << "🛑 gRPC Test Postmortem Analysis 🛑\n";
  std::cerr << "===========================================================\n";

  std::cerr << "❗ gRPC Statistics:\n"
            << StatsAsJson(global_stats().Collect().get()) << "\n";

  std::cerr << "❗ channelz entities:\n";
  for (const auto& node : channelz::ChannelzRegistry::GetAllEntities()) {
    std::cerr << "  🔴 [" << node->uuid() << ":"
              << channelz::BaseNode::EntityTypeString(node->type())
              << "]: " << node->RenderJsonString() << "\n";
  }
}

}  // namespace grpc_core

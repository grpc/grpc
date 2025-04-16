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

#include "test/core/test_util/postmortem.h"

#include "gtest/gtest.h"
#include "src/core/channelz/channelz_registry.h"
#include "src/core/telemetry/stats.h"

namespace grpc_core {

PostMortem::~PostMortem() {
  if (!::testing::Test::HasFailure()) return;
  Emit();
}

void PostMortem::Emit() {
  LOG(INFO) << "===========================================================";
  LOG(INFO) << "🛑 gRPC Test Postmortem Analysis 🛑";
  LOG(INFO) << "===========================================================";

  LOG(INFO) << "❗ gRPC Statistics:\n"
            << StatsAsJson(global_stats().Collect().get());

  LOG(INFO) << "❗ channelz entities:";
  for (const auto& node : channelz::ChannelzRegistry::GetAllEntities()) {
    LOG(INFO) << "  🔴 [" << node->uuid() << ":"
              << channelz::BaseNode::EntityTypeString(node->type())
              << "]: " << node->RenderJsonString();
  }
}

}  // namespace grpc_core

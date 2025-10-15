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

#include "fuzztest/fuzztest.h"
#include "src/core/channelz/channelz.h"
#include "gtest/gtest.h"

namespace grpc_core::channelz {
namespace {

auto AnyEntityType() {
  return fuzztest::ElementOf<BaseNode::EntityType>(
      {BaseNode::EntityType::kTopLevelChannel,
       BaseNode::EntityType::kInternalChannel,
       BaseNode::EntityType::kSubchannel, BaseNode::EntityType::kSocket,
       BaseNode::EntityType::kListenSocket, BaseNode::EntityType::kServer,
       BaseNode::EntityType::kCall});
}

void EntityTypeRoundTrips(BaseNode::EntityType entity_type) {
  auto kind = BaseNode::EntityTypeToKind(entity_type);
  EXPECT_NE(kind, "");
  EXPECT_EQ(BaseNode::KindToEntityType(kind), entity_type);
}
FUZZ_TEST(ChannelzFuzzer, EntityTypeRoundTrips).WithDomains(AnyEntityType());

void KindRoundTrips(absl::string_view kind) {
  auto entity_type = BaseNode::KindToEntityType(kind);
  if (!entity_type.has_value()) return;
  EXPECT_EQ(BaseNode::EntityTypeToKind(*entity_type), kind);
}
FUZZ_TEST(ChannelzFuzzer, KindRoundTrips);

}  // namespace
}  // namespace grpc_core::channelz

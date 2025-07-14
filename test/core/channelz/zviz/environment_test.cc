//
//
// Copyright 2017 gRPC authors.
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

#include "src/core/channelz/zviz/environment.h"

#include "fuzztest/fuzztest.h"
#include "gtest/gtest.h"
#include "test/core/channelz/zviz/environment_fake.h"

namespace grpc_zviz {
namespace {

TEST(EnvironmentTest, EntityLinkText) {
  grpc::channelz::v2::Entity known_entity;
  known_entity.set_kind("channel");
  EnvironmentFake env({{2, std::move(known_entity)}});
  EXPECT_EQ(env.EntityLinkText(123), "Entity 123");
  EXPECT_EQ(env.EntityLinkText(1), "Entity 1");
  EXPECT_EQ(env.EntityLinkText(2), "Channel 2");
}

void EntityLinkTextNeverEmpty(
    int64_t entity_id,
    absl::flat_hash_map<int64_t, grpc::channelz::v2::Entity> entities) {
  EnvironmentFake env(std::move(entities));
  EXPECT_NE(env.EntityLinkText(entity_id), "");
}
FUZZ_TEST(EnvironmentTest, EntityLinkTextNeverEmpty);

}  // namespace
}  // namespace grpc_zviz

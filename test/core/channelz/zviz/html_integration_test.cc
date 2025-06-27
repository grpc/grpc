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

#include <google/protobuf/text_format.h>

#include <string>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "fuzztest/fuzztest.h"
#include "gtest/gtest.h"
#include "src/core/channelz/zviz/entity.h"
#include "src/core/channelz/zviz/html.h"
#include "src/core/channelz/zviz/layout_html.h"
#include "src/proto/grpc/channelz/v2/channelz.pb.h"
#include "test/core/channelz/zviz/environment_fake.h"

namespace grpc_zviz {
namespace {

std::string Render(
    grpc::channelz::v2::Entity entity,
    absl::flat_hash_map<int64_t, grpc::channelz::v2::Entity> entities = {}) {
  html::Container container("body");
  layout::HtmlElement element(container);
  EnvironmentFake env(std::move(entities));
  Format(env, entity, element);
  return container.Render();
}

std::string Render(
    std::string proto,
    absl::flat_hash_map<int64_t, grpc::channelz::v2::Entity> entities = {}) {
  grpc::channelz::v2::Entity entity;
  CHECK(google::protobuf::TextFormat::ParseFromString(proto, &entity));
  return Render(std::move(entity), std::move(entities));
}

TEST(HtmlIntegrationTest, SimpleEntity) {
  EXPECT_EQ(Render(R"pb(
              kind: "channel" id: 123
            )pb"),
            "<body><div class=\"zviz-banner\">Channel 123</div></body>");
}

void RenderNeverEmpty(
    grpc::channelz::v2::Entity proto,
    absl::flat_hash_map<int64_t, grpc::channelz::v2::Entity> entities = {}) {
  EXPECT_NE(Render(proto, entities), "");
}
FUZZ_TEST(HtmlIntegrationTest, RenderNeverEmpty);

}  // namespace
}  // namespace grpc_zviz

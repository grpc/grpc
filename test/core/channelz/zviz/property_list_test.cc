// Copyright 2025 The gRPC Authors
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

#include "src/core/channelz/zviz/property_list.h"

#include <google/protobuf/text_format.h>

#include <optional>
#include <string>

#include "absl/log/check.h"
#include "absl/strings/string_view.h"
#include "fuzztest/fuzztest.h"
#include "gtest/gtest.h"
#include "src/proto/grpc/channelz/v2/channelz.pb.h"
#include "src/proto/grpc/channelz/v2/property_list.pb.h"

namespace grpc_zviz {
namespace {

using ::grpc::channelz::v2::Entity;

// Helper to construct an Entity proto from a text-format string.
Entity ParseEntity(absl::string_view proto) {
  Entity msg;
  CHECK(
      google::protobuf::TextFormat::ParseFromString(std::string(proto), &msg));
  return msg;
}

TEST(GetPropertyAsStringTest, SimpleProperty) {
  const Entity entity = ParseEntity(R"pb(
    data: {
      name: "info"
      value: {
        [type.googleapis.com/grpc.channelz.v2.PropertyList]: {
          properties: {
            key: "status"
            value: { string_value: "OK" }
          }
          properties: {
            key: "retries"
            value: { int64_value: 5 }
          }
        }
      }
    }
  )pb");

  EXPECT_EQ(GetPropertyAsString(entity, "info.status"), "OK");
  EXPECT_EQ(GetPropertyAsString(entity, "info.retries"), "5");
}

TEST(GetPropertyAsStringTest, NestedProperty) {
  const Entity entity = ParseEntity(R"pb(
    data: {
      name: "call_counts"
      value: {
        [type.googleapis.com/grpc.channelz.v2.PropertyList]: {
          properties: {
            key: "details"
            value: {
              any_value: {
                [type.googleapis.com/grpc.channelz.v2.PropertyList]: {
                  properties: {
                    key: "started"
                    value: { int64_value: 100 }
                  }
                  properties: {
                    key: "succeeded"
                    value: { int64_value: 95 }
                  }
                }
              }
            }
          }
        }
      }
    }
  )pb");

  EXPECT_EQ(GetPropertyAsString(entity, "call_counts.details.started"), "100");
  EXPECT_EQ(GetPropertyAsString(entity, "call_counts.details.succeeded"), "95");
}

TEST(GetPropertyAsStringTest, PathNotFound) {
  const Entity entity = ParseEntity(R"pb(
    data: {
      name: "info"
      value: {
        [type.googleapis.com/grpc.channelz.v2.PropertyList]: {
          properties: {
            key: "status"
            value: { string_value: "OK" }
          }
        }
      }
    }
  )pb");

  // First part of path does not match
  EXPECT_EQ(GetPropertyAsString(entity, "wrong_info.status"), std::nullopt);
  // Second part of path does not match
  EXPECT_EQ(GetPropertyAsString(entity, "info.wrong_status"), std::nullopt);
  // Path is too long
  EXPECT_EQ(GetPropertyAsString(entity, "info.status.extra"), std::nullopt);
}

TEST(GetPropertyAsStringTest, PathRefersToList) {
  const Entity entity = ParseEntity(R"pb(
    data: {
      name: "info"
      value: {
        [type.googleapis.com/grpc.channelz.v2.PropertyList]: {
          properties: {
            key: "status"
            value: { string_value: "OK" }
          }
        }
      }
    }
  )pb");

  // Path is just the data name, should not return a value.
  EXPECT_EQ(GetPropertyAsString(entity, "info"), std::nullopt);
}

TEST(GetPropertyAsStringTest, MultipleDataSections) {
  const Entity entity = ParseEntity(R"pb(
    data: {
      name: "config"
      value: {
        [type.googleapis.com/grpc.channelz.v2.PropertyList]: {
          properties: {
            key: "mode"
            value: { string_value: "fast" }
          }
        }
      }
    }
    data: {
      name: "config"
      value: {
        [type.googleapis.com/grpc.channelz.v2.PropertyList]: {
          properties: {
            key: "mode"
            value: { string_value: "slow" }
          }
        }
      }
    }
  )pb");

  // Should return the value from the first one it finds.
  EXPECT_EQ(GetPropertyAsString(entity, "config.mode"), "fast");
}

TEST(GetPropertyAsStringTest, EmptyPath) {
  Entity entity;
  EXPECT_EQ(GetPropertyAsString(entity, ""), std::nullopt);
}

TEST(GetPropertyAsStringTest, EmptyEntity) {
  Entity entity;
  EXPECT_EQ(GetPropertyAsString(entity, "info.status"), std::nullopt);
}

void GetPropertyAsStringFuzzTest(const grpc::channelz::v2::Entity& entity,
                                 const std::string& path) {
  GetPropertyAsString(entity, path);
}
FUZZ_TEST(PropertyListTest, GetPropertyAsStringFuzzTest);

TEST(GetPropertyAsStringTest, Id) {
  const Entity entity = ParseEntity(R"pb(
    id: 12345
  )pb");
  EXPECT_EQ(GetPropertyAsString(entity, "id"), "12345");
}

}  // namespace
}  // namespace grpc_zviz

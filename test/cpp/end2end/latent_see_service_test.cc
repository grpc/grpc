//
//
// Copyright 2018 gRPC authors.
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

#include "src/cpp/latent_see/latent_see_service.h"

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/port_platform.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>

#include <memory>
#include <sstream>

#include "src/core/util/json/json_reader.h"
#include "src/cpp/latent_see/latent_see_client.h"
#include "src/proto/grpc/channelz/v2/latent_see.grpc.pb.h"
#include "test/core/test_util/test_config.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace grpc {
namespace testing {
namespace {

using grpc_core::Json;

MATCHER_P2(HasStringFieldWithValue, field, value, "") {
  auto f = arg.find(field);
  if (f == arg.end()) {
    *result_listener << "does not have field " << field;
    return false;
  }
  if (f->second.type() != grpc_core::Json::Type::kString) {
    *result_listener << "field " << field << " is a "
                     << absl::StrCat(f->second.type());
    return false;
  }
  if (f->second.string() != value) {
    *result_listener << "field " << field << " is " << f->second.string();
    return false;
  }
  return true;
}

TEST(LatentSeeServiceTest, ProcessLatentSeeTraceMarkWorks) {
  channelz::v2::LatentSeeTrace trace;
  channelz::v2::PropertyList properties;
  channelz::v2::PropertyList nested_properties;
  properties.add_properties()->set_key("foo");
  properties.mutable_properties(0)->mutable_value()->set_string_value("bar");
  properties.add_properties()->set_key("duration");
  properties.mutable_properties(1)
      ->mutable_value()
      ->mutable_duration_value()
      ->set_seconds(1000);
  nested_properties.add_properties()->set_key("foo_nested");
  nested_properties.mutable_properties(0)->mutable_value()->set_string_value(
      "bar_nested");
  properties.add_properties()->set_key("nested");
  properties.mutable_properties(2)
      ->mutable_value()
      ->mutable_any_value()
      ->PackFrom(nested_properties);

  trace.set_name("foo");
  trace.set_tid(1);
  trace.set_timestamp_ns(1000);
  *trace.mutable_mark()->mutable_properties() = properties;
  std::ostringstream out;
  auto output = std::make_unique<grpc_core::latent_see::JsonOutput>(out);
  ProcessLatentSeeTrace(trace, output.get());
  output->Finish();
  output.reset();
  // verify the JSON is parsable
  auto obj = grpc_core::JsonParse(out.str());
  ASSERT_EQ(obj->type(), grpc_core::Json::Type::kArray);
  ASSERT_EQ(obj->array().size(), 1);
  ASSERT_EQ(obj->array()[0].type(), Json::Type::kObject);
  auto obj_0 = obj->array()[0].object();
  EXPECT_THAT(obj_0, HasStringFieldWithValue("name", "foo"));
  EXPECT_THAT(obj_0, HasStringFieldWithValue("ph", "i"));
  ASSERT_EQ(obj_0.find("args")->second.type(), Json::Type::kObject);
  auto args = obj_0.find("args")->second.object();
  EXPECT_THAT(args, HasStringFieldWithValue("foo", "bar"));
  ASSERT_EQ(args.find("nested")->second.type(), Json::Type::kObject);
  auto args_nested = args.find("nested")->second.object();
  EXPECT_THAT(args_nested, HasStringFieldWithValue("foo_nested", "bar_nested"));
}

TEST(LatentSeeServiceTest, Works) {
  auto service =
      std::make_unique<LatentSeeService>(LatentSeeService::Options());
  ServerBuilder builder;
  builder.RegisterService(service.get());
  auto server = builder.BuildAndStart();
  auto channel = server->InProcessChannel(ChannelArguments());
  auto stub = std::make_unique<channelz::v2::LatentSee::Stub>(channel);
  std::ostringstream out;
  auto output = std::make_unique<grpc_core::latent_see::JsonOutput>(out);
  FetchLatentSee(stub.get(), 1.0, output.get()).IgnoreError();
  output.reset();
  // just verify the JSON is parsable - we check specifics elsewhere
  auto obj = grpc_core::JsonParse(out.str());
  CHECK_OK(obj);
  server->Shutdown();
  server.reset();
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

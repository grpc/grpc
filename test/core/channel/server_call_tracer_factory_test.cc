// Copyright 2023 gRPC authors.
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

#include <memory>

#include "gtest/gtest.h"

#include "src/core/lib/channel/call_tracer.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/resource_quota/arena.h"

namespace grpc_core {
namespace {

class TestServerCallTracerFactory : public ServerCallTracerFactory {
 public:
  ServerCallTracer* CreateNewServerCallTracer(
      Arena* /*arena*/, const ChannelArgs& /*args*/) override {
    Crash("Not implemented");
  }
};

TEST(ServerCallTracerFactoryTest, GlobalRegistration) {
  TestServerCallTracerFactory factory;
  ServerCallTracerFactory::RegisterGlobal(&factory);
  EXPECT_EQ(ServerCallTracerFactory::Get(ChannelArgs()), &factory);
}

TEST(ServerCallTracerFactoryTest, UsingChannelArgs) {
  TestServerCallTracerFactory factory;
  ChannelArgs args = ChannelArgs().SetObject(&factory);
  EXPECT_EQ(ServerCallTracerFactory::Get(args), &factory);
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

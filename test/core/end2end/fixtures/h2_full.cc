//
//
// Copyright 2015 gRPC authors.
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

#include <string.h>

#include <functional>
#include <memory>

#include "gtest/gtest.h"

#include <grpc/grpc.h>

#include "src/core/lib/channel/channel_args.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/end2end/fixtures/secure_fixture.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
INSTANTIATE_TEST_SUITE_P(H2Full, CoreEnd2endTest,
                         ::testing::Values(CoreTestConfiguration{
                             "chttp2/fullstack",
                             FEATURE_MASK_SUPPORTS_DELAYED_CONNECTION |
                                 FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
                                 FEATURE_MASK_SUPPORTS_AUTHORITY_HEADER,
                             nullptr,
                             [](const ChannelArgs& /*client_args*/,
                                const ChannelArgs& /*server_args*/) {
                               return std::make_unique<InsecureFixture>();
                             }}));
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int r = RUN_ALL_TESTS();
  grpc_shutdown();
  return r;
}

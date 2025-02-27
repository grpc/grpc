//
//
// Copyright 2020 gRPC authors.
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

#include "src/core/credentials/transport/insecure/insecure_security_connector.h"

#include <grpc/grpc_security.h>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/credentials/transport/tls/ssl_utils.h"
#include "src/core/transport/auth_context.h"
#include "src/core/tsi/transport_security.h"
#include "test/core/test_util/test_config.h"

namespace grpc_core {
namespace testing {
namespace {

TEST(InsecureSecurityConnector, MakeAuthContextTest) {
  auto auth_context = TestOnlyMakeInsecureAuthContext();
  // Verify that peer is not authenticated
  EXPECT_EQ(auth_context->is_authenticated(), false);
  // Verify that GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME is set
  auto it = grpc_auth_context_find_properties_by_name(
      auth_context.get(), GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME);
  const grpc_auth_property* prop = grpc_auth_property_iterator_next(&it);
  ASSERT_NE(prop, nullptr);
  EXPECT_STREQ(prop->name, GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME);
  EXPECT_STREQ(prop->value, kInsecureTransportSecurityType);
  // Verify that security level is set to none
  it = grpc_auth_context_find_properties_by_name(
      auth_context.get(), GRPC_TRANSPORT_SECURITY_LEVEL_PROPERTY_NAME);
  prop = grpc_auth_property_iterator_next(&it);
  ASSERT_NE(prop, nullptr);
  EXPECT_STREQ(prop->value, tsi_security_level_to_string(TSI_SECURITY_NONE));
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  const auto result = RUN_ALL_TESTS();
  return result;
}

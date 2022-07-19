/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <string.h>

#include <gtest/gtest.h>

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/log.h>

#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/resolver/resolver_registry.h"
#include "src/core/lib/security/credentials/fake/fake_credentials.h"
#include "src/core/lib/security/security_connector/security_connector.h"
#include "src/core/lib/surface/channel.h"
#include "test/core/util/test_config.h"

void test_unknown_scheme_target(void) {
  grpc_channel_credentials* creds =
      grpc_fake_transport_security_credentials_create();
  grpc_channel* chan = grpc_channel_create("blah://blah", creds, nullptr);
  grpc_channel_element* elem =
      grpc_channel_stack_element(grpc_channel_get_channel_stack(chan), 0);
  ASSERT_STREQ(elem->filter->name, "lame-client");
  grpc_core::ExecCtx exec_ctx;
  grpc_core::Channel::FromC(chan)->Unref();
  creds->Unref();
}

void test_security_connector_already_in_arg(void) {
  grpc_arg arg = grpc_security_connector_to_arg(nullptr);
  grpc_channel_args args;
  args.num_args = 1;
  args.args = &arg;
  grpc_channel* chan = grpc_channel_create(nullptr, nullptr, &args);
  grpc_channel_element* elem =
      grpc_channel_stack_element(grpc_channel_get_channel_stack(chan), 0);
  ASSERT_STREQ(elem->filter->name, "lame-client");
  grpc_core::ExecCtx exec_ctx;
  grpc_core::Channel::FromC(chan)->Unref();
}

void test_null_creds(void) {
  grpc_channel* chan = grpc_channel_create(nullptr, nullptr, nullptr);
  grpc_channel_element* elem =
      grpc_channel_stack_element(grpc_channel_get_channel_stack(chan), 0);
  ASSERT_STREQ(elem->filter->name, "lame-client");
  grpc_core::ExecCtx exec_ctx;
  grpc_core::Channel::FromC(chan)->Unref();
}

TEST(SecureChannelCreateTest, MainTest) {
  grpc_init();
  test_security_connector_already_in_arg();
  test_null_creds();
  grpc_core::CoreConfiguration::RunWithSpecialConfiguration(
      [](grpc_core::CoreConfiguration::Builder* builder) {
        BuildCoreConfiguration(builder);
        // Avoid default prefix
        builder->resolver_registry()->Reset();
      },
      []() { test_unknown_scheme_target(); });
  grpc_shutdown();
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestGrpcScope grpc_scope;
  return RUN_ALL_TESTS();
}

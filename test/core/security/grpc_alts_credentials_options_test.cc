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

#include "src/core/lib/security/credentials/alts/grpc_alts_credentials_options.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gtest/gtest.h>

#include <grpc/grpc.h>
#include <grpc/support/log.h>

#include "src/core/lib/gprpp/crash.h"

#define ALTS_CLIENT_OPTIONS_TEST_TARGET_SERVICE_ACCOUNT_1 "abc@google.com"
#define ALTS_CLIENT_OPTIONS_TEST_TARGET_SERVICE_ACCOUNT_2 "def@google.com"

const size_t kTargetServiceAccountNum = 2;

TEST(GrpcAltsCredentialsOptionsTest, CopyClientOptionsFailure) {
  // Initialization.
  grpc_alts_credentials_options* options =
      grpc_alts_credentials_client_options_create();
  // Test.
  ASSERT_EQ(grpc_alts_credentials_options_copy(nullptr), nullptr);
  // Cleanup.
  grpc_alts_credentials_options_destroy(options);
}

static size_t get_target_service_account_num(
    grpc_alts_credentials_options* options) {
  auto client_options =
      reinterpret_cast<grpc_alts_credentials_client_options*>(options);
  size_t num = 0;
  target_service_account* node = client_options->target_account_list_head;
  while (node != nullptr) {
    num++;
    node = node->next;
  }
  return num;
}

TEST(GrpcAltsCredentialsOptionsTest, ClientOptionsApiSuccess) {
  // Initialization.
  grpc_alts_credentials_options* options =
      grpc_alts_credentials_client_options_create();
  // Set client options fields.
  grpc_alts_credentials_client_options_add_target_service_account(
      options, ALTS_CLIENT_OPTIONS_TEST_TARGET_SERVICE_ACCOUNT_1);
  grpc_alts_credentials_client_options_add_target_service_account(
      options, ALTS_CLIENT_OPTIONS_TEST_TARGET_SERVICE_ACCOUNT_2);
  // Validate client option fields.
  ASSERT_EQ(get_target_service_account_num(options), kTargetServiceAccountNum);
  auto client_options =
      reinterpret_cast<grpc_alts_credentials_client_options*>(options);
  ASSERT_STREQ(client_options->target_account_list_head->data,
               ALTS_CLIENT_OPTIONS_TEST_TARGET_SERVICE_ACCOUNT_2);
  ASSERT_STREQ(client_options->target_account_list_head->next->data,
               ALTS_CLIENT_OPTIONS_TEST_TARGET_SERVICE_ACCOUNT_1);
  // Perform a copy operation and validate its correctness.
  grpc_alts_credentials_options* new_options =
      grpc_alts_credentials_options_copy(options);
  ASSERT_EQ(get_target_service_account_num(new_options),
            kTargetServiceAccountNum);
  auto new_client_options =
      reinterpret_cast<grpc_alts_credentials_client_options*>(new_options);
  ASSERT_STREQ(new_client_options->target_account_list_head->data,
               ALTS_CLIENT_OPTIONS_TEST_TARGET_SERVICE_ACCOUNT_2);
  ASSERT_STREQ(new_client_options->target_account_list_head->next->data,
               ALTS_CLIENT_OPTIONS_TEST_TARGET_SERVICE_ACCOUNT_1);
  // Cleanup.
  grpc_alts_credentials_options_destroy(options);
  grpc_alts_credentials_options_destroy(new_options);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

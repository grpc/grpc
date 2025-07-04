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

#include <grpc/compression.h>
#include <grpc/credentials.h>
#include <grpc/grpc.h>
#include <grpc/grpc_posix.h>
#include <grpc/grpc_security.h>
#include <grpc/grpc_security_constants.h>
#include <grpc/impl/channel_arg_names.h>
#include <grpc/slice.h>
#include <grpc/status.h>
#include <grpc/support/time.h>

#include <memory>
#include <vector>

#include "src/core/lib/channel/channel_args.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/end2end/fixtures/secure_fixture.h"

namespace grpc_core {

#define GRPC_HTTP2_CORE_CLIENT_CHANNEL_TESTS

#define GRPC_HTTP2_CORE_DEADLINE_SINGLE_HOP_TESTS

#define GRPC_HTTP2_CORE_DEADLINE_TESTS

#define GRPC_HTTP2_CORE_END2END_TEST_LIST \
  "CoreEnd2endTests.SimpleRequest|"       \
  "CoreEnd2endTests.SimpleRequest10"

#define GRPC_HTTP2_CORE_LARGE_SEND_TESTS

#define GRPC_HTTP2_HTTP2_FULLSTACK_SINGLE_HOP_TESTS

#define GRPC_HTTP2_HTTP2_SINGLE_HOP_TESTS

#define GRPC_HTTP2_HTTP2_TESTS

#define GRPC_HTTP2_NO_LOGGING_TESTS

#define GRPC_HTTP2_PER_CALL_CREDS_ON_INSECURE_TESTS

#define GRPC_HTTP2_PER_CALL_CREDS_TESTS

#define GRPC_HTTP2_PROXY_AUTH_TESTS

#define GRPC_HTTP2_RESOURCE_QUOTA_TESTS

#define GRPC_HTTP2_RETRY_HTTP2_TESTS

#define GRPC_HTTP2_RETRY_TESTS

#define GRPC_HTTP2_SECURE_END_2_END_TESTS

#define GRPC_HTTP2_WRITE_BUFFERING_TESTS

#define GRPC_HTTP2_PROMISE_CLIENT_TRANSPORT_ALLOW_LIST \
  GRPC_HTTP2_CORE_CLIENT_CHANNEL_TESTS                 \
  GRPC_HTTP2_CORE_DEADLINE_SINGLE_HOP_TESTS            \
  GRPC_HTTP2_CORE_DEADLINE_TESTS                       \
  GRPC_HTTP2_CORE_END2END_TEST_LIST                    \
  GRPC_HTTP2_CORE_LARGE_SEND_TESTS                     \
  GRPC_HTTP2_HTTP2_FULLSTACK_SINGLE_HOP_TESTS          \
  GRPC_HTTP2_HTTP2_SINGLE_HOP_TESTS                    \
  GRPC_HTTP2_HTTP2_TESTS                               \
  GRPC_HTTP2_NO_LOGGING_TESTS                          \
  GRPC_HTTP2_PER_CALL_CREDS_ON_INSECURE_TESTS          \
  GRPC_HTTP2_PER_CALL_CREDS_TESTS                      \
  GRPC_HTTP2_PROXY_AUTH_TESTS                          \
  GRPC_HTTP2_RESOURCE_QUOTA_TESTS                      \
  GRPC_HTTP2_RETRY_HTTP2_TESTS                         \
  GRPC_HTTP2_RETRY_TESTS                               \
  GRPC_HTTP2_SECURE_END_2_END_TESTS                    \
  GRPC_HTTP2_WRITE_BUFFERING_TESTS

std::vector<CoreTestConfiguration> End2endTestConfigs() {
  return std::vector<CoreTestConfiguration>{
      CoreTestConfiguration{
          /*name=*/GRPC_HTTP2_PH2_CLIENT_TEST_SUITE,
          /*feature_mask=*/FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
              FEATURE_MASK_IS_HTTP2 | FEATURE_MASK_IS_CALL_V3 |
              FEATURE_MASK_IS_PH2_CLIENT | FEATURE_MASK_DO_NOT_FUZZ,
          /*overridden_call_host=*/nullptr,
          /*create_fixture=*/
          [](const ChannelArgs& /*client_args*/,
             const ChannelArgs& /*server_args*/) {
            return std::make_unique<InsecureFixture>();
          },
          /* include_test_suites */ "",
          /* include_specific_tests */
          GRPC_HTTP2_PROMISE_CLIENT_TRANSPORT_ALLOW_LIST,
          /* exclude_specific_tests */ ""},
  };
}

}  // namespace grpc_core

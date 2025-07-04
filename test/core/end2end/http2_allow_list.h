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

#ifndef GRPC_TEST_CORE_END2END_HTTP2ALLOW_LIST_H
#define GRPC_TEST_CORE_END2END_HTTP2ALLOW_LIST_H

// Each MACRO here corresponds to one suite declared in
// test/core/end2end/end2end_tests.h
// For PH2 we will keep track of which tests we want enabled in this file.
// This will keep the suite-wise segregation clean and readable for us.
// As we fix issues in PH2 we expect more and more tests to come into the allow
// list. As of now there are 200+ end to end test. We expect this number to go
// up by the time PH2 development is complete.

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

#define GRPC_HTTP2_PROMISE_TRANSPORT_ALLOW_LIST \
  GRPC_HTTP2_CORE_CLIENT_CHANNEL_TESTS          \
  GRPC_HTTP2_CORE_DEADLINE_SINGLE_HOP_TESTS     \
  GRPC_HTTP2_CORE_DEADLINE_TESTS                \
  GRPC_HTTP2_CORE_END2END_TEST_LIST             \
  GRPC_HTTP2_CORE_LARGE_SEND_TESTS              \
  GRPC_HTTP2_HTTP2_FULLSTACK_SINGLE_HOP_TESTS   \
  GRPC_HTTP2_HTTP2_SINGLE_HOP_TESTS             \
  GRPC_HTTP2_HTTP2_TESTS                        \
  GRPC_HTTP2_NO_LOGGING_TESTS                   \
  GRPC_HTTP2_PER_CALL_CREDS_ON_INSECURE_TESTS   \
  GRPC_HTTP2_PER_CALL_CREDS_TESTS               \
  GRPC_HTTP2_PROXY_AUTH_TESTS                   \
  GRPC_HTTP2_RESOURCE_QUOTA_TESTS               \
  GRPC_HTTP2_RETRY_HTTP2_TESTS                  \
  GRPC_HTTP2_RETRY_TESTS                        \
  GRPC_HTTP2_SECURE_END_2_END_TESTS             \
  GRPC_HTTP2_WRITE_BUFFERING_TESTS

#endif  // GRPC_TEST_CORE_END2END_HTTP2ALLOW_LIST_H

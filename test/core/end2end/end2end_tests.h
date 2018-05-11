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

#ifndef GRPC_TEST_CORE_END2END_END2END_TESTS_H
#define GRPC_TEST_CORE_END2END_END2END_TESTS_H

#include <grpc/grpc.h>

typedef struct grpc_end2end_test_fixture grpc_end2end_test_fixture;
typedef struct grpc_end2end_test_config grpc_end2end_test_config;

/* Test feature flags. */
#define FEATURE_MASK_SUPPORTS_DELAYED_CONNECTION 1
#define FEATURE_MASK_SUPPORTS_HOSTNAME_VERIFICATION 2
#define FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS 4
#define FEATURE_MASK_SUPPORTS_REQUEST_PROXYING 8
#define FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL 16
#define FEATURE_MASK_SUPPORTS_AUTHORITY_HEADER 32
#define FEATURE_MASK_DOES_NOT_SUPPORT_RESOURCE_QUOTA_SERVER 64
#define FEATURE_MASK_DOES_NOT_SUPPORT_NETWORK_STATUS_CHANGE 128
#define FEATURE_MASK_SUPPORTS_WORKAROUNDS 256

#define FAIL_AUTH_CHECK_SERVER_ARG_NAME "fail_auth_check"

struct grpc_end2end_test_fixture {
  grpc_completion_queue* cq;
  grpc_completion_queue* shutdown_cq;
  grpc_server* server;
  grpc_channel* client;
  void* fixture_data;
};

struct grpc_end2end_test_config {
  /* A descriptive name for this test fixture. */
  const char* name;

  /* Which features are supported by this fixture. See feature flags above. */
  uint32_t feature_mask;

  /* If the call host is setup by the fixture (for example, via the
   * GRPC_SSL_TARGET_NAME_OVERRIDE_ARG channel arg), which value should the test
   * expect to find in call_details.host */
  const char* overridden_call_host;

  grpc_end2end_test_fixture (*create_fixture)(grpc_channel_args* client_args,
                                              grpc_channel_args* server_args);
  void (*init_client)(grpc_end2end_test_fixture* f,
                      grpc_channel_args* client_args);
  void (*init_server)(grpc_end2end_test_fixture* f,
                      grpc_channel_args* server_args);
  void (*tear_down_data)(grpc_end2end_test_fixture* f);
};

void grpc_end2end_tests_pre_init(void);
void grpc_end2end_tests(int argc, char** argv, grpc_end2end_test_config config);

const char* get_host_override_string(const char* str,
                                     grpc_end2end_test_config config);
/* Returns a pointer to a statically allocated slice: future invocations
   overwrite past invocations, not threadsafe, etc... */
const grpc_slice* get_host_override_slice(const char* str,
                                          grpc_end2end_test_config config);

void validate_host_override_string(const char* pattern, grpc_slice str,
                                   grpc_end2end_test_config config);

#endif /* GRPC_TEST_CORE_END2END_END2END_TESTS_H */

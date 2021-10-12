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

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <grpc/grpc.h>

typedef struct grpc_end2end_test_fixture grpc_end2end_test_fixture;
typedef struct grpc_end2end_test_config grpc_end2end_test_config;

/* Test feature flags. */
#define FEATURE_MASK_SUPPORTS_DELAYED_CONNECTION 1
#define FEATURE_MASK_SUPPORTS_HOSTNAME_VERIFICATION 2
// Feature mask supports call credentials with a minimum security level of
// GRPC_PRIVACY_AND_INTEGRITY.
#define FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS 4
// Feature mask supports call credentials with a minimum security level of
// GRPC_SECURTITY_NONE.
#define FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS_LEVEL_INSECURE 8
#define FEATURE_MASK_SUPPORTS_REQUEST_PROXYING 16
#define FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL 32
#define FEATURE_MASK_SUPPORTS_AUTHORITY_HEADER 64
#define FEATURE_MASK_DOES_NOT_SUPPORT_RESOURCE_QUOTA_SERVER 128
#define FEATURE_MASK_DOES_NOT_SUPPORT_NETWORK_STATUS_CHANGE 256
#define FEATURE_MASK_SUPPORTS_WORKAROUNDS 512
#define FEATURE_MASK_DOES_NOT_SUPPORT_CLIENT_HANDSHAKE_COMPLETE_FIRST 1024

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

struct grpc_end2end_test_case_options {
  bool needs_fullstack;
  bool needs_dns;
  bool needs_names;
  bool proxyable;
  bool secure;
  bool traceable;
  bool exclude_inproc;
  bool needs_http2;
  bool needs_proxy_auth;
  bool needs_write_buffering;
  bool needs_client_channel;
};

struct grpc_end2end_test_case_config {
  const char* name;
  void (*pre_init_func)();
  void (*test_func)(grpc_end2end_test_config config);
  grpc_end2end_test_case_options options;
};

struct grpc_end2end_test_fixture_options {
  bool fullstack;
  bool includes_proxy;
  bool dns_resolver;
  bool name_resolution;
  bool secure;
  bool tracing;
  bool is_inproc;
  bool is_http2;
  bool supports_proxy_auth;
  bool supports_write_buffering;
  bool client_channel;
};

struct grpc_end2end_test_fixture_config {
  const char* name;
  grpc_end2end_test_fixture_options options;
};

void grpc_end2end_tests_pre_init(void);
void grpc_end2end_tests_run_single(grpc_end2end_test_config config,
                                   const char* test_name);
void grpc_end2end_tests(int argc, char** argv, grpc_end2end_test_config config);

const char* get_host_override_string(const char* str,
                                     grpc_end2end_test_config config);
/* Returns a pointer to a statically allocated slice: future invocations
   overwrite past invocations, not threadsafe, etc... */
const grpc_slice* get_host_override_slice(const char* str,
                                          grpc_end2end_test_config config);

void validate_host_override_string(const char* pattern, grpc_slice str,
                                   grpc_end2end_test_config config);

namespace grpc {
namespace testing {

class CoreEnd2EndTestScenario {
 public:
  CoreEnd2EndTestScenario(grpc_end2end_test_config config,
                          int config_index,
                          int config_count,
                          const std::string& test_name)
      : config(config), config_index(config_index), config_count(config_count), test_name(test_name) {}
  grpc_end2end_test_config config;
  int config_index;
  int config_count;
  const std::string test_name;

  void Run() const { grpc_end2end_tests_run_single(config, test_name.c_str()); }

  static std::string GenScenarioName(
      const ::testing::TestParamInfo<CoreEnd2EndTestScenario>& info) {
    std::string result = "";
    // result + = info.param.config.name;
    // result += '/';
    // TODO: disambiguate test names if multiple configs are used....
    result += info.param.test_name;
    if (info.param.config_count != 1) {
      result += "_" + std::to_string(info.param.config_index);
    }
    return result;
  }

  static std::vector<CoreEnd2EndTestScenario> CreateTestScenarios(
      const char* fixture_name, grpc_end2end_test_config* configs,
      int num_configs);
};

class CoreEnd2EndTest
    : public ::testing::TestWithParam<CoreEnd2EndTestScenario> {
 protected:
  static void SetUpTestCase() { grpc_init(); }
  static void TearDownTestCase() { grpc_shutdown(); }
};

}  // namespace testing
}  // namespace grpc

#endif /* GRPC_TEST_CORE_END2END_END2END_TESTS_H */

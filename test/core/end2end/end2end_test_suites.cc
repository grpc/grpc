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
#include <inttypes.h>
#include <string.h>

#include <algorithm>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <regex>
#include <string>
#include <utility>
#include <vector>

#include "src/core/credentials/transport/fake/fake_credentials.h"
#include "src/core/ext/transport/chaotic_good/client/chaotic_good_connector.h"
#include "src/core/ext/transport/chaotic_good/server/chaotic_good_server.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/port.h"
#include "src/core/util/env.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/host_port.h"
#include "src/core/util/no_destruct.h"
#include "src/core/util/sync.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/end2end/fixtures/h2_oauth2_common.h"
#include "test/core/end2end/fixtures/h2_ssl_cred_reload_fixture.h"
#include "test/core/end2end/fixtures/h2_ssl_tls_common.h"
#include "test/core/end2end/fixtures/h2_tls_common.h"
#include "test/core/end2end/fixtures/http_proxy_fixture.h"
#include "test/core/end2end/fixtures/inproc_fixture.h"
#include "test/core/end2end/fixtures/local_util.h"
#include "test/core/end2end/fixtures/proxy.h"
#include "test/core/end2end/fixtures/secure_fixture.h"
#include "test/core/end2end/fixtures/sockpair_fixture.h"
#include "test/core/test_util/port.h"
#include "test/core/test_util/test_config.h"
#include "test/core/test_util/tls_utils.h"
#include "gtest/gtest.h"
#include "absl/base/thread_annotations.h"
#include "absl/functional/any_invocable.h"
#include "absl/meta/type_traits.h"
#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"

// IWYU pragma: no_include <unistd.h>

#ifdef GRPC_POSIX_SOCKET
#include <fcntl.h>

#include "src/core/lib/iomgr/socket_utils_posix.h"
#include "src/core/lib/iomgr/unix_sockets_posix.h"
#endif

#ifdef GRPC_POSIX_WAKEUP_FD
#include "src/core/lib/iomgr/wakeup_fd_posix.h"
#endif

#define CA_CERT_PATH "src/core/tsi/test_creds/ca.pem"
#define SERVER_CERT_PATH "src/core/tsi/test_creds/server1.pem"
#define SERVER_KEY_PATH "src/core/tsi/test_creds/server1.key"

#define GRPC_END2END_TEST_SUITE_VLOG VLOG(2)

namespace grpc_core {

std::vector<CoreTestConfiguration> AllConfigs() {
  std::vector<CoreTestConfiguration> configs = End2endTestConfigs();
  for (const auto& config : configs) {
    // Setting both no gtest && no fuzz == no config -- better to delete it
    GRPC_CHECK_NE(config.feature_mask &
                      (FEATURE_MASK_DO_NOT_FUZZ | FEATURE_MASK_DO_NOT_GTEST),
                  static_cast<uint32_t>(FEATURE_MASK_DO_NOT_FUZZ |
                                        FEATURE_MASK_DO_NOT_GTEST))
        << "Config specified with no fuzz, no gtest: " << config.name;
  }
  std::sort(configs.begin(), configs.end(),
            [](const CoreTestConfiguration& a, const CoreTestConfiguration& b) {
              return strcmp(a.name, b.name) < 0;
            });
  return configs;
}

namespace {

absl::Span<const CoreTestConfiguration> Configs() {
  static NoDestruct<std::vector<CoreTestConfiguration>> kConfigs(AllConfigs());
  return *kConfigs;
}

}  // namespace

const CoreTestConfiguration* CoreTestConfigurationNamed(
    absl::string_view name) {
  for (const CoreTestConfiguration& config : Configs()) {
    if (config.name == name) return &config;
  }
  return nullptr;
}

// A ConfigQuery queries a database a set of test configurations
// that match some criteria.
class ConfigQuery {
 public:
  explicit ConfigQuery(bool fuzzing) {
    if (GetEnv("GRPC_CI_EXPERIMENTS").has_value()) {
      exclude_features_ |= FEATURE_MASK_EXCLUDE_FROM_EXPERIMENT_RUNS;
    }
    if (fuzzing) {
      exclude_features_ |= FEATURE_MASK_DO_NOT_FUZZ;
    } else {
      exclude_features_ |= FEATURE_MASK_DO_NOT_GTEST;
    }
    exclude_experiments_except_for_configs_.insert(
        {ExperimentIds::kExperimentIdPromiseBasedHttp2ClientTransport,
         {GRPC_HTTP2_PH2_CLIENT_CHTTP2_SERVER_CONFIG,
          GRPC_HTTP2_PH2_CLIENT_CHTTP2_SERVER_CONFIG_FAKE_SECURITY,
          GRPC_HTTP2_PH2_CLIENT_CHTTP2_SERVER_CONFIG_INSECURE_CREDENTIALS,
          GRPC_HTTP2_PH2_CLIENT_CHTTP2_SERVER_CONFIG_FULLSTACK_LOCAL_IPV4,
          GRPC_HTTP2_PH2_CLIENT_CHTTP2_SERVER_CONFIG_FULLSTACK_LOCAL_IPV6,
          GRPC_HTTP2_PH2_CLIENT_CHTTP2_SERVER_CONFIG_SSL_PROXY,
          GRPC_HTTP2_PH2_CLIENT_CHTTP2_SERVER_CONFIG_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_TLS12,
          GRPC_HTTP2_PH2_CLIENT_CHTTP2_SERVER_CONFIG_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_TLS13,
          GRPC_HTTP2_PH2_CLIENT_CHTTP2_SERVER_CONFIG_SIMPLE_SSL_FULLSTACK_TLS12,
          GRPC_HTTP2_PH2_CLIENT_CHTTP2_SERVER_CONFIG_SIMPLE_SSL_FULLSTACK_TLS13,
          GRPC_HTTP2_PH2_CLIENT_CHTTP2_SERVER_CONFIG_SSL_CRED_RELOAD_TLS12,
          GRPC_HTTP2_PH2_CLIENT_CHTTP2_SERVER_CONFIG_SSL_CRED_RELOAD_TLS13,
          GRPC_HTTP2_PH2_CLIENT_CHTTP2_SERVER_CONFIG_CERT_WATCHER_PROVIDER_ASYNC_VERIFIER_TLS13,
          GRPC_HTTP2_PH2_CLIENT_CHTTP2_SERVER_CONFIG_CERT_WATCHER_PROVIDER_SYNC_VERIFIER_TLS12,
          GRPC_HTTP2_PH2_CLIENT_CHTTP2_SERVER_CONFIG_SIMPLE_SSL_FULLSTACK,
          GRPC_HTTP2_PH2_CLIENT_CHTTP2_SERVER_CONFIG_STATIC_PROVIDER_ASYNC_VERIFIER_TLS13,
          GRPC_HTTP2_PH2_CLIENT_CHTTP2_SERVER_CONFIG_RETRY}});
  }
  ConfigQuery(const ConfigQuery&) = delete;
  ConfigQuery& operator=(const ConfigQuery&) = delete;
  // Enforce that the returned configurations have the given features.
  ConfigQuery& EnforceFeatures(uint32_t features) {
    enforce_features_ |= features;
    return *this;
  }
  // Enforce that the returned configurations do not have the given features.
  ConfigQuery& ExcludeFeatures(uint32_t features) {
    exclude_features_ |= features;
    return *this;
  }
  // Enforce that the returned configurations have the given name (regex).
  ConfigQuery& AllowName(const std::string& name) {
    allowed_names_.emplace_back(
        std::regex(name, std::regex_constants::ECMAScript));
    return *this;
  }
  // Enforce that the returned configurations do not have the given name
  // (regex).
  ConfigQuery& ExcludeName(const std::string& name) {
    excluded_names_.emplace_back(
        std::regex(name, std::regex_constants::ECMAScript));
    return *this;
  }

  auto Run() const {
    std::vector<const CoreTestConfiguration*> out;
    for (const CoreTestConfiguration& config : Configs()) {
      if ((config.feature_mask & enforce_features_) == enforce_features_ &&
          (config.feature_mask & exclude_features_) == 0) {
        bool allowed = allowed_names_.empty();
        for (const std::regex& re : allowed_names_) {
          if (std::regex_match(config.name, re)) {
            allowed = true;
            break;
          }
        }
        for (const std::regex& re : excluded_names_) {
          if (std::regex_match(config.name, re)) {
            allowed = false;
            break;
          }
        }
        if (allowed && CanConfigRunWithExperiment(config)) {
          out.push_back(&config);
        }
      }
    }
    return out;
  }

 private:
  bool CanConfigRunWithExperiment(const CoreTestConfiguration& config) const {
    for (const auto& [experiment_id, configs] :
         exclude_experiments_except_for_configs_) {
      if (IsExperimentEnabled(experiment_id) &&
          !configs.contains(config.name)) {
        GRPC_END2END_TEST_SUITE_VLOG
            << "CanConfigRunWithExperiment false {config : " << config.name
            << ", experiment : " << experiment_id << " }";
        return false;
      }
    }
    GRPC_END2END_TEST_SUITE_VLOG
        << "CanConfigRunWithExperiment true {config : " << config.name << " }";
    return true;
  }

  uint32_t enforce_features_ = 0;
  uint32_t exclude_features_ = 0;

  // TODO(tjagtap) : [PH2][P5] Consider deprecating allowed_names_ and
  // excluded_names_ in favour of include_test_suites , include_specific_tests
  // and exclude_specific_tests
  // This is poor design because the suite knows about the config. So when we
  // add a new config, all the tests must know about it. Instead when we add a
  // new config, we must choose which suites we want to include or exclude for
  // it.
  std::vector<std::regex> allowed_names_;
  std::vector<std::regex> excluded_names_;

  // If there is a new feature with its own experiment that we want to enable
  // only for a few Configs, we can list that here. That will make sure that
  // only the selected E2E test configs are run with the new experiment on.
  absl::flat_hash_map<ExperimentIds, absl::flat_hash_set<absl::string_view>>
      exclude_experiments_except_for_configs_;
};

CORE_END2END_TEST_SUITE(CoreEnd2endTests, ConfigQuery(fuzzing).Run());

CORE_END2END_TEST_SUITE(
    SecureEnd2endTests,
    ConfigQuery(fuzzing).EnforceFeatures(FEATURE_MASK_IS_SECURE).Run());

CORE_END2END_TEST_SUITE(CoreLargeSendTests,
                        ConfigQuery(fuzzing)
                            .ExcludeFeatures(FEATURE_MASK_1BYTE_AT_A_TIME |
                                             FEATURE_MASK_ENABLES_TRACES)
                            .Run());

CORE_END2END_TEST_SUITE(
    CoreDeadlineTests,
    ConfigQuery(fuzzing).ExcludeFeatures(FEATURE_MASK_IS_MINSTACK).Run());

CORE_END2END_TEST_SUITE(
    CoreDeadlineSingleHopTests,
    ConfigQuery(fuzzing)
        .ExcludeFeatures(FEATURE_MASK_SUPPORTS_REQUEST_PROXYING |
                         FEATURE_MASK_IS_MINSTACK)
        .Run());

CORE_END2END_TEST_SUITE(
    CoreClientChannelTests,
    ConfigQuery(fuzzing)
        .EnforceFeatures(FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL)
        .Run());

CORE_END2END_TEST_SUITE(
    Http2SingleHopTests,
    ConfigQuery(fuzzing)
        .EnforceFeatures(FEATURE_MASK_IS_HTTP2)
        .ExcludeFeatures(FEATURE_MASK_SUPPORTS_REQUEST_PROXYING |
                         FEATURE_MASK_ENABLES_TRACES)
        .Run());

CORE_END2END_TEST_SUITE(
    Http2FullstackSingleHopTests,
    ConfigQuery(fuzzing)
        .EnforceFeatures(FEATURE_MASK_IS_HTTP2)
        .EnforceFeatures(FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL)
        .ExcludeFeatures(FEATURE_MASK_SUPPORTS_REQUEST_PROXYING)
        .Run());

CORE_END2END_TEST_SUITE(
    RetryTests, ConfigQuery(fuzzing)
                    .EnforceFeatures(FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL)
                    .ExcludeFeatures(FEATURE_MASK_DOES_NOT_SUPPORT_RETRY)
                    .Run());

CORE_END2END_TEST_SUITE(
    WriteBufferingTests,
    ConfigQuery(fuzzing)
        .ExcludeFeatures(FEATURE_MASK_DOES_NOT_SUPPORT_WRITE_BUFFERING)
        .Run());

CORE_END2END_TEST_SUITE(
    Http2Tests,
    ConfigQuery(fuzzing).EnforceFeatures(FEATURE_MASK_IS_HTTP2).Run());

CORE_END2END_TEST_SUITE(
    RetryHttp2Tests,
    ConfigQuery(fuzzing)
        .EnforceFeatures(FEATURE_MASK_IS_HTTP2 |
                         FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL)
        .ExcludeFeatures(FEATURE_MASK_DOES_NOT_SUPPORT_RETRY |
                         FEATURE_MASK_SUPPORTS_REQUEST_PROXYING)
        .Run());

CORE_END2END_TEST_SUITE(
    ResourceQuotaTests,
    ConfigQuery(fuzzing)
        .ExcludeFeatures(FEATURE_MASK_SUPPORTS_REQUEST_PROXYING |
                         FEATURE_MASK_1BYTE_AT_A_TIME)
        .ExcludeName("Chttp2.*Uds.*")
        .ExcludeName("Chttp2HttpProxy")
        .Run());

CORE_END2END_TEST_SUITE(
    PerCallCredsTests,
    ConfigQuery(fuzzing)
        .EnforceFeatures(FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS)
        .Run());

CORE_END2END_TEST_SUITE(
    PerCallCredsOnInsecureTests,
    ConfigQuery(fuzzing)
        .EnforceFeatures(
            FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS_LEVEL_INSECURE)
        .Run());

CORE_END2END_TEST_SUITE(
    NoLoggingTests,
    ConfigQuery(fuzzing).ExcludeFeatures(FEATURE_MASK_ENABLES_TRACES).Run());

CORE_END2END_TEST_SUITE(
    ProxyAuthTests, ConfigQuery(fuzzing).AllowName("Chttp2HttpProxy").Run());

void EnsureSuitesLinked() {}

}  // namespace grpc_core

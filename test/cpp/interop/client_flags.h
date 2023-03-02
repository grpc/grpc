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

#ifndef GRPC_TEST_CPP_INTEROP_CLIENT_FLAGS_H
#define GRPC_TEST_CPP_INTEROP_CLIENT_FLAGS_H

#include <memory>

#include "absl/flags/declare.h"

ABSL_DECLARE_FLAG(bool, use_alts);
ABSL_DECLARE_FLAG(bool, use_tls);
ABSL_DECLARE_FLAG(std::string, custom_credentials_type);
ABSL_DECLARE_FLAG(bool, use_test_ca);
ABSL_DECLARE_FLAG(int32_t, server_port);
ABSL_DECLARE_FLAG(std::string, server_host);
ABSL_DECLARE_FLAG(std::string, server_host_override);
ABSL_DECLARE_FLAG(std::string, test_case);
ABSL_DECLARE_FLAG(int32_t, num_times);
ABSL_DECLARE_FLAG(std::string, default_service_account);
ABSL_DECLARE_FLAG(std::string, service_account_key_file);
ABSL_DECLARE_FLAG(std::string, oauth_scope);
ABSL_DECLARE_FLAG(bool, do_not_abort_on_transient_failures);
ABSL_DECLARE_FLAG(int32_t, soak_iterations);
ABSL_DECLARE_FLAG(int32_t, soak_max_failures);
ABSL_DECLARE_FLAG(int32_t, soak_per_iteration_max_acceptable_latency_ms);
ABSL_DECLARE_FLAG(int32_t, soak_overall_timeout_seconds);
ABSL_DECLARE_FLAG(int32_t, soak_min_time_ms_between_rpcs);
ABSL_DECLARE_FLAG(int32_t, iteration_interval);
ABSL_DECLARE_FLAG(std::string, additional_metadata);
ABSL_DECLARE_FLAG(bool, log_metadata_and_status);


#endif  // GRPC_TEST_CPP_INTEROP_CLIENT_FLAGS_H

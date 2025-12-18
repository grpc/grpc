# Copyright 2017 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

load("//bazel:grpc_build_system.bzl", "grpc_cc_test", "grpc_package")

licenses(["notice"])

grpc_package(
    name = "test/core/credentials/call",
    features = [
        "layering_check",
        "parse_headers",
    ],
)

grpc_cc_test(
    name = "call_credentials_test",
    srcs = ["call_credentials_test.cc"],
    external_deps = [
        "absl/log:check",
        "absl/log:log",
        "gtest",
        "absl/strings",
        "absl/strings:str_format",
        "libssl",
    ],
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc_core_credentials_header",
        "//:grpc_jwt_credentials",
        "//:grpc_security_base",
        "//:httpcli",
        "//:iomgr_timer",
        "//:promise",
        "//:transport_auth_context",
        "//:uri",
        "//src/core:channel_args",
        "//src/core:env",
        "//src/core:error",
        "//src/core:error_utils",
        "//src/core:exec_ctx_wakeup_scheduler",
        "//src/core:gcp_service_account_identity_credentials",
        "//src/core:grpc_check",
        "//src/core:grpc_external_account_credentials",
        "//src/core:grpc_fake_credentials",
        "//src/core:grpc_google_default_credentials",
        "//src/core:grpc_iam_credentials",
        "//src/core:grpc_oauth2_credentials",
        "//src/core:grpc_tls_credentials",
        "//src/core:httpcli_ssl_credentials",
        "//src/core:json_reader",
        "//src/core:jwt_token_file_call_credentials",
        "//src/core:map",
        "//src/core:seq",
        "//src/core:time",
        "//src/core:unique_type_name",
        "//src/core:wait_for_single_owner",
        "//src/core:xds_credentials",
        "//test/core/event_engine:event_engine_test_utils",
        "//test/core/event_engine/fuzzing_event_engine",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
        "//test/core/test_util:test_call_creds",
    ],
)

grpc_cc_test(
    name = "jwt_util_test",
    srcs = ["jwt_util_test.cc"],
    external_deps = [
        "absl/status",
        "absl/strings",
        "gtest",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:gpr",
        "//src/core:jwt_util",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "call_creds_registry_test",
    srcs = ["call_creds_registry_test.cc"],
    external_deps = [
        "gtest",
    ],
    deps = [
        "//:config",
        "//:gpr",
        "//:grpc",
        "//src/core:call_creds_registry",
        "//src/core:jwt_token_file_call_credentials",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
        "//test/core/test_util:test_call_creds",
    ],
)

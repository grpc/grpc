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

grpc_package(name = "test/core/credentials/transport/tls")

grpc_cc_test(
    name = "tls_credentials_test",
    srcs = ["tls_credentials_test.cc"],
    external_deps = [
        "gtest",
    ],
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "system_roots_test",
    srcs = ["system_roots_test.cc"],
    data = [
        "//test/core/credentials/transport/tls/test_data:bundle.pem",
        "//test/core/credentials/transport/tls/test_data:test_roots/cert1.pem",
        "//test/core/credentials/transport/tls/test_data:test_roots/cert2.pem",
        "//test/core/credentials/transport/tls/test_data:test_roots/cert3.pem",
    ],
    external_deps = [
        "gtest",
    ],
    deps = [
        "//:gpr",
        "//:grpc",
        "//src/core:load_file",
        "//src/core:slice",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
    ],
)

grpc_cc_test(
    name = "tls_security_connector_test",
    srcs = ["tls_security_connector_test.cc"],
    data = [
        "//src/core/tsi/test_creds:ca.pem",
        "//src/core/tsi/test_creds:multi-domain.key",
        "//src/core/tsi/test_creds:multi-domain.pem",
        "//src/core/tsi/test_creds:server0.key",
        "//src/core/tsi/test_creds:server0.pem",
        "//src/core/tsi/test_creds:server1.key",
        "//src/core/tsi/test_creds:server1.pem",
        "//test/core/credentials/transport/tls/test_data:spiffe/client_spiffebundle.json",
        "//test/core/credentials/transport/tls/test_data:spiffe/test_bundles/spiffebundle2.json",
        "//test/core/credentials/transport/tls/test_data:spiffe/test_bundles/spiffebundle_malformed.json",
    ],
    external_deps = [
        "absl/log:check",
        "gtest",
    ],
    deps = [
        "//:gpr",
        "//:grpc",
        "//src/core:channel_args",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
        "//test/core/test_util:test_call_creds",
    ],
)

grpc_cc_test(
    name = "grpc_tls_credentials_options_test",
    srcs = ["grpc_tls_credentials_options_test.cc"],
    data = [
        "//src/core/tsi/test_creds:ca.pem",
        "//src/core/tsi/test_creds:multi-domain.key",
        "//src/core/tsi/test_creds:multi-domain.pem",
        "//src/core/tsi/test_creds:server0.key",
        "//src/core/tsi/test_creds:server0.pem",
        "//src/core/tsi/test_creds:server1.key",
        "//src/core/tsi/test_creds:server1.pem",
    ],
    external_deps = ["gtest"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
    ],
)

grpc_cc_test(
    name = "grpc_tls_credentials_options_comparator_test",
    srcs = ["grpc_tls_credentials_options_comparator_test.cc"],
    external_deps = ["gtest"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
    ],
)

grpc_cc_test(
    name = "grpc_tls_certificate_distributor_test",
    srcs = ["grpc_tls_certificate_distributor_test.cc"],
    data = [
        "//test/core/credentials/transport/tls/test_data:spiffe/client_spiffebundle.json",
        "//test/core/credentials/transport/tls/test_data:spiffe/test_bundles/spiffebundle.json",
    ],
    external_deps = [
        "absl/log:check",
        "gtest",
    ],
    deps = [
        "//:gpr",
        "//:grpc",
        "//src/core:match",
        "//src/core:slice",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
    ],
)

grpc_cc_test(
    name = "grpc_tls_certificate_provider_test",
    srcs = ["grpc_tls_certificate_provider_test.cc"],
    data = [
        "//src/core/tsi/test_creds:ca.pem",
        "//src/core/tsi/test_creds:malformed-cert.pem",
        "//src/core/tsi/test_creds:malformed-key.pem",
        "//src/core/tsi/test_creds:multi-domain.key",
        "//src/core/tsi/test_creds:multi-domain.pem",
        "//src/core/tsi/test_creds:server0.key",
        "//src/core/tsi/test_creds:server0.pem",
        "//src/core/tsi/test_creds:server1.key",
        "//src/core/tsi/test_creds:server1.pem",
        "//test/core/credentials/transport/tls/test_data:spiffe/client_spiffebundle.json",
        "//test/core/credentials/transport/tls/test_data:spiffe/test_bundles/spiffebundle2.json",
        "//test/core/credentials/transport/tls/test_data:spiffe/test_bundles/spiffebundle_malformed.json",
    ],
    external_deps = [
        "absl/log:check",
        "gtest",
    ],
    deps = [
        "//:gpr",
        "//:grpc",
        "//src/core:match",
        "//src/core:slice",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
    ],
)

grpc_cc_test(
    name = "grpc_tls_certificate_verifier_test",
    srcs = ["grpc_tls_certificate_verifier_test.cc"],
    external_deps = [
        "absl/log:log",
        "gtest",
    ],
    deps = [
        "//:gpr",
        "//:grpc",
        "//src/core:slice",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
    ],
)

grpc_cc_test(
    name = "certificate_provider_registry_test",
    srcs = ["certificate_provider_registry_test.cc"],
    external_deps = ["gtest"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "grpc_tls_crl_provider_test",
    srcs = ["grpc_tls_crl_provider_test.cc"],
    data = [
        "//test/core/tsi/test_creds/crl_data:ca.pem",
        "//test/core/tsi/test_creds/crl_data:intermediate_ca.pem",
        "//test/core/tsi/test_creds/crl_data/crls:ab06acdd.r0",
        "//test/core/tsi/test_creds/crl_data/crls:b9322cac.r0",
        "//test/core/tsi/test_creds/crl_data/crls:current.crl",
        "//test/core/tsi/test_creds/crl_data/crls:intermediate.crl",
    ],
    external_deps = ["gtest"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//:iomgr_timer",
        "//src/core:default_event_engine",
        "//src/core:grpc_crl_provider",
        "//test/core/event_engine:event_engine_test_utils",
        "//test/core/event_engine/fuzzing_event_engine",
        "//test/core/event_engine/fuzzing_event_engine:fuzzing_event_engine_cc_proto",
        "//test/core/test_util:fuzz_config_vars_cc_proto",
        "//test/core/test_util:grpc_test_util",
        "//test/core/tsi:transport_security_test_lib",
    ],
)

grpc_cc_test(
    name = "spiffe_utils_test",
    srcs = ["spiffe_utils_test.cc"],
    data = [
        "//test/core/credentials/transport/tls/test_data:spiffe/client_spiffebundle.json",
        "//test/core/credentials/transport/tls/test_data:spiffe/spiffe_cert.pem",
        "//test/core/credentials/transport/tls/test_data:spiffe/test_bundles/server1_spiffe.pem",
        "//test/core/credentials/transport/tls/test_data:spiffe/test_bundles/spiffe-openssl.cnf",
        "//test/core/credentials/transport/tls/test_data:spiffe/test_bundles/spiffe_cert.pem",
        "//test/core/credentials/transport/tls/test_data:spiffe/test_bundles/spiffe_multi_uri_san_cert.pem",
        "//test/core/credentials/transport/tls/test_data:spiffe/test_bundles/spiffe_test.json",
        "//test/core/credentials/transport/tls/test_data:spiffe/test_bundles/spiffebundle.json",
        "//test/core/credentials/transport/tls/test_data:spiffe/test_bundles/spiffebundle2.json",
        "//test/core/credentials/transport/tls/test_data:spiffe/test_bundles/spiffebundle_corrupted_cert.json",
        "//test/core/credentials/transport/tls/test_data:spiffe/test_bundles/spiffebundle_empty_keys.json",
        "//test/core/credentials/transport/tls/test_data:spiffe/test_bundles/spiffebundle_empty_string_key.json",
        "//test/core/credentials/transport/tls/test_data:spiffe/test_bundles/spiffebundle_invalid_trustdomain.json",
        "//test/core/credentials/transport/tls/test_data:spiffe/test_bundles/spiffebundle_malformed.json",
        "//test/core/credentials/transport/tls/test_data:spiffe/test_bundles/spiffebundle_match_client_spiffe.json",
        "//test/core/credentials/transport/tls/test_data:spiffe/test_bundles/spiffebundle_wrong_kid.json",
        "//test/core/credentials/transport/tls/test_data:spiffe/test_bundles/spiffebundle_wrong_kty.json",
        "//test/core/credentials/transport/tls/test_data:spiffe/test_bundles/spiffebundle_wrong_multi_certs.json",
        "//test/core/credentials/transport/tls/test_data:spiffe/test_bundles/spiffebundle_wrong_root.json",
        "//test/core/credentials/transport/tls/test_data:spiffe/test_bundles/spiffebundle_wrong_seq_type.json",
        "//test/core/credentials/transport/tls/test_data:spiffe/test_bundles/spiffebundle_wrong_use.json",
    ],
    external_deps = [
        "gtest",
    ],
    deps = [
        "//:gpr",
        "//:grpc",
        "//src/core:spiffe_utils",
        "//test/core/test_util:grpc_test_util",
    ],
)

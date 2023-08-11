# Copyright 2021 The gRPC Authors
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
"""Load dependencies needed to compile and test the grpc library as a 3rd-party consumer."""

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
load("@com_github_grpc_grpc//bazel:grpc_python_deps.bzl", "grpc_python_deps")

# buildifier: disable=unnamed-macro
def grpc_deps():
    """Loads dependencies need to compile and test the grpc library."""

    native.bind(
        name = "upb_lib",
        actual = "@upb//:upb",
    )

    native.bind(
        name = "upb_collections_lib",
        actual = "@upb//:collections",
    )

    native.bind(
        name = "upb_reflection",
        actual = "@upb//:reflection",
    )

    native.bind(
        name = "upb_lib_descriptor",
        actual = "@upb//:descriptor_upb_proto",
    )

    native.bind(
        name = "upb_lib_descriptor_reflection",
        actual = "@upb//:descriptor_upb_proto_reflection",
    )

    native.bind(
        name = "upb_textformat_lib",
        actual = "@upb//:textformat",
    )

    native.bind(
        name = "upb_json_lib",
        actual = "@upb//:json",
    )

    native.bind(
        name = "upb_generated_code_support__only_for_generated_code_do_not_use__i_give_permission_to_break_me",
        actual = "@upb//:generated_code_support__only_for_generated_code_do_not_use__i_give_permission_to_break_me",
    )

    native.bind(
        name = "libssl",
        actual = "@boringssl//:ssl",
    )

    native.bind(
        name = "libcrypto",
        actual = "@boringssl//:crypto",
    )

    native.bind(
        name = "madler_zlib",
        actual = "@zlib//:zlib",
    )

    native.bind(
        name = "protobuf",
        actual = "@com_google_protobuf//:protobuf",
    )

    native.bind(
        name = "protobuf_clib",
        actual = "@com_google_protobuf//:protoc_lib",
    )

    native.bind(
        name = "protobuf_headers",
        actual = "@com_google_protobuf//:protobuf_headers",
    )

    native.bind(
        name = "protocol_compiler",
        actual = "@com_google_protobuf//:protoc",
    )

    native.bind(
        name = "cares",
        actual = "@com_github_cares_cares//:ares",
    )

    native.bind(
        name = "gtest",
        actual = "@com_google_googletest//:gtest",
    )

    native.bind(
        name = "fuzztest",
        actual = "@com_google_fuzztest//fuzztest",
    )

    native.bind(
        name = "fuzztest_main",
        actual = "@com_google_fuzztest//fuzztest:fuzztest_gtest_main",
    )

    native.bind(
        name = "benchmark",
        actual = "@com_github_google_benchmark//:benchmark",
    )

    native.bind(
        name = "re2",
        actual = "@com_googlesource_code_re2//:re2",
    )

    native.bind(
        name = "grpc_cpp_plugin",
        actual = "@com_github_grpc_grpc//src/compiler:grpc_cpp_plugin",
    )

    native.bind(
        name = "grpc++_codegen_proto",
        actual = "@com_github_grpc_grpc//:grpc++_codegen_proto",
    )

    native.bind(
        name = "opencensus-context",
        actual = "@io_opencensus_cpp//opencensus/context:context",
    )

    native.bind(
        name = "opencensus-trace",
        actual = "@io_opencensus_cpp//opencensus/trace:trace",
    )

    native.bind(
        name = "opencensus-trace-context_util",
        actual = "@io_opencensus_cpp//opencensus/trace:context_util",
    )

    native.bind(
        name = "opencensus-trace-propagation",
        actual = "@io_opencensus_cpp//opencensus/trace:grpc_trace_bin",
    )

    native.bind(
        name = "opencensus-trace-span_context",
        actual = "@io_opencensus_cpp//opencensus/trace:span_context",
    )

    native.bind(
        name = "opencensus-stats",
        actual = "@io_opencensus_cpp//opencensus/stats:stats",
    )

    native.bind(
        name = "opencensus-stats-test",
        actual = "@io_opencensus_cpp//opencensus/stats:test_utils",
    )

    native.bind(
        name = "opencensus-with-tag-map",
        actual = "@io_opencensus_cpp//opencensus/tags:with_tag_map",
    )

    native.bind(
        name = "opencensus-tags",
        actual = "@io_opencensus_cpp//opencensus/tags:tags",
    )

    native.bind(
        name = "opencensus-tags-context_util",
        actual = "@io_opencensus_cpp//opencensus/tags:context_util",
    )

    native.bind(
        name = "opencensus-trace-stackdriver_exporter",
        actual = "@io_opencensus_cpp//opencensus/exporters/trace/stackdriver:stackdriver_exporter",
    )

    native.bind(
        name = "opencensus-stats-stackdriver_exporter",
        actual = "@io_opencensus_cpp//opencensus/exporters/stats/stackdriver:stackdriver_exporter",
    )

    native.bind(
        name = "googleapis_trace_grpc_service",
        actual = "@com_google_googleapis//google/devtools/cloudtrace/v2:cloudtrace_cc_grpc",
    )

    native.bind(
        name = "googleapis_monitoring_grpc_service",
        actual = "@com_google_googleapis//google/monitoring/v3:monitoring_cc_grpc",
    )

    native.bind(
        name = "googleapis_logging_grpc_service",
        actual = "@com_google_googleapis//google/logging/v2:logging_cc_grpc",
    )

    native.bind(
        name = "googleapis_logging_cc_proto",
        actual = "@com_google_googleapis//google/logging/v2:logging_cc_proto",
    )

    if "boringssl" not in native.existing_rules():
        http_archive(
            name = "boringssl",
            # Use github mirror instead of https://boringssl.googlesource.com/boringssl
            # to obtain a boringssl archive with consistent sha256
            sha256 = "0675a4f86ce5e959703425d6f9063eaadf6b61b7f3399e77a154c0e85bad46b1",
            strip_prefix = "boringssl-342e805bc1f5dfdd650e3f031686d6c939b095d9",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/google/boringssl/archive/342e805bc1f5dfdd650e3f031686d6c939b095d9.tar.gz",
                "https://github.com/google/boringssl/archive/342e805bc1f5dfdd650e3f031686d6c939b095d9.tar.gz",
            ],
        )

    if "zlib" not in native.existing_rules():
        http_archive(
            name = "zlib",
            build_file = "@com_github_grpc_grpc//third_party:zlib.BUILD",
            sha256 = "90f43a9c998740e8a0db24b0af0147033db2aaaa99423129abbd76640757cac9",
            strip_prefix = "zlib-04f42ceca40f73e2978b50e93806c2a18c1281fc",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/madler/zlib/archive/04f42ceca40f73e2978b50e93806c2a18c1281fc.tar.gz",
                "https://github.com/madler/zlib/archive/04f42ceca40f73e2978b50e93806c2a18c1281fc.tar.gz",
            ],
        )

    if "com_google_protobuf" not in native.existing_rules():
        http_archive(
            name = "com_google_protobuf",
            sha256 = "76a33e2136f23971ce46c72fd697cd94dc9f73d56ab23b753c3e16854c90ddfd",
            strip_prefix = "protobuf-2c5fa078d8e86e5f4bd34e6f4c9ea9e8d7d4d44a",
            urls = [
                # https://github.com/protocolbuffers/protobuf/commits/v23.4
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/protocolbuffers/protobuf/archive/2c5fa078d8e86e5f4bd34e6f4c9ea9e8d7d4d44a.tar.gz",
                "https://github.com/protocolbuffers/protobuf/archive/2c5fa078d8e86e5f4bd34e6f4c9ea9e8d7d4d44a.tar.gz",
            ],
            patches = [
                "@com_github_grpc_grpc//third_party:protobuf.patch",
            ],
            patch_args = ["-p1"],
        )

    if "com_google_googletest" not in native.existing_rules():
        http_archive(
            name = "com_google_googletest",
            sha256 = "c8de6c60e12ad014a28225c5247ee735861d85cf906df617f6a29954ca05f547",
            strip_prefix = "googletest-0e402173c97aea7a00749e825b194bfede4f2e45",
            urls = [
                # 2022-02-09
                "https://github.com/google/googletest/archive/0e402173c97aea7a00749e825b194bfede4f2e45.tar.gz",
            ],
        )

    if "com_google_fuzztest" not in native.existing_rules():
        # when updating this remember to run:
        # bazel run @com_google_fuzztest//bazel:setup_configs > tools/fuzztest.bazelrc
        http_archive(
            name = "com_google_fuzztest",
            sha256 = "cdf8d8cd3cdc77280a7c59b310edf234e489a96b6e727cb271e7dfbeb9bcca8d",
            strip_prefix = "fuzztest-4ecaeb5084a061a862af8f86789ee184cd3d3f18",
            urls = [
                # 2023-05-16
                "https://github.com/google/fuzztest/archive/4ecaeb5084a061a862af8f86789ee184cd3d3f18.tar.gz",
            ],
        )

    if "rules_cc" not in native.existing_rules():
        http_archive(
            name = "rules_cc",
            sha256 = "3d9e271e2876ba42e114c9b9bc51454e379cbf0ec9ef9d40e2ae4cec61a31b40",
            strip_prefix = "rules_cc-0.0.6",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/bazelbuild/rules_cc/releases/download/0.0.6/rules_cc-0.0.6.tar.gz",
                "https://github.com/bazelbuild/rules_cc/releases/download/0.0.6/rules_cc-0.0.6.tar.gz",
            ],
        )

    if "com_github_google_benchmark" not in native.existing_rules():
        http_archive(
            name = "com_github_google_benchmark",
            sha256 = "4e47ca279d5ae967c506c136bd8afb42eedcaf010aebb48a0e87790cae4b488a",
            strip_prefix = "benchmark-015d1a091af6937488242b70121858bce8fd40e9",
            urls = [
                # v1.8.2
                "https://github.com/google/benchmark/archive/015d1a091af6937488242b70121858bce8fd40e9.tar.gz",
            ],
        )

    if "com_googlesource_code_re2" not in native.existing_rules():
        http_archive(
            name = "com_googlesource_code_re2",
            sha256 = "1ae8ccfdb1066a731bba6ee0881baad5efd2cd661acd9569b689f2586e1a50e9",
            strip_prefix = "re2-2022-04-01",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/google/re2/archive/2022-04-01.tar.gz",
                "https://github.com/google/re2/archive/2022-04-01.tar.gz",
            ],
        )

    if "com_github_cares_cares" not in native.existing_rules():
        http_archive(
            name = "com_github_cares_cares",
            build_file = "@com_github_grpc_grpc//third_party:cares/cares.BUILD",
            sha256 = "bf26e5b25e259911914a85ae847b6d723488adb5af4f8bdeb9d0871a318476e3",
            strip_prefix = "c-ares-6360e96b5cf8e5980c887ce58ef727e53d77243a",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/c-ares/c-ares/archive/6360e96b5cf8e5980c887ce58ef727e53d77243a.tar.gz",
                "https://github.com/c-ares/c-ares/archive/6360e96b5cf8e5980c887ce58ef727e53d77243a.tar.gz",
            ],
        )

    if "com_google_absl" not in native.existing_rules():
        http_archive(
            name = "com_google_absl",
            sha256 = "5366d7e7fa7ba0d915014d387b66d0d002c03236448e1ba9ef98122c13b35c36",
            strip_prefix = "abseil-cpp-20230125.3",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/abseil/abseil-cpp/archive/20230125.3.tar.gz",
                "https://github.com/abseil/abseil-cpp/archive/20230125.3.tar.gz",
            ],
        )

    if "bazel_toolchains" not in native.existing_rules():
        # list of releases is at https://github.com/bazelbuild/bazel-toolchains/releases
        http_archive(
            name = "bazel_toolchains",
            sha256 = "179ec02f809e86abf56356d8898c8bd74069f1bd7c56044050c2cd3d79d0e024",
            strip_prefix = "bazel-toolchains-4.1.0",
            urls = [
                "https://mirror.bazel.build/github.com/bazelbuild/bazel-toolchains/releases/download/4.1.0/bazel-toolchains-4.1.0.tar.gz",
                "https://github.com/bazelbuild/bazel-toolchains/releases/download/4.1.0/bazel-toolchains-4.1.0.tar.gz",
            ],
        )

    if "bazel_skylib" not in native.existing_rules():
        http_archive(
            name = "bazel_skylib",
            urls = [
                "https://mirror.bazel.build/github.com/bazelbuild/bazel-skylib/releases/download/1.0.3/bazel-skylib-1.0.3.tar.gz",
                "https://github.com/bazelbuild/bazel-skylib/releases/download/1.0.3/bazel-skylib-1.0.3.tar.gz",
            ],
            sha256 = "1c531376ac7e5a180e0237938a2536de0c54d93f5c278634818e0efc952dd56c",
        )

    if "bazel_compdb" not in native.existing_rules():
        http_archive(
            name = "bazel_compdb",
            sha256 = "bcecfd622c4ef272fd4ba42726a52e140b961c4eac23025f18b346c968a8cfb4",
            strip_prefix = "bazel-compilation-database-0.4.5",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/grailbio/bazel-compilation-database/archive/0.4.5.tar.gz",
                "https://github.com/grailbio/bazel-compilation-database/archive/0.4.5.tar.gz",
            ],
        )

    if "io_opencensus_cpp" not in native.existing_rules():
        http_archive(
            name = "io_opencensus_cpp",
            sha256 = "46b3b5812c150a21bacf860c2f76fc42b89773ed77ee954c32adeb8593aa2a8e",
            strip_prefix = "opencensus-cpp-5501a1a255805e0be83a41348bb5f2630d5ed6b3",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/census-instrumentation/opencensus-cpp/archive/5501a1a255805e0be83a41348bb5f2630d5ed6b3.tar.gz",
                "https://github.com/census-instrumentation/opencensus-cpp/archive/5501a1a255805e0be83a41348bb5f2630d5ed6b3.tar.gz",
            ],
        )

    if "upb" not in native.existing_rules():
        http_archive(
            name = "upb",
            sha256 = "c29fbd26eb19f388a1894099fae5ae599210be22926e1e1694c0ed1284f1c151",
            strip_prefix = "upb-455cfdb8ae60a1763e6d924e36851c6897a781bb",
            urls = [
                # https://github.com/protocolbuffers/upb/commits/23.x
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/protocolbuffers/upb/archive/455cfdb8ae60a1763e6d924e36851c6897a781bb.tar.gz",
                "https://github.com/protocolbuffers/upb/archive/455cfdb8ae60a1763e6d924e36851c6897a781bb.tar.gz",
            ],
        )

    if "envoy_api" not in native.existing_rules():
        http_archive(
            name = "envoy_api",
            sha256 = "6fd3496c82919a433219733819a93b56699519a193126959e9c4fedc25e70663",
            strip_prefix = "data-plane-api-e53e7bbd012f81965f2e79848ad9a58ceb67201f",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/envoyproxy/data-plane-api/archive/e53e7bbd012f81965f2e79848ad9a58ceb67201f.tar.gz",
                "https://github.com/envoyproxy/data-plane-api/archive/e53e7bbd012f81965f2e79848ad9a58ceb67201f.tar.gz",
            ],
        )

    if "io_bazel_rules_go" not in native.existing_rules():
        http_archive(
            name = "io_bazel_rules_go",
            sha256 = "69de5c704a05ff37862f7e0f5534d4f479418afc21806c887db544a316f3cb6b",
            urls = [
                "https://mirror.bazel.build/github.com/bazelbuild/rules_go/releases/download/v0.27.0/rules_go-v0.27.0.tar.gz",
                "https://github.com/bazelbuild/rules_go/releases/download/v0.27.0/rules_go-v0.27.0.tar.gz",
            ],
        )

    if "build_bazel_rules_apple" not in native.existing_rules():
        http_archive(
            name = "build_bazel_rules_apple",
            sha256 = "f94e6dddf74739ef5cb30f000e13a2a613f6ebfa5e63588305a71fce8a8a9911",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/bazelbuild/rules_apple/releases/download/1.1.3/rules_apple.1.1.3.tar.gz",
                "https://github.com/bazelbuild/rules_apple/releases/download/1.1.3/rules_apple.1.1.3.tar.gz",
            ],
        )

    if "build_bazel_apple_support" not in native.existing_rules():
        http_archive(
            name = "build_bazel_apple_support",
            sha256 = "f4fdf5c9b42b92ea12f229b265d74bb8cedb8208ca7a445b383c9f866cf53392",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/bazelbuild/apple_support/releases/download/1.3.1/apple_support.1.3.1.tar.gz",
                "https://github.com/bazelbuild/apple_support/releases/download/1.3.1/apple_support.1.3.1.tar.gz",
            ],
        )

    if "com_google_googleapis" not in native.existing_rules():
        http_archive(
            name = "com_google_googleapis",
            sha256 = "5bb6b0253ccf64b53d6c7249625a7e3f6c3bc6402abd52d3778bfa48258703a0",
            strip_prefix = "googleapis-2f9af297c84c55c8b871ba4495e01ade42476c92",
            build_file = Label("//bazel:googleapis.BUILD"),
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/googleapis/googleapis/archive/2f9af297c84c55c8b871ba4495e01ade42476c92.tar.gz",
                "https://github.com/googleapis/googleapis/archive/2f9af297c84c55c8b871ba4495e01ade42476c92.tar.gz",
            ],
        )

    if "bazel_gazelle" not in native.existing_rules():
        http_archive(
            name = "bazel_gazelle",
            sha256 = "de69a09dc70417580aabf20a28619bb3ef60d038470c7cf8442fafcf627c21cb",
            urls = [
                "https://mirror.bazel.build/github.com/bazelbuild/bazel-gazelle/releases/download/v0.24.0/bazel-gazelle-v0.24.0.tar.gz",
                "https://github.com/bazelbuild/bazel-gazelle/releases/download/v0.24.0/bazel-gazelle-v0.24.0.tar.gz",
            ],
        )

    if "opencensus_proto" not in native.existing_rules():
        http_archive(
            name = "opencensus_proto",
            sha256 = "b7e13f0b4259e80c3070b583c2f39e53153085a6918718b1c710caf7037572b0",
            strip_prefix = "opencensus-proto-0.3.0/src",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/census-instrumentation/opencensus-proto/archive/v0.3.0.tar.gz",
                "https://github.com/census-instrumentation/opencensus-proto/archive/v0.3.0.tar.gz",
            ],
        )

    if "com_envoyproxy_protoc_gen_validate" not in native.existing_rules():
        http_archive(
            name = "com_envoyproxy_protoc_gen_validate",
            strip_prefix = "protoc-gen-validate-4694024279bdac52b77e22dc87808bd0fd732b69",
            sha256 = "1e490b98005664d149b379a9529a6aa05932b8a11b76b4cd86f3d22d76346f47",
            urls = [
                "https://github.com/envoyproxy/protoc-gen-validate/archive/4694024279bdac52b77e22dc87808bd0fd732b69.tar.gz",
            ],
            patches = ["@com_github_grpc_grpc//third_party:protoc-gen-validate.patch"],
            patch_args = ["-p1"],
        )

    if "com_github_cncf_udpa" not in native.existing_rules():
        http_archive(
            name = "com_github_cncf_udpa",
            sha256 = "0d33b83f8c6368954e72e7785539f0d272a8aba2f6e2e336ed15fd1514bc9899",
            strip_prefix = "xds-e9ce68804cb4e64cab5a52e3c8baf840d4ff87b7",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/cncf/xds/archive/e9ce68804cb4e64cab5a52e3c8baf840d4ff87b7.tar.gz",
                "https://github.com/cncf/xds/archive/e9ce68804cb4e64cab5a52e3c8baf840d4ff87b7.tar.gz",
            ],
        )

    if "io_opentelemetry_cpp" not in native.existing_rules():
        http_archive(
            name = "io_opentelemetry_cpp",
            sha256 = "668de24f81c8d36d75092ad9dcb02a97cd41473adbe72485ece05e336db48249",
            strip_prefix = "opentelemetry-cpp-1.9.1",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/open-telemetry/opentelemetry-cpp/archive/refs/tags/v1.9.1.tar.gz",
                "https://github.com/open-telemetry/opentelemetry-cpp/archive/refs/tags/v1.9.1.tar.gz",
            ],
        )

    if "google_cloud_cpp" not in native.existing_rules():
        http_archive(
            name = "google_cloud_cpp",
            sha256 = "371d01b03c7e2604d671b8fa1c86710abe3b524a78bc2705a6bb4de715696755",
            strip_prefix = "google-cloud-cpp-2.14.0",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/googleapis/google-cloud-cpp/archive/refs/tags/v2.14.0.tar.gz",
                "https://github.com/googleapis/google-cloud-cpp/archive/refs/tags/v2.14.0.tar.gz",
            ],
        )

    grpc_python_deps()

# TODO: move some dependencies from "grpc_deps" here?
# buildifier: disable=unnamed-macro
def grpc_test_only_deps():
    """Internal, not intended for use by packages that are consuming grpc.

    Loads dependencies that are only needed to run grpc library's tests.
    """
    native.bind(
        name = "twisted",
        actual = "@com_github_twisted_twisted//:twisted",
    )

    native.bind(
        name = "yaml",
        actual = "@com_github_yaml_pyyaml//:yaml",
    )

    if "com_github_twisted_twisted" not in native.existing_rules():
        http_archive(
            name = "com_github_twisted_twisted",
            sha256 = "ca17699d0d62eafc5c28daf2c7d0a18e62ae77b4137300b6c7d7868b39b06139",
            strip_prefix = "twisted-twisted-17.5.0",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/twisted/twisted/archive/twisted-17.5.0.zip",
                "https://github.com/twisted/twisted/archive/twisted-17.5.0.zip",
            ],
            build_file = "@com_github_grpc_grpc//third_party:twisted.BUILD",
        )

    if "com_github_yaml_pyyaml" not in native.existing_rules():
        http_archive(
            name = "com_github_yaml_pyyaml",
            sha256 = "e34d97db6d846f5e2ad51417fd646e7ce6a3a70726ccea2a857e0580a7155f39",
            strip_prefix = "pyyaml-6.0.1",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/yaml/pyyaml/archive/6.0.1.zip",
                "https://github.com/yaml/pyyaml/archive/6.0.1.zip",
            ],
            build_file = "@com_github_grpc_grpc//third_party:yaml.BUILD",
        )

    if "com_github_twisted_incremental" not in native.existing_rules():
        http_archive(
            name = "com_github_twisted_incremental",
            sha256 = "f0ca93359ee70243ff7fbf2d904a6291810bd88cb80ed4aca6fa77f318a41a36",
            strip_prefix = "incremental-incremental-17.5.0",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/twisted/incremental/archive/incremental-17.5.0.zip",
                "https://github.com/twisted/incremental/archive/incremental-17.5.0.zip",
            ],
            build_file = "@com_github_grpc_grpc//third_party:incremental.BUILD",
        )

    if "com_github_zopefoundation_zope_interface" not in native.existing_rules():
        http_archive(
            name = "com_github_zopefoundation_zope_interface",
            sha256 = "e9579fc6149294339897be3aa9ecd8a29217c0b013fe6f44fcdae00e3204198a",
            strip_prefix = "zope.interface-4.4.3",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/zopefoundation/zope.interface/archive/4.4.3.zip",
                "https://github.com/zopefoundation/zope.interface/archive/4.4.3.zip",
            ],
            build_file = "@com_github_grpc_grpc//third_party:zope_interface.BUILD",
        )

    if "com_github_twisted_constantly" not in native.existing_rules():
        http_archive(
            name = "com_github_twisted_constantly",
            sha256 = "2702cd322161a579d2c0dbf94af4e57712eedc7bd7bbbdc554a230544f7d346c",
            strip_prefix = "constantly-15.1.0",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/twisted/constantly/archive/15.1.0.zip",
                "https://github.com/twisted/constantly/archive/15.1.0.zip",
            ],
            build_file = "@com_github_grpc_grpc//third_party:constantly.BUILD",
        )

    if "com_google_libprotobuf_mutator" not in native.existing_rules():
        http_archive(
            name = "com_google_libprotobuf_mutator",
            sha256 = "11ab4c57b4051977d8fedb86dba5c9092e578bc293c47be146e0b0596b6a0bdc",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/google/libprotobuf-mutator/archive/c390388561be36f94a559a4aed7e2fe60470f60b.tar.gz",
                "https://github.com/google/libprotobuf-mutator/archive/c390388561be36f94a559a4aed7e2fe60470f60b.tar.gz",
            ],
            strip_prefix = "libprotobuf-mutator-c390388561be36f94a559a4aed7e2fe60470f60b",
            build_file = "@com_github_grpc_grpc//third_party:libprotobuf_mutator.BUILD",
        )

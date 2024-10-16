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
load("//bazel:grpc_python_deps.bzl", "grpc_python_deps")

# buildifier: disable=unnamed-macro
def grpc_deps():
    """Loads dependencies need to compile and test the grpc library."""

    if "platforms" not in native.existing_rules():
        http_archive(
            name = "platforms",
            sha256 = "218efe8ee736d26a3572663b374a253c012b716d8af0c07e842e82f238a0a7ee",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/bazelbuild/platforms/releases/download/0.0.10/platforms-0.0.10.tar.gz",
                "https://github.com/bazelbuild/platforms/releases/download/0.0.10/platforms-0.0.10.tar.gz",
            ],
        )

    if "boringssl" not in native.existing_rules():
        http_archive(
            name = "boringssl",
            # Use github mirror instead of https://boringssl.googlesource.com/boringssl
            # to obtain a boringssl archive with consistent sha256
            sha256 = "c70d519e4ee709b7a74410a5e3a937428b8198d793a3d771be3dd2086ae167c8",
            strip_prefix = "boringssl-b8b3e6e11166719a8ebfa43c0cde9ad7d57a84f6",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/google/boringssl/archive/b8b3e6e11166719a8ebfa43c0cde9ad7d57a84f6.tar.gz",
                "https://github.com/google/boringssl/archive/b8b3e6e11166719a8ebfa43c0cde9ad7d57a84f6.tar.gz",
            ],
        )

    if "zlib" not in native.existing_rules():
        http_archive(
            name = "zlib",
            build_file = "@com_github_grpc_grpc//third_party:zlib.BUILD",
            sha256 = "18337cdb32562003c39d9f7322b9a166ad4abfb2b909566428e11f96d2385586",
            strip_prefix = "zlib-09155eaa2f9270dc4ed1fa13e2b4b2613e6e4851",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/madler/zlib/archive/09155eaa2f9270dc4ed1fa13e2b4b2613e6e4851.tar.gz",
                "https://github.com/madler/zlib/archive/09155eaa2f9270dc4ed1fa13e2b4b2613e6e4851.tar.gz",
            ],
        )

    if "com_google_protobuf" not in native.existing_rules():
        http_archive(
            name = "com_google_protobuf",
            sha256 = "8766fce06ef41d79cb6037d04be37df3d5c493d9da92517a7243640d6389f03c",
            strip_prefix = "protobuf-10ef3f77683f77fb3c059bf47725c27b3ff41e63",
            urls = [
                # https://github.com/protocolbuffers/protobuf/commits/v28.1
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/protocolbuffers/protobuf/archive/10ef3f77683f77fb3c059bf47725c27b3ff41e63.tar.gz",
                "https://github.com/protocolbuffers/protobuf/archive/10ef3f77683f77fb3c059bf47725c27b3ff41e63.tar.gz",
            ],
            patches = [
                "@com_github_grpc_grpc//third_party:protobuf.patch",
            ],
            patch_args = ["-p1"],
        )

    if "com_google_googletest" not in native.existing_rules():
        http_archive(
            name = "com_google_googletest",
            sha256 = "31bf78bd91b96dd5e24fab3bb1d7f3f7453ccbaceec9afb86d6e4816a15ab109",
            strip_prefix = "googletest-2dd1c131950043a8ad5ab0d2dda0e0970596586a",
            urls = [
                # 2023-10-09
                "https://github.com/google/googletest/archive/2dd1c131950043a8ad5ab0d2dda0e0970596586a.tar.gz",
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
            sha256 = "2037875b9a4456dce4a79d112a8ae885bbc4aad968e6587dca6e64f3a0900cdf",
            strip_prefix = "rules_cc-0.0.9",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/bazelbuild/rules_cc/releases/download/0.0.9/rules_cc-0.0.9.tar.gz",
                "https://github.com/bazelbuild/rules_cc/releases/download/0.0.9/rules_cc-0.0.9.tar.gz",
            ],
        )

    if "com_github_google_benchmark" not in native.existing_rules():
        http_archive(
            name = "com_github_google_benchmark",
            sha256 = "8e7b955f04bc6984e4f14074d0d191474f76a6c8e849e04a9dced49bc975f2d4",
            strip_prefix = "benchmark-344117638c8ff7e239044fd0fa7085839fc03021",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/google/benchmark/archive/344117638c8ff7e239044fd0fa7085839fc03021.tar.gz",
                "https://github.com/google/benchmark/archive/344117638c8ff7e239044fd0fa7085839fc03021.tar.gz",
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
            sha256 = "f50e5ac311a81382da7fa75b97310e4b9006474f9560ac46f54a9967f07d4ae3",
            strip_prefix = "abseil-cpp-20240722.0",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/abseil/abseil-cpp/archive/20240722.0.tar.gz",
                "https://github.com/abseil/abseil-cpp/archive/20240722.0.tar.gz",
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
                "https://mirror.bazel.build/github.com/bazelbuild/bazel-skylib/releases/download/1.7.1/bazel-skylib-1.7.1.tar.gz",
                "https://github.com/bazelbuild/bazel-skylib/releases/download/1.7.1/bazel-skylib-1.7.1.tar.gz",
            ],
            sha256 = "bc283cdfcd526a52c3201279cda4bc298652efa898b10b4db0837dc51652756f",
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

    if "envoy_api" not in native.existing_rules():
        http_archive(
            name = "envoy_api",
            sha256 = "e525a6fb6e6ed3eef1eec6bef3da9b5708e471f0f9335a7604df14a4b386231e",
            strip_prefix = "data-plane-api-f8b75d1efa92bbf534596a013d9ca5873f79dd30",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/envoyproxy/data-plane-api/archive/f8b75d1efa92bbf534596a013d9ca5873f79dd30.tar.gz",
                "https://github.com/envoyproxy/data-plane-api/archive/f8b75d1efa92bbf534596a013d9ca5873f79dd30.tar.gz",
            ],
        )

    # TODO(roth): Need to override version of rules_proto, because
    # we're using a newer version of rules_go than the xDS protos are,
    # and that requires a newer version of rules_proto.  In turn, the
    # newer version of rules_proto requires a newer version of
    # bazel_features.  So these two entries can go away once the xDS
    # protos upgrade to the newer version of rules_go.
    if "bazel_features" not in native.existing_rules():
        http_archive(
            name = "bazel_features",
            sha256 = "5ac743bf5f05d88e84962e978811f2524df09602b789c92cf7ae2111ecdeda94",
            strip_prefix = "bazel_features-1.14.0",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/bazel-contrib/bazel_features/releases/download/v1.14.0/bazel_features-v1.14.0.tar.gz",
                "https://github.com/bazel-contrib/bazel_features/releases/download/v1.14.0/bazel_features-v1.14.0.tar.gz",
            ],
        )
    if "rules_proto" not in native.existing_rules():
        http_archive(
            name = "rules_proto",
            sha256 = "6fb6767d1bef535310547e03247f7518b03487740c11b6c6adb7952033fe1295",
            strip_prefix = "rules_proto-6.0.2",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/bazelbuild/rules_proto/archive/refs/tags/6.0.2.tar.gz",
                "https://github.com/bazelbuild/rules_proto/archive/refs/tags/6.0.2.tar.gz",
            ],
        )

    if "io_bazel_rules_go" not in native.existing_rules():
        http_archive(
            name = "io_bazel_rules_go",
            sha256 = "d93ef02f1e72c82d8bb3d5169519b36167b33cf68c252525e3b9d3d5dd143de7",
            urls = [
                "https://mirror.bazel.build/github.com/bazelbuild/rules_go/releases/download/v0.49.0/rules_go-v0.49.0.zip",
                "https://github.com/bazelbuild/rules_go/releases/download/v0.49.0/rules_go-v0.49.0.zip",
            ],
            patches = [
                "@com_github_grpc_grpc//bazel:rules_go.patch",
            ],
            patch_args = ["-p1"],
        )

    if "build_bazel_rules_apple" not in native.existing_rules():
        http_archive(
            name = "build_bazel_rules_apple",
            sha256 = "34c41bfb59cdaea29ac2df5a2fa79e5add609c71bb303b2ebb10985f93fa20e7",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/bazelbuild/rules_apple/releases/download/3.1.1/rules_apple.3.1.1.tar.gz",
                "https://github.com/bazelbuild/rules_apple/releases/download/3.1.1/rules_apple.3.1.1.tar.gz",
            ],
        )

    if "build_bazel_apple_support" not in native.existing_rules():
        http_archive(
            name = "build_bazel_apple_support",
            sha256 = "cf4d63f39c7ba9059f70e995bf5fe1019267d3f77379c2028561a5d7645ef67c",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/bazelbuild/apple_support/releases/download/1.11.1/apple_support.1.11.1.tar.gz",
                "https://github.com/bazelbuild/apple_support/releases/download/1.11.1/apple_support.1.11.1.tar.gz",
            ],
        )

    if "com_google_googleapis" not in native.existing_rules():
        http_archive(
            name = "com_google_googleapis",
            sha256 = "0513f0f40af63bd05dc789cacc334ab6cec27cc89db596557cb2dfe8919463e4",
            strip_prefix = "googleapis-fe8ba054ad4f7eca946c2d14a63c3f07c0b586a0",
            build_file = Label("//bazel:googleapis.BUILD"),
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/googleapis/googleapis/archive/fe8ba054ad4f7eca946c2d14a63c3f07c0b586a0.tar.gz",
                "https://github.com/googleapis/googleapis/archive/fe8ba054ad4f7eca946c2d14a63c3f07c0b586a0.tar.gz",
            ],
        )

    if "bazel_gazelle" not in native.existing_rules():
        http_archive(
            name = "bazel_gazelle",
            sha256 = "d76bf7a60fd8b050444090dfa2837a4eaf9829e1165618ee35dceca5cbdf58d5",
            urls = [
                "https://mirror.bazel.build/github.com/bazelbuild/bazel-gazelle/releases/download/v0.37.0/bazel-gazelle-v0.37.0.tar.gz",
                "https://github.com/bazelbuild/bazel-gazelle/releases/download/v0.37.0/bazel-gazelle-v0.37.0.tar.gz",
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
            sha256 = "9372f9ecde8fbadf83c8c7de3dbb49b11067aa26fb608c501106d0b4bf06c28f",
            strip_prefix = "protoc-gen-validate-1.0.4",
            urls = ["https://github.com/bufbuild/protoc-gen-validate/archive/refs/tags/v1.0.4.zip"],
        )

    if "com_github_cncf_xds" not in native.existing_rules():
        http_archive(
            name = "com_github_cncf_xds",
            sha256 = "dc305e20c9fa80822322271b50aa2ffa917bf4fd3973bcec52bfc28dc32c5927",
            strip_prefix = "xds-3a472e524827f72d1ad621c4983dd5af54c46776",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/cncf/xds/archive/3a472e524827f72d1ad621c4983dd5af54c46776.tar.gz",
                "https://github.com/cncf/xds/archive/3a472e524827f72d1ad621c4983dd5af54c46776.tar.gz",
            ],
        )

    # TODO(stanleycheung): remove this when prometheus-cpp AND
    #   opentelemetry-cpp cut a new release
    # This override is needed because this fix
    #   https://github.com/jupp0r/prometheus-cpp/pull/626
    #   has not been included in the latest prometheus-cpp release yet.
    # We also need opentelemetry-cpp to update their dependency on
    #   prometheus-cpp after that fix is released.
    # Without the fix, we cannot build the prometheus exporter with bazel 6
    if "com_github_jupp0r_prometheus_cpp" not in native.existing_rules():
        http_archive(
            name = "com_github_jupp0r_prometheus_cpp",
            sha256 = "cdf03ee63fcb1d9e113f7bd525e043c254729dddf19d80396489f5b92c83c18d",
            strip_prefix = "prometheus-cpp-b1234816facfdda29845c46696a02998a4af115a",
            urls = [
                "https://github.com/jupp0r/prometheus-cpp/archive/b123481.zip",
            ],
        )

    if "io_opentelemetry_cpp" not in native.existing_rules():
        http_archive(
            name = "io_opentelemetry_cpp",
            sha256 = "ed681d20a684b7d485a49288e7cfb2d182bf882e5c112c5f2fa3f9e9da2278fc",
            strip_prefix = "opentelemetry-cpp-4bd64c9a336fd438d6c4c9dad2e6b61b0585311f",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/open-telemetry/opentelemetry-cpp/archive/4bd64c9a336fd438d6c4c9dad2e6b61b0585311f.tar.gz",
                "https://github.com/open-telemetry/opentelemetry-cpp/archive/4bd64c9a336fd438d6c4c9dad2e6b61b0585311f.tar.gz",
            ],
        )

    if "google_cloud_cpp" not in native.existing_rules():
        http_archive(
            name = "google_cloud_cpp",
            sha256 = "7ca7f583b60d2aa1274411fed3b9fb3887119b2e84244bb3fc69ea1db819e4e5",
            strip_prefix = "google-cloud-cpp-2.16.0",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/googleapis/google-cloud-cpp/archive/refs/tags/v2.16.0.tar.gz",
                "https://github.com/googleapis/google-cloud-cpp/archive/refs/tags/v2.16.0.tar.gz",
            ],
        )

    grpc_python_deps()

# TODO: move some dependencies from "grpc_deps" here?
# buildifier: disable=unnamed-macro
def grpc_test_only_deps():
    """Internal, not intended for use by packages that are consuming grpc.

    Loads dependencies that are only needed to run grpc library's tests.
    """

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
            sha256 = "9c8f800aed088cdf89adc3eaaa66b56b4da7da041f26338aa71a2ab43d860d46",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/google/libprotobuf-mutator/archive/1f95f8083066f5b38fd2db172e7e7f9aa7c49d2d.tar.gz",
                "https://github.com/google/libprotobuf-mutator/archive/1f95f8083066f5b38fd2db172e7e7f9aa7c49d2d.tar.gz",
            ],
            strip_prefix = "libprotobuf-mutator-1f95f8083066f5b38fd2db172e7e7f9aa7c49d2d",
            build_file = "@com_github_grpc_grpc//third_party:libprotobuf_mutator.BUILD",
        )

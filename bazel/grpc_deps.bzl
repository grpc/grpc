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
            sha256 = "cea4c77cd69279585ea53efa2bb2ae6eb5f31fb39c214213c5b7cdf3c44d7b52",
            strip_prefix = "boringssl-c64b8fefbba9a9dadda73138062fc449bdf11e2a",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/google/boringssl/archive/c64b8fefbba9a9dadda73138062fc449bdf11e2a.tar.gz",
                "https://github.com/google/boringssl/archive/c64b8fefbba9a9dadda73138062fc449bdf11e2a.tar.gz",
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
            sha256 = "cf2db029202bb8eb1471b9bae387cc475d15d9e99c547e6906155033f81249a5",
            strip_prefix = "protobuf-2d4414f384dc499af113b5991ce3eaa9df6dd931",
            urls = [
                # https://github.com/protocolbuffers/protobuf/commits/v29.0
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/protocolbuffers/protobuf/archive/2d4414f384dc499af113b5991ce3eaa9df6dd931.tar.gz",
                "https://github.com/protocolbuffers/protobuf/archive/2d4414f384dc499af113b5991ce3eaa9df6dd931.tar.gz",
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
            sha256 = "abc605dd850f813bb37004b77db20106a19311a96b2da1c92b789da529d28fe1",
            strip_prefix = "rules_cc-0.0.17",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/bazelbuild/rules_cc/releases/download/0.0.17/rules_cc-0.0.17.tar.gz",
                "https://github.com/bazelbuild/rules_cc/releases/download/0.0.17/rules_cc-0.0.17.tar.gz",
            ],
        )

    if "com_github_google_benchmark" not in native.existing_rules():
        http_archive(
            name = "com_github_google_benchmark",
            sha256 = "11f344710a80fd73db0fc686b4fe40867dc34d914d9cdfd7a4b416a65d1e692f",
            strip_prefix = "benchmark-12235e24652fc7f809373e7c11a5f73c5763fc4c",
            urls = [
                # v1.9.0
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/google/benchmark/archive/12235e24652fc7f809373e7c11a5f73c5763fc4c.tar.gz",
                "https://github.com/google/benchmark/archive/12235e24652fc7f809373e7c11a5f73c5763fc4c.tar.gz",
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
            repo_mapping = {
                "@googletest": "@com_google_googletest",
            },
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
            sha256 = "79502264d1a3a4b6309d4dae8c822e7349bcfe33e84f3c6d1affb2a40d11a31d",
            strip_prefix = "bazel-compilation-database-d198303a4319092ab31895c4b98d64174ebe8872",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/grailbio/bazel-compilation-database/archive/d198303a4319092ab31895c4b98d64174ebe8872.tar.gz",
                "https://github.com/grailbio/bazel-compilation-database/archive/d198303a4319092ab31895c4b98d64174ebe8872.tar.gz",
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
            sha256 = "cd8b49614408b43bd45d90e3e98d69e24eea632ff42ac3bfb8bca68bc31e377f",
            strip_prefix = "data-plane-api-4de3c74cf21a9958c1cf26d8993c55c6e0d28b49",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/envoyproxy/data-plane-api/archive/4de3c74cf21a9958c1cf26d8993c55c6e0d28b49.tar.gz",
                "https://github.com/envoyproxy/data-plane-api/archive/4de3c74cf21a9958c1cf26d8993c55c6e0d28b49.tar.gz",
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
            sha256 = "0e5c64a2599a6e26c6a03d6162242d231ecc0de219534c38cb4402171def21e8",
            strip_prefix = "rules_proto-7.0.2",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/bazelbuild/rules_proto/archive/refs/tags/7.0.2.tar.gz",
                "https://github.com/bazelbuild/rules_proto/archive/refs/tags/7.0.2.tar.gz",
            ],
        )

    if "rules_shell" not in native.existing_rules():
        http_archive(
            name = "rules_shell",
            sha256 = "d8cd4a3a91fc1dc68d4c7d6b655f09def109f7186437e3f50a9b60ab436a0c53",
            strip_prefix = "rules_shell-0.3.0",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/bazelbuild/rules_shell/releases/download/v0.3.0/rules_shell-v0.3.0.tar.gz",
                "https://github.com/bazelbuild/rules_shell/releases/download/v0.3.0/rules_shell-v0.3.0.tar.gz",
            ],
        )

    if "rules_java" not in native.existing_rules():
        http_archive(
            name = "rules_java",
            sha256 = "5449ed36d61269579dd9f4b0e532cd131840f285b389b3795ae8b4d717387dd8",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/bazelbuild/rules_java/releases/download/8.7.0/rules_java-8.7.0.tar.gz",
                "https://github.com/bazelbuild/rules_java/releases/download/8.7.0/rules_java-8.7.0.tar.gz",
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
            sha256 = "86ff9c3a2c7bc308fef339bcd5b3819aa735215033886cc281eb63f10cd17976",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/bazelbuild/rules_apple/releases/download/3.16.0/rules_apple.3.16.0.tar.gz",
                "https://github.com/bazelbuild/rules_apple/releases/download/3.16.0/rules_apple.3.16.0.tar.gz",
            ],
        )

    if "build_bazel_apple_support" not in native.existing_rules():
        http_archive(
            name = "build_bazel_apple_support",
            sha256 = "b53f6491e742549f13866628ddffcc75d1f3b2d6987dc4f14a16b242113c890b",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/bazelbuild/apple_support/releases/download/1.17.1/apple_support.1.17.1.tar.gz",
                "https://github.com/bazelbuild/apple_support/releases/download/1.17.1/apple_support.1.17.1.tar.gz",
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

    if "io_opentelemetry_cpp" not in native.existing_rules():
        http_archive(
            name = "io_opentelemetry_cpp",
            sha256 = "a85a22521de7426c3e019d50e18d0af16e0391e0353923308eb168b3e624aea1",
            strip_prefix = "opentelemetry-cpp-ced79860f8c8a091a2eabfee6d47783f828a9b59",
            urls = [
                # v1.19.0
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/open-telemetry/opentelemetry-cpp/archive/ced79860f8c8a091a2eabfee6d47783f828a9b59.tar.gz",
                "https://github.com/open-telemetry/opentelemetry-cpp/archive/ced79860f8c8a091a2eabfee6d47783f828a9b59.tar.gz",
            ],
        )

    grpc_module_deps()

    grpc_python_deps()

# TODO: move some dependencies from "grpc_deps" here?
# buildifier: disable=unnamed-macro
def grpc_test_only_deps():
    """Internal, not intended for use by packages that are consuming grpc.

    Loads dependencies that are only needed to run grpc library's tests.
    """

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

def grpc_module_deps():
    if "google_cloud_cpp" not in native.existing_rules():
        http_archive(
            name = "google_cloud_cpp",
            sha256 = "81ea28cf9e5bb032d356b0187409f30b1035f8ea5b530675ea248c8a6c0070aa",
            strip_prefix = "google-cloud-cpp-2.35.0",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/googleapis/google-cloud-cpp/archive/refs/tags/v2.35.0.tar.gz",
                "https://github.com/googleapis/google-cloud-cpp/archive/refs/tags/v2.35.0.tar.gz",
            ],
        )

grpc_repo_deps_ext = module_extension(implementation = lambda ctx: grpc_module_deps())

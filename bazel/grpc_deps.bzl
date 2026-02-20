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
            integrity = "sha256-KXQuhydYCbXlmNwvBNhpYMx6VbMGfZciHJq7yZJr/w8=",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/bazelbuild/platforms/releases/download/0.0.11/platforms-0.0.11.tar.gz",
                "https://github.com/bazelbuild/platforms/releases/download/0.0.11/platforms-0.0.11.tar.gz",
            ],
        )

    if "boringssl" not in native.existing_rules():
        http_archive(
            name = "boringssl",
            # Use github mirror instead of https://boringssl.googlesource.com/boringssl
            # to obtain a boringssl archive with consistent sha256
            sha256 = "b717df72df0023933a729bac00fa422a222949cfa84eb8500f3a9af34441fb6e",
            strip_prefix = "boringssl-c63fadbde60a2224c22189d14c4001bbd2a3a629",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/google/boringssl/archive/c63fadbde60a2224c22189d14c4001bbd2a3a629.tar.gz",
                "https://github.com/google/boringssl/archive/c63fadbde60a2224c22189d14c4001bbd2a3a629.tar.gz",
            ],
        )

    if "zlib" not in native.existing_rules():
        http_archive(
            name = "zlib",
            patch_strip = 1,
            strip_prefix = "zlib-1.3.1",
            integrity = "sha256-mpOyt9/ax3zrpaVYpYDnRmfdb+3kWFuR7vtg8Dty3yM=",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz",
                "https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz",
            ],
            patches = [
                "@com_github_grpc_grpc//bazel/zlib:add_build_file.patch",
                "@com_github_grpc_grpc//bazel/zlib:fix_windows.patch",
            ],
        )

    if "com_google_protobuf" not in native.existing_rules():
        http_archive(
            name = "com_google_protobuf",
            sha256 = "d0e3a75876a81e1536028bb9cf9181382b198da4cc6fa6aef86879ef629ac807",
            strip_prefix = "protobuf-74211c0dfc2777318ab53c2cd2c317a2ef9012de",
            urls = [
                # https://github.com/protocolbuffers/protobuf/commits/v31.1
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/protocolbuffers/protobuf/archive/74211c0dfc2777318ab53c2cd2c317a2ef9012de.tar.gz",
                "https://github.com/protocolbuffers/protobuf/archive/74211c0dfc2777318ab53c2cd2c317a2ef9012de.tar.gz",
            ],
            patches = [
                "@com_github_grpc_grpc//third_party:protobuf.patch",
            ],
            patch_args = ["-p1"],
            repo_mapping = {
                "@abseil-cpp": "@com_google_absl",
                "@googletest": "@com_google_googletest",
            },
        )

    if "com_google_googletest" not in native.existing_rules():
        http_archive(
            name = "com_google_googletest",
            sha256 = "65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c",
            strip_prefix = "googletest-1.17.0",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/google/googletest/releases/download/v1.17.0/googletest-1.17.0.tar.gz",
                "https://github.com/google/googletest/releases/download/v1.17.0/googletest-1.17.0.tar.gz",
            ],
            repo_mapping = {
                "@abseil-cpp": "@com_google_absl",
                "@re2": "@com_googlesource_code_re2",
            },
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
            integrity = "sha256-cS13hosxUt1hjE1k+q3e/MWWX5D13m5t0dXdzQvoLUI=",
            strip_prefix = "rules_cc-0.1.1",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/bazelbuild/rules_cc/releases/download/0.1.1/rules_cc-0.1.1.tar.gz",
                "https://github.com/bazelbuild/rules_cc/releases/download/0.1.1/rules_cc-0.1.1.tar.gz",
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
            sha256 = "cd1968cd09d384a5c553c82bf53ba458e47e9bfcdc253a3627f1b36ea6f814e2",
            strip_prefix = "c-ares-d3a507e920e7af18a5efb7f9f1d8044ed4750013",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/c-ares/c-ares/archive/d3a507e920e7af18a5efb7f9f1d8044ed4750013.tar.gz",
                "https://github.com/c-ares/c-ares/archive/d3a507e920e7af18a5efb7f9f1d8044ed4750013.tar.gz",
            ],
        )

    if "com_google_absl" not in native.existing_rules():
        http_archive(
            name = "com_google_absl",
            sha256 = "9b7a064305e9fd94d124ffa6cc358592eb42b5da588fb4e07d09254aa40086db",
            strip_prefix = "abseil-cpp-20250512.1",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/abseil/abseil-cpp/archive/refs/tags/20250512.1.tar.gz",
                "https://github.com/abseil/abseil-cpp/archive/refs/tags/20250512.1.tar.gz",
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
            integrity = "sha256-UbUQWnYLNTdz+QTSu8XmZNCYf7ryImUWTeZdQ+kQ2Kw=",
            urls = [
                "https://mirror.bazel.build/github.com/bazelbuild/bazel-skylib/releases/download/1.8.1/bazel-skylib-1.8.1.tar.gz",
                "https://github.com/bazelbuild/bazel-skylib/releases/download/1.8.1/bazel-skylib-1.8.1.tar.gz",
            ],
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
            patches = [
                "@com_github_grpc_grpc//third_party:opencensus-cpp.patch",
            ],
            patch_args = ["-p1"],
        )

    if "envoy_api" not in native.existing_rules():
        http_archive(
            name = "envoy_api",
            sha256 = "ed5e6c319f8ebcdf24a9491f866a599bb9a3c193b859a94ad13bd31f85b46855",
            strip_prefix = "data-plane-api-6ef568cf4a67362849911d1d2a546fd9f35db2ff",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/envoyproxy/data-plane-api/archive/6ef568cf4a67362849911d1d2a546fd9f35db2ff.tar.gz",
                "https://github.com/envoyproxy/data-plane-api/archive/6ef568cf4a67362849911d1d2a546fd9f35db2ff.tar.gz",
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
            sha256 = "3de3a199400eea7a766091aeb96c4b84c86266ad1f933f9933bbb7c359e727fe",
            strip_prefix = "googleapis-2193a2bfcecb92b92aad7a4d81baa428cafd7dfd",
            build_file = Label("//bazel:googleapis.BUILD"),
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/googleapis/googleapis/archive/2193a2bfcecb92b92aad7a4d81baa428cafd7dfd.tar.gz",
                "https://github.com/googleapis/googleapis/archive/2193a2bfcecb92b92aad7a4d81baa428cafd7dfd.tar.gz",
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
            sha256 = "ab51e978326b87e06be7a12fc6496f3ff6586339043557dbbd31f622332a5d45",
            strip_prefix = "protoc-gen-validate-1.2.1",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/bufbuild/protoc-gen-validate/archive/refs/tags/v1.2.1.zip",
                "https://github.com/bufbuild/protoc-gen-validate/archive/refs/tags/v1.2.1.zip",
            ],
        )

    if "com_github_cncf_xds" not in native.existing_rules():
        http_archive(
            name = "com_github_cncf_xds",
            sha256 = "49535f3c3370004309da50194c09bbfc528d4702424dd46e7d56a278a3dfc15d",
            strip_prefix = "xds-ee656c7534f5d7dc23d44dd611689568f72017a6",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/cncf/xds/archive/ee656c7534f5d7dc23d44dd611689568f72017a6.tar.gz",
                "https://github.com/cncf/xds/archive/ee656c7534f5d7dc23d44dd611689568f72017a6.tar.gz",
            ],
        )

    if "dev_cel" not in native.existing_rules():
        http_archive(
            name = "dev_cel",
            sha256 = "d6cb6b4ed272500d16546c672a65a7452b241462a200dda3f62a7de413883344",
            strip_prefix = "cel-spec-9f069b3ee58b02d6f6736c5ebd6587075c1a1b22",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/google/cel-spec/archive/9f069b3ee58b02d6f6736c5ebd6587075c1a1b22.tar.gz",
                "https://github.com/google/cel-spec/archive/9f069b3ee58b02d6f6736c5ebd6587075c1a1b22.tar.gz",
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

    # Building grpc with openssl is only supported when using bzlmod. Workspaces
    # are deprecated, so just create a dummy repo so that the grpc targets build
    # when using workspaces.
    openssl = repository_rule(
        implementation = _openssl_impl,
    )
    openssl(name = "openssl")

    grpc_module_deps()

    grpc_python_deps()

def _openssl_impl(ctx):
    ctx.file("BUILD", """
package(default_visibility = ["//visibility:public"])

cc_library(name = "ssl")

cc_library(name = "crypto")
""")

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

    if "yaml-cpp" not in native.existing_rules():
        http_archive(
            name = "yaml-cpp",
            integrity = "sha256-KYWT2cRA/ZA0uLGT2WMYt21JvJfGzq23sINu3wttdTk=",
            strip_prefix = "",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/jbeder/yaml-cpp/releases/download/yaml-cpp-0.9.0/yaml-cpp-yaml-cpp-0.9.0.tar.gz",
                "https://github.com/jbeder/yaml-cpp/releases/download/yaml-cpp-0.9.0/yaml-cpp-yaml-cpp-0.9.0.tar.gz",
            ],
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

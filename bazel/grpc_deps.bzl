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

def grpc_deps():
    """Loads dependencies need to compile and test the grpc library."""

    native.bind(
        name = "upb_lib",
        actual = "@upb//:upb",
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
        name = "absl",
        actual = "@com_google_absl//absl",
    )

    native.bind(
        name = "absl-base",
        actual = "@com_google_absl//absl/base",
    )

    native.bind(
        name = "absl-time",
        actual = "@com_google_absl//absl/time:time",
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
        name = "libuv",
        actual = "@libuv//:libuv",
    )

    if "boringssl" not in native.existing_rules():
        http_archive(
            name = "boringssl",
            # Use github mirror instead of https://boringssl.googlesource.com/boringssl
            # to obtain a boringssl archive with consistent sha256
            sha256 = "bf070ffab0ea1a2d0c3d024d8ab8c813c96a5d07a890a9e7e9a979e9ad427b47",
            strip_prefix = "boringssl-95b3ed1b01f2ef1d72fed290ed79fe1b0e7dafc0",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/google/boringssl/archive/95b3ed1b01f2ef1d72fed290ed79fe1b0e7dafc0.tar.gz",
                "https://github.com/google/boringssl/archive/95b3ed1b01f2ef1d72fed290ed79fe1b0e7dafc0.tar.gz",
            ],
        )

    if "zlib" not in native.existing_rules():
        http_archive(
            name = "zlib",
            build_file = "@com_github_grpc_grpc//third_party:zlib.BUILD",
            sha256 = "6d4d6640ca3121620995ee255945161821218752b551a1a180f4215f7d124d45",
            strip_prefix = "zlib-cacf7f1d4e3d44d871b605da3b647f07d718623f",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/madler/zlib/archive/cacf7f1d4e3d44d871b605da3b647f07d718623f.tar.gz",
                "https://github.com/madler/zlib/archive/cacf7f1d4e3d44d871b605da3b647f07d718623f.tar.gz",
            ],
        )

    if "com_google_protobuf" not in native.existing_rules():
        http_archive(
            name = "com_google_protobuf",
            sha256 = "dd53cb731b1b6b515d3d679644344bf33bb9f5b88c98187ae5c7be734a665c57",
            strip_prefix = "protobuf-909a0f36a10075c4b4bc70fdee2c7e32dd612a72",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/protocolbuffers/protobuf/archive/909a0f36a10075c4b4bc70fdee2c7e32dd612a72.tar.gz",
                "https://github.com/protocolbuffers/protobuf/archive/909a0f36a10075c4b4bc70fdee2c7e32dd612a72.tar.gz",
            ],
            patches = ["@com_github_grpc_grpc//third_party:protobuf.patch"],
            patch_args = ["-p1"],
        )

    if "com_google_googletest" not in native.existing_rules():
        http_archive(
            name = "com_google_googletest",
            sha256 = "443d383db648ebb8e391382c0ab63263b7091d03197f304390baac10f178a468",
            strip_prefix = "googletest-c9ccac7cb7345901884aabf5d1a786cfa6e2f397",
            urls = [
                # 2019-08-19
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/google/googletest/archive/c9ccac7cb7345901884aabf5d1a786cfa6e2f397.tar.gz",
                "https://github.com/google/googletest/archive/c9ccac7cb7345901884aabf5d1a786cfa6e2f397.tar.gz",
            ],
        )

    if "rules_cc" not in native.existing_rules():
        http_archive(
            name = "rules_cc",
            sha256 = "35f2fb4ea0b3e61ad64a369de284e4fbbdcdba71836a5555abb5e194cf119509",
            strip_prefix = "rules_cc-624b5d59dfb45672d4239422fa1e3de1822ee110",
            urls = [
                #2019-08-15
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/bazelbuild/rules_cc/archive/624b5d59dfb45672d4239422fa1e3de1822ee110.tar.gz",
                "https://github.com/bazelbuild/rules_cc/archive/624b5d59dfb45672d4239422fa1e3de1822ee110.tar.gz",
            ],
        )

    if "com_github_google_benchmark" not in native.existing_rules():
        http_archive(
            name = "com_github_google_benchmark",
            sha256 = "daa4a97e0547d76de300e325a49177b199f3689ce5a35e25d47696f7cb050f86",
            strip_prefix = "benchmark-73d4d5e8d6d449fc8663765a42aa8aeeee844489",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/google/benchmark/archive/73d4d5e8d6d449fc8663765a42aa8aeeee844489.tar.gz",
                "https://github.com/google/benchmark/archive/73d4d5e8d6d449fc8663765a42aa8aeeee844489.tar.gz",
            ],
        )

    if "com_googlesource_code_re2" not in native.existing_rules():
        http_archive(
            name = "com_googlesource_code_re2",
            sha256 = "319a58a58d8af295db97dfeecc4e250179c5966beaa2d842a82f0a013b6a239b",
            # Release 2021-09-01
            strip_prefix = "re2-8e08f47b11b413302749c0d8b17a1c94777495d5",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/google/re2/archive/8e08f47b11b413302749c0d8b17a1c94777495d5.tar.gz",
                "https://github.com/google/re2/archive/8e08f47b11b413302749c0d8b17a1c94777495d5.tar.gz",
            ],
        )

    if "com_github_cares_cares" not in native.existing_rules():
        http_archive(
            name = "com_github_cares_cares",
            build_file = "@com_github_grpc_grpc//third_party:cares/cares.BUILD",
            sha256 = "e8c2751ddc70fed9dc6f999acd92e232d5846f009ee1674f8aee81f19b2b915a",
            strip_prefix = "c-ares-e982924acee7f7313b4baa4ee5ec000c5e373c30",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/c-ares/c-ares/archive/e982924acee7f7313b4baa4ee5ec000c5e373c30.tar.gz",
                "https://github.com/c-ares/c-ares/archive/e982924acee7f7313b4baa4ee5ec000c5e373c30.tar.gz",
            ],
        )

    if "com_google_absl" not in native.existing_rules():
        http_archive(
            name = "com_google_absl",
            sha256 = "35f22ef5cb286f09954b7cc4c85b5a3f6221c9d4df6b8c4a1e9d399555b366ee",
            strip_prefix = "abseil-cpp-997aaf3a28308eba1b9156aa35ab7bca9688e9f6",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/abseil/abseil-cpp/archive/997aaf3a28308eba1b9156aa35ab7bca9688e9f6.tar.gz",
                "https://github.com/abseil/abseil-cpp/archive/997aaf3a28308eba1b9156aa35ab7bca9688e9f6.tar.gz",
            ],
        )

    if "bazel_toolchains" not in native.existing_rules():
        # list of releases is at https://releases.bazel.build/bazel-toolchains.html
        http_archive(
            name = "bazel_toolchains",
            sha256 = "179ec02f809e86abf56356d8898c8bd74069f1bd7c56044050c2cd3d79d0e024",
            strip_prefix = "bazel-toolchains-4.1.0",
            urls = [
                "https://mirror.bazel.build/github.com/bazelbuild/bazel-toolchains/archive/4.1.0.tar.gz",
                "https://github.com/bazelbuild/bazel-toolchains/releases/download/4.1.0/bazel-toolchains-4.1.0.tar.gz",
            ],
        )

    if "bazel_skylib" not in native.existing_rules():
        http_archive(
            name = "bazel_skylib",
            urls = [
                "https://mirror.bazel.build/github.com/bazelbuild/bazel-skylib/releases/download/1.0.2/bazel-skylib-1.0.2.tar.gz",
                "https://github.com/bazelbuild/bazel-skylib/releases/download/1.0.2/bazel-skylib-1.0.2.tar.gz",
            ],
            sha256 = "97e70364e9249702246c0e9444bccdc4b847bed1eb03c5a3ece4f83dfe6abc44",
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
            sha256 = "90d6fafa8b1a2ea613bf662731d3086e1c2ed286f458a95c81744df2dbae41b1",
            strip_prefix = "opencensus-cpp-c9a4da319bc669a772928ffc55af4a61be1a1176",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/census-instrumentation/opencensus-cpp/archive/c9a4da319bc669a772928ffc55af4a61be1a1176.tar.gz",
                "https://github.com/census-instrumentation/opencensus-cpp/archive/c9a4da319bc669a772928ffc55af4a61be1a1176.tar.gz",
            ],
        )

    if "upb" not in native.existing_rules():
        http_archive(
            name = "upb",
            sha256 = "6a5f67874af66b239b709c572ac1a5a00fdb1b29beaf13c3e6f79b1ba10dc7c4",
            strip_prefix = "upb-2de300726a1ba2de9a468468dc5ff9ed17a3215f",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/protocolbuffers/upb/archive/2de300726a1ba2de9a468468dc5ff9ed17a3215f.tar.gz",
                "https://github.com/protocolbuffers/upb/archive/2de300726a1ba2de9a468468dc5ff9ed17a3215f.tar.gz",
            ],
        )

    if "envoy_api" not in native.existing_rules():
        http_archive(
            name = "envoy_api",
            sha256 = "e89d4dddbadf797dd2700ce45ee8abc82557a934a15fcad82673e7d13213b868",
            strip_prefix = "data-plane-api-20b1b5fcee88a20a08b71051a961181839ec7268",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/envoyproxy/data-plane-api/archive/20b1b5fcee88a20a08b71051a961181839ec7268.tar.gz",
                "https://github.com/envoyproxy/data-plane-api/archive/20b1b5fcee88a20a08b71051a961181839ec7268.tar.gz",
            ],
        )

    if "io_bazel_rules_go" not in native.existing_rules():
        http_archive(
            name = "io_bazel_rules_go",
            sha256 = "dbf5a9ef855684f84cac2e7ae7886c5a001d4f66ae23f6904da0faaaef0d61fc",
            urls = [
                "https://mirror.bazel.build/github.com/bazelbuild/rules_go/releases/download/v0.24.11/rules_go-v0.24.11.tar.gz",
                "https://github.com/bazelbuild/rules_go/releases/download/v0.24.11/rules_go-v0.24.11.tar.gz",
            ],
        )

    if "build_bazel_rules_apple" not in native.existing_rules():
        http_archive(
            name = "build_bazel_rules_apple",
            sha256 = "0052d452af7742c8f3a4e0929763388a66403de363775db7e90adecb2ba4944b",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/bazelbuild/rules_apple/releases/download/0.31.3/rules_apple.0.31.3.tar.gz",
                "https://github.com/bazelbuild/rules_apple/releases/download/0.31.3/rules_apple.0.31.3.tar.gz",
            ],
        )

    if "build_bazel_apple_support" not in native.existing_rules():
        http_archive(
            name = "build_bazel_apple_support",
            sha256 = "76df040ade90836ff5543888d64616e7ba6c3a7b33b916aa3a4b68f342d1b447",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/bazelbuild/apple_support/releases/download/0.11.0/apple_support.0.11.0.tar.gz",
                "https://github.com/bazelbuild/apple_support/releases/download/0.11.0/apple_support.0.11.0.tar.gz",
            ],
        )

    if "libuv" not in native.existing_rules():
        http_archive(
            name = "libuv",
            build_file = "@com_github_grpc_grpc//third_party:libuv.BUILD",
            sha256 = "5ca4e9091f3231d8ad8801862dc4e851c23af89c69141d27723157776f7291e7",
            strip_prefix = "libuv-02a9e1be252b623ee032a3137c0b0c94afbe6809",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/libuv/libuv/archive/02a9e1be252b623ee032a3137c0b0c94afbe6809.tar.gz",
                "https://github.com/libuv/libuv/archive/02a9e1be252b623ee032a3137c0b0c94afbe6809.tar.gz",
            ],
        )

    if "com_google_googleapis" not in native.existing_rules():
        http_archive(
            name = "com_google_googleapis",
            sha256 = "5bb6b0253ccf64b53d6c7249625a7e3f6c3bc6402abd52d3778bfa48258703a0",
            strip_prefix = "googleapis-2f9af297c84c55c8b871ba4495e01ade42476c92",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/googleapis/googleapis/archive/2f9af297c84c55c8b871ba4495e01ade42476c92.tar.gz",
                "https://github.com/googleapis/googleapis/archive/2f9af297c84c55c8b871ba4495e01ade42476c92.tar.gz",
            ],
        )

    if "bazel_gazelle" not in native.existing_rules():
        http_archive(
            name = "bazel_gazelle",
            sha256 = "d987004a72697334a095bbaa18d615804a28280201a50ed6c234c40ccc41e493",
            strip_prefix = "bazel-gazelle-0.19.1",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/bazelbuild/bazel-gazelle/archive/v0.19.1.tar.gz",
                "https://github.com/bazelbuild/bazel-gazelle/archive/v0.19.1.tar.gz",
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
            sha256 = "dd4962e4a9e8388a4fbc5c33e64d73bdb222f103e4bad40ca5535f81c2c606c2",
            strip_prefix = "protoc-gen-validate-59da36e59fef2267fc2b1849a05159e3ecdf24f3",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/envoyproxy/protoc-gen-validate/archive/59da36e59fef2267fc2b1849a05159e3ecdf24f3.tar.gz",
                "https://github.com/envoyproxy/protoc-gen-validate/archive/59da36e59fef2267fc2b1849a05159e3ecdf24f3.tar.gz",
            ],
        )

    grpc_python_deps()

# TODO: move some dependencies from "grpc_deps" here?
def grpc_test_only_deps():
    """Internal, not intended for use by packages that are consuming grpc.
       Loads dependencies that are only needed to run grpc library's tests."""
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
            sha256 = "6b4314b1b2051ddb9d4fcd1634e1fa9c1bb4012954273c9ff3ef689f6ec6c93e",
            strip_prefix = "pyyaml-3.12",
            urls = [
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/yaml/pyyaml/archive/3.12.zip",
                "https://github.com/yaml/pyyaml/archive/3.12.zip",
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
            sha256 = "b847c71723d8ce0b747aa661d7f3a07f1d16c595bf9c0202f30febc2f9a24a06",
            urls = [
                "https://github.com/google/libprotobuf-mutator/archive/ffd86a32874e5c08a143019aad1aaf0907294c9f.tar.gz",
                "https://storage.googleapis.com/grpc-bazel-mirror/github.com/google/libprotobuf-mutator/archive/ffd86a32874e5c08a143019aad1aaf0907294c9f.tar.gz",
            ],
            strip_prefix = "libprotobuf-mutator-ffd86a32874e5c08a143019aad1aaf0907294c9f",
            build_file = "@com_github_grpc_grpc//third_party:libprotobuf_mutator.BUILD",
        )

workspace(name = "com_github_grpc_grpc")

load("//bazel:grpc_deps.bzl", "grpc_deps", "grpc_test_only_deps")

grpc_deps()

grpc_test_only_deps()

load("//bazel:grpc_extra_deps.bzl", "grpc_extra_deps")

grpc_extra_deps()

load("@bazel_toolchains//rules/exec_properties:exec_properties.bzl", "create_rbe_exec_properties_dict", "custom_exec_properties")

custom_exec_properties(
    name = "grpc_custom_exec_properties",
    constants = {
        "LARGE_MACHINE": create_rbe_exec_properties_dict(
            labels = {
                "os": "ubuntu",
                "machine_size": "large",
            },
        ),
    },
)

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

http_archive(
    name = "platforms",
    sha256 = "8150406605389ececb6da07cbcb509d5637a3ab9a24bc69b1101531367d89d74",
    urls = ["https://github.com/bazelbuild/platforms/releases/download/0.0.8/platforms-0.0.8.tar.gz"],
)

RULES_ANDROID_NDK_COMMIT = "010f4f17dd13a8baaaacc28ba6c8c2c75f54c68b"

RULES_ANDROID_NDK_SHA = "2ab6a97748772f289331d75caaaee0593825935d1d9d982231a437fb8ab5a14d"

http_archive(
    name = "rules_android_ndk",
    sha256 = RULES_ANDROID_NDK_SHA,
    strip_prefix = "rules_android_ndk-%s" % RULES_ANDROID_NDK_COMMIT,
    url = "https://github.com/bazelbuild/rules_android_ndk/archive/%s.zip" % RULES_ANDROID_NDK_COMMIT,
)

android_sdk_repository(
    name = "androidsdk",
    build_tools_version = "34.0.0",
)

load("@rules_android_ndk//:rules.bzl", "android_ndk_repository")

android_ndk_repository(name = "androidndk")

# Note that we intentionally avoid calling `register_toolchains("@androidndk//:all")`
# here, because the toolchain rule fails when $ANDROID_NDK_HOME is not set.
# Use `--extra_toolchains=@androidndk//:all` to manually register it when building for Android.

# Prevents bazel's '...' expansion from including the following folder.
# This is required because the BUILD file in the following folder
# will trigger bazel failure when Android SDK is not configured.
# The targets in the following folder need to be included in APK and will
# be invoked by binder transport implementation through JNI.
local_repository(
    name = "binder_transport_android_helper",
    path = "src/core/ext/transport/binder/java",
)

# Prevents bazel's '...' expansion from including the following folder.
# This is required to avoid triggering "Unable to find package for @rules_fuzzing//fuzzing:cc_defs.bzl"
# error.
local_repository(
    name = "ignore_third_party_utf8_range_subtree",
    path = "third_party/utf8_range",
)

load("@rules_python//python:pip.bzl", "pip_parse")

pip_parse(
    name = "grpc_python_dependencies",
    requirements_lock = "@com_github_grpc_grpc//:requirements.bazel.txt",
)

load("@grpc_python_dependencies//:requirements.bzl", "install_deps")

install_deps()

load("@com_google_protobuf//bazel:system_python.bzl", "system_python")

system_python(
    name = "system_python",
    minimum_python_version = "3.7",
)

load("@system_python//:pip.bzl", system_pip_parse = "pip_parse")

system_pip_parse(
    name = "pip_deps",
    requirements = "@com_google_protobuf//python:requirements.txt",
    requirements_overrides = {
        "3.11": "@com_google_protobuf//python:requirements_311.txt",
    },
)

http_archive(
    name = "build_bazel_rules_swift",
    sha256 = "bf2861de6bf75115288468f340b0c4609cc99cc1ccc7668f0f71adfd853eedb3",
    url = "https://github.com/bazelbuild/rules_swift/releases/download/1.7.1/rules_swift.1.7.1.tar.gz",
)

load(
    "@build_bazel_apple_support//lib:repositories.bzl",
    "apple_support_dependencies",
)

apple_support_dependencies()

load(
    "@build_bazel_rules_swift//swift:repositories.bzl",
    "swift_rules_dependencies",
)

swift_rules_dependencies()

# This loads the libpfm transitive dependency.
# See https://github.com/google/benchmark/pull/1520
load("@com_github_google_benchmark//:bazel/benchmark_deps.bzl", "benchmark_deps")

benchmark_deps()

load("@io_opentelemetry_cpp//bazel:repository.bzl", "opentelemetry_cpp_deps")

opentelemetry_cpp_deps()

load("@io_opentelemetry_cpp//bazel:extra_deps.bzl", "opentelemetry_extra_deps")

opentelemetry_extra_deps()

# TODO: Enable below once https://github.com/bazel-xcode/PodToBUILD/issues/232 is resolved
#
#http_archive(
#    name = "rules_pods",
#    urls = ["https://github.com/pinterest/PodToBUILD/releases/download/4.1.0-412495/PodToBUILD.zip"],
#)
#
#load(
#    "@rules_pods//BazelExtensions:workspace.bzl",
#    "new_pod_repository",
#)
#
#new_pod_repository(
#    name = "CronetFramework",
#    is_dynamic_framework = True,
#    podspec_url = "https://raw.githubusercontent.com/CocoaPods/Specs/master/Specs/2/e/1/CronetFramework/0.0.5/CronetFramework.podspec.json",
#    url = "https://storage.googleapis.com/grpc-precompiled-binaries/cronet/Cronet.framework-v0.0.5.zip",
#)

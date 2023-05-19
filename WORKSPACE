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
    name = "build_bazel_rules_android",
    sha256 = "cd06d15dd8bb59926e4d65f9003bfc20f9da4b2519985c27e190cddc8b7a7806",
    strip_prefix = "rules_android-0.1.1",
    urls = ["https://github.com/bazelbuild/rules_android/archive/v0.1.1.zip"],
)

load("//third_party/android:android_configure.bzl", "android_configure")

android_configure(name = "local_config_android")

load("@local_config_android//:android_configure.bzl", "android_workspace")

android_workspace()

# Prevents bazel's '...' expansion from including the following folder.
# This is required because the BUILD file in the following folder
# will trigger bazel failure when Android SDK is not configured.
# The targets in the following folder need to be included in APK and will
# be invoked by binder transport implementation through JNI.
local_repository(
    name = "binder_transport_android_helper",
    path = "./src/core/ext/transport/binder/java",
)

# Prevents bazel's '...' expansion from including the following folder.
# This is required to avoid triggering "Unable to find package for @rules_fuzzing//fuzzing:cc_defs.bzl"
# error.
local_repository(
    name = "ignore_third_party_utf8_range_subtree",
    path = "third_party/utf8_range",
)

load("@io_bazel_rules_python//python:pip.bzl", "pip_install")

pip_install(
    name = "grpc_python_dependencies",
    requirements = "@com_github_grpc_grpc//:requirements.bazel.txt",
)

load("@upb//bazel:system_python.bzl", "system_python")

system_python(
    name = "system_python",
    minimum_python_version = "3.7",
)

load("@system_python//:pip.bzl", "pip_parse")

pip_parse(
    name = "pip_deps",
    requirements = "@upb//python:requirements.txt",
    requirements_overrides = {
        "3.11": "@upb//python:requirements_311.txt",
    },
)

http_archive(
    name = "build_bazel_rules_swift",
    sha256 = "bf2861de6bf75115288468f340b0c4609cc99cc1ccc7668f0f71adfd853eedb3",
    url = "https://github.com/bazelbuild/rules_swift/releases/download/1.7.1/rules_swift.1.7.1.tar.gz",
)

load(
    "@build_bazel_rules_swift//swift:repositories.bzl",
    "swift_rules_dependencies",
)

swift_rules_dependencies()

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

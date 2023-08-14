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
"""Loads the dependencies necessary for the external repositories defined in grpc_deps.bzl."""

load("@bazel_gazelle//:deps.bzl", "gazelle_dependencies")
load("@build_bazel_apple_support//lib:repositories.bzl", "apple_support_dependencies")
load("@build_bazel_rules_apple//apple:repositories.bzl", "apple_rules_dependencies")
load("@com_envoyproxy_protoc_gen_validate//:dependencies.bzl", "go_third_party")
load("@com_google_googleapis//:repository_rules.bzl", "switched_rules_by_language")
load("@com_google_protobuf//:protobuf_deps.bzl", "protobuf_deps")
load("@envoy_api//bazel:repositories.bzl", "api_dependencies")
load("@google_cloud_cpp//bazel:google_cloud_cpp_deps.bzl", "google_cloud_cpp_deps")
load("@io_bazel_rules_go//go:deps.bzl", "go_register_toolchains", "go_rules_dependencies")
load("@upb//bazel:workspace_deps.bzl", "upb_deps")

def grpc_extra_deps(ignore_version_differences = False):
    """Loads the extra dependencies.

    These are necessary for using the external repositories defined in
    grpc_deps.bzl. Projects that depend on gRPC as an external repository need
    to call both grpc_deps and grpc_extra_deps, if they have not already loaded
    the extra dependencies. For example, they can do the following in their
    WORKSPACE
    ```
    load("@com_github_grpc_grpc//bazel:grpc_deps.bzl", "grpc_deps", "grpc_test_only_deps")
    grpc_deps()

    grpc_test_only_deps()

    load("@com_github_grpc_grpc//bazel:grpc_extra_deps.bzl", "grpc_extra_deps")

    grpc_extra_deps()
    ```

    Args:
      ignore_version_differences: Plumbed directly to the invocation of
        apple_rules_dependencies.
    """
    protobuf_deps()

    upb_deps()

    api_dependencies()

    go_rules_dependencies()
    go_register_toolchains(version = "1.18")
    gazelle_dependencies()

    # Pull-in the go 3rd party dependencies for protoc_gen_validate, which is
    # needed for building C++ xDS protos
    go_third_party()

    apple_rules_dependencies(ignore_version_differences = ignore_version_differences)

    apple_support_dependencies()

    # Initialize Google APIs with only C++ and Python targets
    switched_rules_by_language(
        name = "com_google_googleapis_imports",
        cc = True,
        grpc = True,
        python = True,
    )

    google_cloud_cpp_deps()

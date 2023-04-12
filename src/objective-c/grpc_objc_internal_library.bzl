# Copyright 2019 gRPC authors.
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

"""
This is for the gRPC build system. This isn't intended to be used outsite of
the BUILD file for gRPC. It contains the mapping for the template system we
use to generate other platform's build system files.

Please consider that there should be a high bar for additions and changes to
this file.
Each rule listed must be re-written for Google's internal build system, and
each change must be ported from one to the other.
"""

load("@rules_proto//proto:defs.bzl", "proto_library")
load(
    "//bazel:generate_objc.bzl",
    "generate_objc",
    "generate_objc_hdrs",
    "generate_objc_non_arc_srcs",
    "generate_objc_srcs",
)
load("//bazel:grpc_build_system.bzl", "grpc_objc_library")
load("@build_bazel_rules_apple//apple:ios.bzl", "ios_unit_test")
load(
    "@build_bazel_rules_apple//apple/testing/default_runner:ios_test_runner.bzl",
    "ios_test_runner",
)

# The default device type for ios objc unit tests
IOS_UNIT_TEST_DEVICE_TYPE = "iPhone 11"

# The default iOS version for ios objc unit tests
# IOS_UNIT_TEST_OS_VERSION = "13.3"

def grpc_objc_ios_unit_test(
        name,
        deps,
        env = {}):
    """ios unit test for running objc test suite on iOS simulator runner

    Args:
        name: The name of the unit test target.
        deps: The dependencies of the target.
        env: Optional test environment variables passed to the test
    """
    test_runner = "grpc_ios_sim_runner_" + name
    ios_test_runner(
        name = test_runner,
        device_type = IOS_UNIT_TEST_DEVICE_TYPE,
        # os_version = IOS_UNIT_TEST_OS_VERSION,
        test_environment = env,
    )

    ios_unit_test(
        name = name,
        minimum_os_version = "9.0",
        runner = test_runner,
        deps = deps,
    )

def proto_library_objc_wrapper(
        name,
        srcs,
        deps = [],
        use_well_known_protos = False):
    """proto_library for adding dependencies to google/protobuf protos.

    Args:
      name: The name of the target.
      srcs: The sources to include.
      deps: The dependencies of the target.
      use_well_known_protos: ignored in open source version.
    """
    proto_library(
        name = name,
        srcs = srcs,
        deps = deps,
    )

def grpc_objc_examples_library(
        name,
        srcs = [],
        hdrs = [],
        textual_hdrs = [],
        data = [],
        deps = [],
        defines = [],
        sdk_frameworks = [],
        includes = []):
    """objc_library for testing, only works in //src/objective-c/exmaples

    Args:
        name: name of target
        hdrs: public headers
        srcs: all source files (.m)
        textual_hdrs: private headers
        data: any other bundle resources
        defines: preprocessors
        sdk_frameworks: sdks
        includes: added to search path, always [the path to objc directory]
        deps: dependencies
    """
    grpc_objc_library(
        name = name,
        srcs = srcs,
        hdrs = hdrs,
        textual_hdrs = textual_hdrs,
        data = data,
        defines = defines,
        includes = includes,
        sdk_frameworks = sdk_frameworks,
        deps = deps + [":RemoteTest"],
    )

def grpc_objc_testing_library(
        name,
        srcs = [],
        hdrs = [],
        textual_hdrs = [],
        data = [],
        deps = [],
        defines = [],
        includes = []):
    """objc_library for testing, only works in //src/objective-c/tests

    Args:
        name: name of target
        hdrs: public headers
        srcs: all source files (.m)
        textual_hdrs: private headers
        data: any other bundle resources
        defines: preprocessors
        includes: added to search path, always [the path to objc directory]
        deps: dependencies
    """

    additional_deps = [
        ":RemoteTest",
        "//src/objective-c:grpc_objc_client_internal_testing",
    ]

    if not name == "TestConfigs":
        additional_deps.append(":TestConfigs")

    grpc_objc_library(
        name = name,
        hdrs = hdrs,
        srcs = srcs,
        textual_hdrs = textual_hdrs,
        data = data,
        defines = defines,
        includes = includes,
        deps = deps + additional_deps,
        testonly = 1,
    )

def local_objc_grpc_library(name, deps, testing = True, srcs = [], use_well_known_protos = False, **kwargs):
    """objc_library for use within the repo.

    For local targets within the gRPC repository only. Will not work outside of the repo.

    Args:
      name: The name of the library.
      deps: The library dependencies.
      testing: Whether or not to include testing dependencies.
      srcs: The source files for the rule.
      use_well_known_protos: Whether or not to include well known protos.
      **kwargs: Other arguments to apply to the library.
    """
    objc_grpc_library_name = "_" + name + "_objc_grpc_library"

    generate_objc(
        name = objc_grpc_library_name,
        srcs = srcs,
        deps = deps,
        use_well_known_protos = use_well_known_protos,
        **kwargs
    )

    generate_objc_hdrs(
        name = objc_grpc_library_name + "_hdrs",
        src = ":" + objc_grpc_library_name,
    )

    generate_objc_non_arc_srcs(
        name = objc_grpc_library_name + "_non_arc_srcs",
        src = ":" + objc_grpc_library_name,
    )

    arc_srcs = None
    if len(srcs) > 0:
        generate_objc_srcs(
            name = objc_grpc_library_name + "_srcs",
            src = ":" + objc_grpc_library_name,
        )
        arc_srcs = [":" + objc_grpc_library_name + "_srcs"]

    library_deps = ["@com_google_protobuf//:protobuf_objc"]
    if testing:
        library_deps.append("//src/objective-c:grpc_objc_client_internal_testing")
    else:
        library_deps.append("//src/objective-c:proto_objc_rpc")

    grpc_objc_library(
        name = name,
        hdrs = [":" + objc_grpc_library_name + "_hdrs"],
        non_arc_srcs = [":" + objc_grpc_library_name + "_non_arc_srcs"],
        srcs = arc_srcs,
        defines = [
            "GPB_USE_PROTOBUF_FRAMEWORK_IMPORTS=0",
            "GPB_GRPC_FORWARD_DECLARE_MESSAGE_PROTO=0",
        ],
        includes = ["_generated_protos"],
        deps = library_deps,
    )

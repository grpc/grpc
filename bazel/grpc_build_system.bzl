# Copyright 2016 gRPC authors.
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

#
# This is for the gRPC build system. This isn't intended to be used outsite of
# the BUILD file for gRPC. It contains the mapping for the template system we
# use to generate other platform's build system files.
#
# Please consider that there should be a high bar for additions and changes to
# this file.
# Each rule listed must be re-written for Google's internal build system, and
# each change must be ported from one to the other.
#

"""
Contains macros used throughout the repo.
"""

load("//bazel:cc_grpc_library.bzl", "cc_grpc_library")
load("//bazel:copts.bzl", "GRPC_DEFAULT_COPTS")
load("@upb//bazel:upb_proto_library.bzl", "upb_proto_library", "upb_proto_reflection_library")
load("@build_bazel_rules_apple//apple:ios.bzl", "ios_unit_test")
load("@build_bazel_rules_apple//apple/testing/default_runner:ios_test_runner.bzl", "ios_test_runner")

# The set of pollers to test against if a test exercises polling
POLLERS = ["epollex", "epoll1", "poll"]

def if_not_windows(a):
    return select({
        "//:windows": [],
        "//:windows_msvc": [],
        "//conditions:default": a,
    })

def if_mac(a):
    return select({
        "//:mac_x86_64": a,
        "//conditions:default": [],
    })

def _get_external_deps(external_deps):
    ret = []
    for dep in external_deps:
        if dep == "address_sorting":
            ret.append("//third_party/address_sorting")
        elif dep == "xxhash":
            ret.append("//third_party/xxhash")
        elif dep == "cares":
            ret += select({
                "//:grpc_no_ares": [],
                "//conditions:default": ["//external:cares"],
            })
        elif dep == "cronet_c_for_grpc":
            ret.append("//third_party/objective_c/Cronet:cronet_c_for_grpc")
        elif dep.startswith("absl/"):
            ret.append("@com_google_absl//" + dep)
        else:
            ret.append("//external:" + dep)
    return ret

def _update_visibility(visibility):
    if visibility == None:
        return None

    # Visibility rules prefixed with '@grpc_' are used to flag different visibility rule
    # classes upstream.
    PUBLIC = ["//visibility:public"]
    PRIVATE = ["//:__subpackages__"]
    VISIBILITY_TARGETS = {
        "alt_gpr_base_legacy": PRIVATE,
        "alt_grpc++_base_legacy": PRIVATE,
        "alt_grpc_base_legacy": PRIVATE,
        "alt_grpc++_base_unsecure_legacy": PRIVATE,
        "alts_frame_protector": PRIVATE,
        "channelz": PRIVATE,
        "client_channel": PRIVATE,
        "debug_location": PRIVATE,
        "endpoint_tests": PRIVATE,
        "grpclb": PRIVATE,
        "grpc_opencensus_plugin": PUBLIC,
        "grpc_resolver_fake": PRIVATE,
        "grpc++_test": PRIVATE,
        "httpcli": PRIVATE,
        "public": PUBLIC,
        "ref_counted_ptr": PRIVATE,
        "trace": PRIVATE,
        "tsi_interface": PRIVATE,
        "tsi": PRIVATE,
        "xds": PRIVATE,
    }
    final_visibility = []
    for rule in visibility:
        if rule.startswith("@grpc:"):
            for replacement in VISIBILITY_TARGETS[rule[len("@grpc:"):]]:
                final_visibility.append(replacement)
        else:
            final_visibility.append(rule)
    return [x for x in final_visibility]

def grpc_cc_library(
        name,
        srcs = [],
        public_hdrs = [],
        hdrs = [],
        external_deps = [],
        defines = [],
        deps = [],
        select_deps = None,
        standalone = False,
        language = "C++",
        testonly = False,
        visibility = None,
        alwayslink = 0,
        data = [],
        use_cfstream = False,
        tags = [],
        linkstatic = False):
    """An internal wrapper around cc_library.

    Args:
      name: The name of the library.
      srcs: The source files.
      public_hdrs: The public headers.
      hdrs: The headers.
      external_deps: External depdendencies to be resolved.
      defines: Build defines to use.
      deps: cc_library deps.
      select_deps: deps included conditionally.
      standalone: Unused.
      language: The language of the library, e.g. C, C++.
      testonly: Whether the target is for tests only.
      visibility: The visibility of the target.
      alwayslink: Whether to enable alwayslink on the cc_library.
      data: Data dependencies.
      use_cfstream: Whether to use cfstream.
      tags: Tags to apply to the rule.
      linkstatic: Whether to enable linkstatic on the cc_library.
    """
    visibility = _update_visibility(visibility)
    copts = []
    if use_cfstream:
        copts = if_mac(["-DGRPC_CFSTREAM"])
    if language.upper() == "C":
        copts = copts + if_not_windows(["-std=c99"])
    linkopts = if_not_windows(["-pthread"])
    if use_cfstream:
        linkopts = linkopts + if_mac(["-framework CoreFoundation"])

    if select_deps:
        for select_deps_entry in select_deps:
            deps += select(select_deps_entry)

    native.cc_library(
        name = name,
        srcs = srcs,
        defines = defines +
                  select({
                      "//:grpc_no_ares": ["GRPC_ARES=0"],
                      "//conditions:default": [],
                  }) +
                  select({
                      "//:remote_execution": ["GRPC_PORT_ISOLATED_RUNTIME=1"],
                      "//conditions:default": [],
                  }) +
                  select({
                      "//:grpc_allow_exceptions": ["GRPC_ALLOW_EXCEPTIONS=1"],
                      "//:grpc_disallow_exceptions": ["GRPC_ALLOW_EXCEPTIONS=0"],
                      "//conditions:default": [],
                  }),
        hdrs = hdrs + public_hdrs,
        deps = deps + _get_external_deps(external_deps),
        copts = GRPC_DEFAULT_COPTS + copts,
        visibility = visibility,
        testonly = testonly,
        linkopts = linkopts,
        includes = [
            "include",
            "src/core/ext/upb-generated",  # Once upb code-gen issue is resolved, remove this.
            "src/core/ext/upbdefs-generated",  # Once upb code-gen issue is resolved, remove this.
        ],
        alwayslink = alwayslink,
        data = data,
        tags = tags,
        linkstatic = linkstatic,
    )

def grpc_proto_plugin(name, srcs = [], deps = []):
    native.cc_binary(
        name = name,
        srcs = srcs,
        deps = deps,
    )

def grpc_proto_library(
        name,
        srcs = [],
        deps = [],
        well_known_protos = False,
        has_services = True,
        use_external = False,
        generate_mocks = False):
    cc_grpc_library(
        name = name,
        srcs = srcs,
        deps = deps,
        well_known_protos = well_known_protos,
        proto_only = not has_services,
        use_external = use_external,
        generate_mocks = generate_mocks,
    )

def ios_cc_test(
        name,
        tags = [],
        **kwargs):
    """An ios C++ test target.

    Args:
      name: The name of the test.
      tags: The tags to apply to the test.
      **kwargs: All other arguments to apply.
    """
    test_lib_ios = name + "_test_lib_ios"
    ios_tags = tags + ["manual", "ios_cc_test"]
    test_runner = "ios_x86_64_sim_runner_" + name
    ios_test_runner(
        name = test_runner,
        device_type = "iPhone X",
    )
    if not any([t for t in tags if t.startswith("no_test_ios")]):
        native.objc_library(
            name = test_lib_ios,
            srcs = kwargs.get("srcs"),
            deps = kwargs.get("deps"),
            copts = kwargs.get("copts"),
            data = kwargs.get("data"),
            tags = ios_tags,
            alwayslink = 1,
            testonly = 1,
        )
        ios_test_deps = [":" + test_lib_ios]
        ios_unit_test(
            name = name + "_on_ios",
            size = kwargs.get("size"),
            data = kwargs.get("data"),
            tags = ios_tags,
            minimum_os_version = "9.0",
            runner = test_runner,
            deps = ios_test_deps,
        )

def grpc_cc_test(name, srcs = [], deps = [], external_deps = [], args = [], data = [], uses_polling = True, language = "C++", size = "medium", timeout = None, tags = [], exec_compatible_with = [], exec_properties = {}, shard_count = None, flaky = None, copts = [], linkstatic = None):
    """A cc_test target for use in the gRPC repo.

    Args:
        name: The name of the test.
        srcs: The source files.
        deps: The target deps.
        external_deps: The external deps.
        args: The args to supply to the test binary.
        data: Data dependencies.
        uses_polling: Whether the test uses polling.
        language: The language of the test, e.g C, C++.
        size: The size of the test.
        timeout: The test timeout.
        tags: The tags for the test.
        exec_compatible_with: A list of constraint values that must be
            satisifed for the platform.
        exec_properties: A dictionary of strings that will be added to the
            exec_properties of a platform selected for this target.
        shard_count: The number of shards for this test.
        flaky: Whether this test is flaky.
        copts: Add these to the compiler invocation.
        linkstatic: link the binary in static mode
    """
    copts = copts + if_mac(["-DGRPC_CFSTREAM"])
    if language.upper() == "C":
        copts = copts + if_not_windows(["-std=c99"])

    # NOTE: these attributes won't be used for the poller-specific versions of a test
    # automatically, you need to set them explicitly (if applicable)
    args = {
        "srcs": srcs,
        "args": args,
        "data": data,
        "deps": deps + _get_external_deps(external_deps),
        "copts": GRPC_DEFAULT_COPTS + copts,
        "linkopts": if_not_windows(["-pthread"]),
        "size": size,
        "timeout": timeout,
        "exec_compatible_with": exec_compatible_with,
        "exec_properties": exec_properties,
        "shard_count": shard_count,
        "flaky": flaky,
        "linkstatic": linkstatic,
    }
    if uses_polling:
        # the vanilla version of the test should run on platforms that only
        # support a single poller
        native.cc_test(
            name = name,
            testonly = True,
            tags = (tags + [
                "no_linux",  # linux supports multiple pollers
            ]),
            **args
        )

        # on linux we run the same test multiple times, once for each poller
        for poller in POLLERS:
            native.sh_test(
                name = name + "@poller=" + poller,
                data = [name] + data,
                srcs = [
                    "//test/core/util:run_with_poller_sh",
                ],
                size = size,
                timeout = timeout,
                args = [
                    poller,
                    "$(location %s)" % name,
                ] + args["args"],
                tags = (tags + ["no_windows", "no_mac"]),
                exec_compatible_with = exec_compatible_with,
                exec_properties = exec_properties,
                shard_count = shard_count,
                flaky = flaky,
            )
    else:
        # the test behavior doesn't depend on polling, just generate the test
        native.cc_test(name = name, tags = tags + ["no_uses_polling"], **args)
    ios_cc_test(
        name = name,
        tags = tags,
        **args
    )

def grpc_cc_binary(name, srcs = [], deps = [], external_deps = [], args = [], data = [], language = "C++", testonly = False, linkshared = False, linkopts = [], tags = [], features = []):
    """Generates a cc_binary for use in the gRPC repo.

    Args:
      name: The name of the target.
      srcs: The source files.
      deps: The dependencies.
      external_deps: The external dependencies.
      args: The arguments to supply to the binary.
      data: The data dependencies.
      language: The language of the binary, e.g. C, C++.
      testonly: Whether the binary is for tests only.
      linkshared: Enables linkshared on the binary.
      linkopts: linkopts to supply to the cc_binary.
      tags: Tags to apply to the target.
      features: features to be supplied to the cc_binary.
    """
    copts = []
    if language.upper() == "C":
        copts = ["-std=c99"]
    native.cc_binary(
        name = name,
        srcs = srcs,
        args = args,
        data = data,
        testonly = testonly,
        linkshared = linkshared,
        deps = deps + _get_external_deps(external_deps),
        copts = GRPC_DEFAULT_COPTS + copts,
        linkopts = if_not_windows(["-pthread"]) + linkopts,
        tags = tags,
        features = features,
    )

# buildifier: disable=unnamed-macro
def grpc_generate_one_off_targets():
    # In open-source, grpc_objc* libraries depend directly on //:grpc
    native.alias(
        name = "grpc_objc",
        actual = "//:grpc",
    )

def grpc_generate_objc_one_off_targets():
    pass

def grpc_sh_test(name, srcs, args = [], data = [], tags = []):
    native.sh_test(
        name = name,
        srcs = srcs,
        args = args,
        data = data,
        tags = tags,
    )

def grpc_sh_binary(name, srcs, data = []):
    native.sh_binary(
        name = name,
        srcs = srcs,
        data = data,
    )

def grpc_py_binary(
        name,
        srcs,
        data = [],
        deps = [],
        external_deps = [],
        testonly = False,
        python_version = "PY2",
        **kwargs):
    native.py_binary(
        name = name,
        srcs = srcs,
        testonly = testonly,
        data = data,
        deps = deps + _get_external_deps(external_deps),
        python_version = python_version,
        **kwargs
    )

def grpc_package(name, visibility = "private", features = []):
    """Creates a package.

    Args:
        name: The name of the target
        visibility: The visibility of the target.
        features: The features to enable.
    """
    if visibility == "tests":
        visibility = ["//test:__subpackages__"]
    elif visibility == "public":
        visibility = ["//visibility:public"]
    elif visibility == "private":
        visibility = []
    else:
        fail("Unknown visibility " + visibility)

    if len(visibility) != 0:
        # buildifier: disable=native-package
        native.package(
            default_visibility = visibility,
            features = features,
        )

def grpc_objc_library(
        name,
        srcs = [],
        hdrs = [],
        textual_hdrs = [],
        data = [],
        deps = [],
        defines = [],
        includes = [],
        visibility = ["//visibility:public"]):
    """The grpc version of objc_library, only used for the Objective-C library compilation

    Args:
        name: name of target
        hdrs: public headers
        srcs: all source files (.m)
        textual_hdrs: private headers
        data: any other bundle resources
        defines: preprocessors
        includes: added to search path, always [the path to objc directory]
        deps: dependencies
        visibility: visibility, default to public
    """

    native.objc_library(
        name = name,
        hdrs = hdrs,
        srcs = srcs,
        textual_hdrs = textual_hdrs,
        data = data,
        deps = deps,
        defines = defines,
        includes = includes,
        visibility = visibility,
    )

def grpc_upb_proto_library(name, deps):
    upb_proto_library(name = name, deps = deps)

def grpc_upb_proto_reflection_library(name, deps):
    upb_proto_reflection_library(name = name, deps = deps)

# buildifier: disable=unnamed-macro
def python_config_settings():
    native.config_setting(
        name = "python3",
        flag_values = {"@bazel_tools//tools/python:python_version": "PY3"},
    )

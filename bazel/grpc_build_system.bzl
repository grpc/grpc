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

load("//bazel:cc_grpc_library.bzl", "cc_grpc_library")
load("//bazel:copts.bzl", "GRPC_DEFAULT_COPTS")
load("@upb//bazel:upb_proto_library.bzl", "upb_proto_library")
load("@build_bazel_rules_apple//apple:ios.bzl", "ios_unit_test")

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
            ret += ["//third_party/address_sorting"]
        elif dep == "xxhash":
            ret += ["//third_party/xxhash"]
        elif dep == "cares":
            ret += select({
                "//:grpc_no_ares": [],
                "//conditions:default": ["//external:cares"],
            })
        elif dep == "cronet_c_for_grpc":
            ret += ["//third_party/objective_c/Cronet:cronet_c_for_grpc"]
        elif dep.startswith("absl/"):
            ret += ["@com_google_absl//" + dep]
        else:
            ret += ["//external:" + dep]
    return ret

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
    copts = []
    if use_cfstream:
        copts = if_mac(["-DGRPC_CFSTREAM"])
    if language.upper() == "C":
        copts = copts + if_not_windows(["-std=c99"])
    linkopts = if_not_windows(["-pthread"])
    if use_cfstream:
        linkopts = linkopts + if_mac(["-framework CoreFoundation"])

    if select_deps:
        deps += select(select_deps)

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
    ios_test_adapter = "//third_party/objective_c/google_toolbox_for_mac:GTM_GoogleTestRunner_GTM_USING_XCTEST"

    test_lib_ios = name + "_test_lib_ios"
    ios_tags = tags + ["manual", "ios_cc_test"]
    if not any([t for t in tags if t.startswith("no_test_ios")]):
        native.objc_library(
            name = test_lib_ios,
            srcs = kwargs.get("srcs"),
            deps = kwargs.get("deps"),
            copts = kwargs.get("copts"),
            tags = ios_tags,
            alwayslink = 1,
            testonly = 1,
        )
        ios_test_deps = [ios_test_adapter, ":" + test_lib_ios]
        ios_unit_test(
            name = name + "_on_ios",
            size = kwargs.get("size"),
            tags = ios_tags,
            minimum_os_version = "9.0",
            deps = ios_test_deps,
        )

def grpc_cc_test(name, srcs = [], deps = [], external_deps = [], args = [], data = [], uses_polling = True, language = "C++", size = "medium", timeout = None, tags = [], exec_compatible_with = [], exec_properties = {}, shard_count = None, flaky = None, copts = []):
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

def grpc_generate_one_off_targets():
    # In open-source, grpc_objc* libraries depend directly on //:grpc
    native.alias(
        name = "grpc_objc",
        actual = "//:grpc",
    )

def grpc_generate_objc_one_off_targets():
    pass

def grpc_sh_test(name, srcs, args = [], data = []):
    native.sh_test(
        name = name,
        srcs = srcs,
        args = args,
        data = data,
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
    if visibility == "tests":
        visibility = ["//test:__subpackages__"]
    elif visibility == "public":
        visibility = ["//visibility:public"]
    elif visibility == "private":
        visibility = []
    else:
        fail("Unknown visibility " + visibility)

    if len(visibility) != 0:
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

def python_config_settings():
    native.config_setting(
        name = "python3",
        flag_values = {"@bazel_tools//tools/python:python_version": "PY3"},
    )

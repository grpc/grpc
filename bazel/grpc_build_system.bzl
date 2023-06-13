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
load("//bazel:experiments.bzl", "EXPERIMENTS")
load("@upb//bazel:upb_proto_library.bzl", "upb_proto_library", "upb_proto_reflection_library")
load("@build_bazel_rules_apple//apple:ios.bzl", "ios_unit_test")
load("@build_bazel_rules_apple//apple/testing/default_runner:ios_test_runner.bzl", "ios_test_runner")

# The set of pollers to test against if a test exercises polling
POLLERS = ["epoll1", "poll"]

# The set of known EventEngines to test
EVENT_ENGINES = {"default": {"tags": []}}

def if_not_windows(a):
    return select({
        "//:windows": [],
        "//:windows_msvc": [],
        "//conditions:default": a,
    })

def if_windows(a):
    return select({
        "//:windows": a,
        "//:windows_msvc": a,
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
        elif dep.startswith("google/"):
            ret.append("@com_google_googleapis//" + dep)
        else:
            ret.append("//external:" + dep)
    return ret

def _update_visibility(visibility):
    if visibility == None:
        return None

    # Visibility rules prefixed with '@grpc:' are used to flag different visibility rule
    # classes upstream.
    PUBLIC = ["//visibility:public"]
    PRIVATE = ["//:__subpackages__"]
    VISIBILITY_TARGETS = {
        "alt_grpc++_base_legacy": PRIVATE,
        "alt_grpc_base_legacy": PRIVATE,
        "alt_grpc++_base_unsecure_legacy": PRIVATE,
        "alts_frame_protector": PRIVATE,
        "channelz": PRIVATE,
        "client_channel": PRIVATE,
        "cli": PRIVATE,
        "debug_location": PRIVATE,
        "endpoint_tests": PRIVATE,
        "exec_ctx": PRIVATE,
        "grpclb": PRIVATE,
        "grpc_opencensus_plugin": PUBLIC,
        "grpcpp_gcp_observability": PUBLIC,
        "grpc_resolver_fake": PRIVATE,
        "grpc++_test": PRIVATE,
        "http": PRIVATE,
        "httpcli": PRIVATE,
        "iomgr_timer": PRIVATE,
        "iomgr_internal_errqueue": PRIVATE,
        "iomgr_buffer_list": PRIVATE,
        "json_reader_legacy": PRIVATE,
        "public": PUBLIC,
        "ref_counted_ptr": PRIVATE,
        "trace": PRIVATE,
        "tsi_interface": PRIVATE,
        "tsi": PRIVATE,
        "xds": PRIVATE,
        "xds_client_core": PRIVATE,
        "grpc_python_observability": PRIVATE,
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
        tags = [],
        linkopts = [],
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
      tags: Tags to apply to the rule.
      linkopts: Extra libraries to link.
      linkstatic: Whether to enable linkstatic on the cc_library.
    """
    visibility = _update_visibility(visibility)
    copts = []
    if language.upper() == "C":
        copts = copts + if_not_windows(["-std=c11"])
    linkopts = linkopts + if_not_windows(["-pthread"]) + if_windows(["-defaultlib:ws2_32.lib"])
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
    test_lib_cc = name + "_test_lib_cc"
    test_lib_ios = name + "_test_lib_ios"
    ios_tags = tags + ["manual", "ios_cc_test"]
    test_runner = "ios_x86_64_sim_runner_" + name
    ios_test_runner(
        name = test_runner,
        device_type = "iPhone X",
    )
    if not any([t for t in tags if t.startswith("no_test_ios")]):
        native.cc_library(
            name = test_lib_cc,
            srcs = kwargs.get("srcs"),
            deps = kwargs.get("deps"),
            copts = kwargs.get("copts"),
            data = kwargs.get("data"),
            tags = ios_tags,
            alwayslink = 1,
            testonly = 1,
        )
        native.objc_library(
            name = test_lib_ios,
            deps = [":" + test_lib_cc],
            alwayslink = 1,
            testonly = 1,
        )
        ios_test_deps = [":" + test_lib_ios]
        ios_unit_test(
            name = name + "_on_ios",
            size = kwargs.get("size"),
            data = kwargs.get("data"),
            tags = ios_tags,
            minimum_os_version = "11.0",
            runner = test_runner,
            deps = ios_test_deps,
        )

def expand_tests(name, srcs, deps, tags, args, exclude_pollers, uses_polling, uses_event_engine, flaky):
    """Common logic used to parameterize tests for every poller and EventEngine and experiment.

    Args:
        name: base name of the test
        srcs: source files
        deps: base deps
        tags: base tags
        args: base args
        flaky: base flaky
        exclude_pollers: list of poller names to exclude for this set of tests.
        uses_polling: set to False if the test is not sensitive to polling methodology.
        uses_event_engine: set to False if the test is not sensitive to
            EventEngine implementation differences

    Returns:
        A list of dictionaries containing modified values of name, srcs, deps, tags, and args.
    """
    poller_config = []

    if not uses_polling:
        tags = tags + ["no_uses_polling"]

        poller_config.append({
            "name": name,
            "srcs": srcs,
            "deps": deps,
            "tags": tags,
            "args": args,
            "flaky": flaky,
            "env": {},
        })
    else:
        # On linux we run the same test with the default EventEngine, once for each
        # poller
        for poller in POLLERS:
            if poller in exclude_pollers:
                continue
            poller_config.append({
                "name": name + "@poller=" + poller,
                "srcs": srcs,
                "deps": deps,
                "tags": (tags + EVENT_ENGINES["default"]["tags"] + [
                    "no_windows",
                    "no_mac",
                    "bazel_only",
                ]),
                "args": args,
                "env": {
                    "GRPC_POLL_STRATEGY": poller,
                },
                "flaky": flaky,
            })

        # Now generate one test for each subsequent EventEngine, all using the
        # default poller. These tests will have `@engine=<name>` appended to the
        # test target name. If a test target name has no `@engine=<name>` component,
        # that indicates that the default EventEngine is being used.
        if not uses_event_engine:
            # The poller tests exercise the default engine on Linux. This test
            # handles other platforms.
            poller_config.append({
                "name": name,
                "srcs": srcs,
                "deps": deps,
                "tags": tags + ["no_linux"],
                "args": args,
                "env": {},
                "flaky": flaky,
            })
        else:
            for engine_name, engine in EVENT_ENGINES.items():
                test_name = name + "@engine=" + engine_name
                test_tags = tags + engine["tags"] + ["bazel_only"]
                test_args = args + ["--engine=" + engine_name]
                if engine_name == "default":
                    # The poller tests exercise the default engine on Linux.
                    # This test handles other platforms.
                    test_name = name
                    test_tags = tags + engine["tags"] + ["no_linux"]
                    test_args = args
                poller_config.append({
                    "name": test_name,
                    "srcs": srcs,
                    "deps": deps,
                    "tags": test_tags,
                    "args": test_args,
                    "env": {},
                    "flaky": flaky,
                })

    experiments = {}
    for mode, tag_to_experiments in EXPERIMENTS.items():
        experiments[mode] = {}
        for tag in tags:
            if tag not in tag_to_experiments:
                continue
            for experiment in tag_to_experiments[tag]:
                experiments[mode][experiment] = 1
        experiments[mode] = list(experiments[mode].keys())

    mode_config = {
        # format: <mode>: (enabled_target_tags, disabled_target_tags)
        "dbg": (["noopt"], ["nodbg"]),
        "on": (None, []),
        "off": ([], None),
    }

    must_have_tags = [
        # We don't run experiments on cmake builds
        "bazel_only",
        # Nor on windows
        "no_windows",
        # Nor on mac
        "no_mac",
        # Nor on arm64
        "no_arm64",
    ]
    experiment_config = list(poller_config)
    for mode, config in mode_config.items():
        enabled_tags, disabled_tags = config
        if enabled_tags != None:
            for experiment in experiments[mode]:
                for config in poller_config:
                    config = dict(config)
                    config["name"] = config["name"] + "@experiment=" + experiment
                    env = dict(config["env"])
                    env["GRPC_EXPERIMENTS"] = experiment
                    config["env"] = env
                    tags = config["tags"]
                    for tag in must_have_tags + enabled_tags:
                        if tag not in tags:
                            tags = tags + [tag]
                    config["tags"] = tags
                    config["flaky"] = True
                    experiment_config.append(config)
        if disabled_tags != None:
            for experiment in experiments[mode]:
                for config in poller_config:
                    config = dict(config)
                    config["name"] = config["name"] + "@experiment=no_" + experiment
                    env = dict(config["env"])
                    env["GRPC_EXPERIMENTS"] = "-" + experiment
                    config["env"] = env
                    tags = config["tags"]
                    for tag in must_have_tags + disabled_tags:
                        if tag not in tags:
                            tags = tags + [tag]
                    config["tags"] = tags
                    experiment_config.append(config)
    return experiment_config

def grpc_cc_test(name, srcs = [], deps = [], external_deps = [], args = [], data = [], uses_polling = True, language = "C++", size = "medium", timeout = None, tags = [], exec_compatible_with = [], exec_properties = {}, shard_count = None, flaky = None, copts = [], linkstatic = None, exclude_pollers = [], uses_event_engine = True):
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
        exclude_pollers: list of poller names to exclude for this set of tests.
        uses_event_engine: set to False if the test is not sensitive to
            EventEngine implementation differences
    """
    if language.upper() == "C":
        copts = copts + if_not_windows(["-std=c11"])

    core_deps = deps + _get_external_deps(external_deps) + ["//test/core/util:grpc_suppressions"]

    # Test args for all tests
    test_args = {
        "data": data,
        "copts": GRPC_DEFAULT_COPTS + copts,
        "linkopts": if_not_windows(["-pthread"]) + if_windows(["-defaultlib:ws2_32.lib"]),
        "size": size,
        "timeout": timeout,
        "exec_compatible_with": exec_compatible_with,
        "exec_properties": exec_properties,
        "shard_count": shard_count,
        "linkstatic": linkstatic,
    }

    if "grpc-fuzzer" not in tags and "no_test_ios" not in tags:
        ios_cc_test(
            name = name,
            srcs = srcs,
            tags = tags,
            deps = core_deps,
            args = args,
            flaky = True,
            **test_args
        )

    for poller_config in expand_tests(name, srcs, core_deps, tags, args, exclude_pollers, uses_polling, uses_event_engine, flaky):
        native.cc_test(
            name = poller_config["name"],
            srcs = poller_config["srcs"],
            deps = poller_config["deps"],
            tags = poller_config["tags"],
            args = poller_config["args"],
            env = poller_config["env"],
            flaky = poller_config["flaky"],
            **test_args
        )

def grpc_cc_binary(name, srcs = [], deps = [], external_deps = [], args = [], data = [], language = "C++", testonly = False, linkshared = False, linkopts = [], tags = [], features = [], visibility = None):
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
      visibility: The visibility of the target.
    """
    visibility = _update_visibility(visibility)
    copts = []
    if language.upper() == "C":
        copts = ["-std=c11"]
    native.cc_binary(
        name = name,
        srcs = srcs,
        args = args,
        data = data,
        testonly = testonly,
        linkshared = linkshared,
        deps = deps + _get_external_deps(external_deps) + ["//test/core/util:grpc_suppressions"],
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
    native.config_setting(
        name = "windows_other",
        values = {"define": "GRPC_WINDOWS_OTHER=1"},
    )

def grpc_generate_objc_one_off_targets():
    pass

def grpc_generate_one_off_internal_targets():
    pass

def grpc_sh_test(name, srcs = [], args = [], data = [], uses_polling = True, size = "medium", timeout = None, tags = [], exec_compatible_with = [], exec_properties = {}, shard_count = None, flaky = None, exclude_pollers = [], uses_event_engine = True):
    """Execute an sh_test for every <poller> x <EventEngine> combination

    Args:
        name: The name of the test.
        srcs: The source files.
        args: The args to supply to the test binary.
        data: Data dependencies.
        uses_polling: Whether the test uses polling.
        size: The size of the test.
        timeout: The test timeout.
        tags: The tags for the test.
        exec_compatible_with: A list of constraint values that must be
            satisifed for the platform.
        exec_properties: A dictionary of strings that will be added to the
            exec_properties of a platform selected for this target.
        shard_count: The number of shards for this test.
        flaky: Whether this test is flaky.
        exclude_pollers: list of poller names to exclude for this set of tests.
        uses_event_engine: set to False if the test is not sensitive to
            EventEngine implementation differences
    """
    test_args = {
        "data": data,
        "size": size,
        "timeout": timeout,
        "exec_compatible_with": exec_compatible_with,
        "exec_properties": exec_properties,
        "shard_count": shard_count,
    }

    for poller_config in expand_tests(name, srcs, [], tags, args, exclude_pollers, uses_polling, uses_event_engine, flaky):
        native.sh_test(
            name = poller_config["name"],
            srcs = poller_config["srcs"],
            deps = poller_config["deps"],
            tags = poller_config["tags"],
            args = poller_config["args"],
            env = poller_config["env"],
            flaky = poller_config["flaky"],
            **test_args
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
        non_arc_srcs = [],
        textual_hdrs = [],
        testonly = False,
        data = [],
        deps = [],
        defines = [],
        sdk_frameworks = [],
        includes = [],
        visibility = ["//visibility:public"]):
    """The grpc version of objc_library, only used for the Objective-C library compilation

    Args:
        name: name of target
        hdrs: public headers
        srcs: all source files (.m)
        non_arc_srcs: list of Objective-C files that DO NOT use ARC.
        textual_hdrs: private headers
        testonly: Whether the binary is for tests only.
        data: any other bundle resources
        defines: preprocessors
        sdk_frameworks: sdks
        includes: added to search path, always [the path to objc directory]
        deps: dependencies
        visibility: visibility, default to public
    """

    native.objc_library(
        name = name,
        hdrs = hdrs,
        srcs = srcs,
        non_arc_srcs = non_arc_srcs,
        textual_hdrs = textual_hdrs,
        copts = GRPC_DEFAULT_COPTS + ["-ObjC++", "-std=gnu++14"],
        testonly = testonly,
        data = data,
        deps = deps,
        defines = defines,
        includes = includes,
        sdk_frameworks = sdk_frameworks,
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

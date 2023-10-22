# Copyright 2023 gRPC authors.
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

load("//bazel:copts.bzl", "GRPC_DEFAULT_COPTS")
load("//bazel:experiments.bzl", "EXPERIMENTS")
load("//bazel:test_experiments.bzl", "TEST_EXPERIMENTS")
load("@build_bazel_rules_apple//apple:ios.bzl", "ios_unit_test")
load("@build_bazel_rules_apple//apple/testing/default_runner:ios_test_runner.bzl", "ios_test_runner")

# The set of pollers to test against if a test exercises polling
POLLERS = ["epoll1", "poll"]

# The set of known EventEngines to test
EVENT_ENGINES = {"default": {"tags": []}}

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
        elif dep.startswith("otel/"):
            ret.append(dep.replace("otel/", "@io_opentelemetry_cpp//"))
        elif dep.startswith("google_cloud_cpp"):
            ret.append(dep.replace("google_cloud_cpp", "@google_cloud_cpp//"))
        else:
            ret.append("//external:" + dep)
    return ret

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

    # buildifier: disable=uninitialized
    def _populate_experiments_platform_config(config, platform_experiments_map):
        for platform, experiments_on_platform in platform_experiments_map.items():
            for mode, tag_to_experiments in experiments_on_platform.items():
                if mode not in config:
                    config[mode] = {}
                for tag in tags:
                    if tag not in tag_to_experiments:
                        continue
                    for experiment in tag_to_experiments[tag]:
                        if experiment not in config[mode]:
                            config[mode][experiment] = []
                        config[mode][experiment].append(platform)

    _populate_experiments_platform_config(experiments, EXPERIMENTS)
    _populate_experiments_platform_config(experiments, TEST_EXPERIMENTS)

    mode_config = {
        # format: <mode>: (enabled_target_tags, disabled_target_tags)
        "on": (None, []),
        "off": ([], None),
    }

    must_have_tags = [
        # We don't run experiments on cmake builds
        "bazel_only",
        # Nor on mac
        "no_mac",
        # Nor on arm64
        "no_arm64",
    ]

    def _update_experiments_platform_test_tags(tags, platforms):
        if "posix" not in platforms:
            if "no_linux" not in tags:
                tags.append("no_linux")
            if "no_mac" not in tags:
                tags.append("no_mac")
        if "windows" not in platforms:
            if "no_windows" not in tags:
                tags.append("no_windows")
        if "ios" not in platforms:
            if "no_test_ios" not in tags:
                tags.append("no_test_ios")
        return tags

    experiment_config = list(poller_config)
    for mode, config in mode_config.items():
        enabled_tags, disabled_tags = config
        if enabled_tags != None:
            for experiment in experiments[mode].keys():
                for config in poller_config:
                    config = dict(config)
                    config["name"] = config["name"] + "@experiment=" + experiment
                    env = dict(config["env"])
                    env["GRPC_EXPERIMENTS"] = experiment
                    env["GRPC_CI_EXPERIMENTS"] = "1"
                    config["env"] = env
                    tags = config["tags"]
                    for tag in must_have_tags + enabled_tags:
                        if tag not in tags:
                            tags = tags + [tag]
                    config["tags"] = _update_experiments_platform_test_tags(tags, experiments[mode][experiment])
                    config["flaky"] = True
                    experiment_config.append(config)
        if disabled_tags != None:
            for experiment in experiments[mode].keys():
                for config in poller_config:
                    config = dict(config)
                    config["name"] = config["name"] + "@experiment=no_" + experiment
                    env = dict(config["env"])
                    env["GRPC_EXPERIMENTS"] = "-" + experiment
                    env["GRPC_CI_EXPERIMENTS"] = "1"
                    config["env"] = env
                    tags = config["tags"]
                    for tag in must_have_tags + disabled_tags:
                        if tag not in tags:
                            tags = tags + [tag]
                    config["tags"] = _update_experiments_platform_test_tags(tags, experiments[mode][experiment])
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




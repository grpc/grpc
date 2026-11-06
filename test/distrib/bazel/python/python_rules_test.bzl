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
"""Bazel rule tests of bazel/python_rules.bzl"""

load("@bazel_skylib//lib:unittest.bzl", "analysistest", "asserts")
load("@rules_python//python:py_info.bzl", "PyInfo")

_StaticAnalysisInfo = provider(
    fields = {
        "direct_files_by_target": "The preferred direct static-analysis inputs keyed by target name.",
    },
)

def _assert_in(env, item, container):
    asserts.true(
        env,
        item in container,
        "Expected " + str(item) + " to be in " + str(container),
    )

def _short_paths(files):
    if files == None:
        return []
    return sorted([file.short_path for file in files.to_list()])

def _static_analysis_aspect_impl(target, ctx):
    py_info = target[PyInfo]
    direct_files = getattr(py_info, "direct_pyi_files", None)
    if direct_files == None or not direct_files.to_list():
        direct_files = getattr(py_info, "direct_original_sources", None)

    direct_files_by_target = {}
    for dep in getattr(ctx.rule.attr, "deps", []):
        if _StaticAnalysisInfo in dep:
            direct_files_by_target.update(dep[_StaticAnalysisInfo].direct_files_by_target)
    direct_files_by_target[target.label.name] = _short_paths(direct_files)

    return [_StaticAnalysisInfo(direct_files_by_target = direct_files_by_target)]

_static_analysis_aspect = aspect(
    implementation = _static_analysis_aspect_impl,
    attr_aspects = ["deps"],
    required_providers = [PyInfo],
    provides = [_StaticAnalysisInfo],
)

# Tests the declared outputs of the 'py_proto_library' rule and, indirectly, also  tests that
# these outputs are actually generated (building ":helloworld_py_pb2" will fail if not all of
# the declared output files are actually generated).
def _py_proto_library_provider_contents_test_impl(ctx):
    env = analysistest.begin(ctx)

    target = analysistest.target_under_test(env)
    py_info = target[PyInfo]

    files = [file.short_path for file in target.files.to_list()]
    runfiles = [file.short_path for file in target.default_runfiles.files.to_list()]
    direct_original_sources = _short_paths(py_info.direct_original_sources)
    direct_pyi_files = _short_paths(py_info.direct_pyi_files)
    transitive_original_sources = _short_paths(py_info.transitive_original_sources)
    transitive_pyi_files = _short_paths(py_info.transitive_pyi_files)
    transitive_sources = _short_paths(py_info.transitive_sources)

    _assert_in(env, "helloworld_pb2.py", files)
    _assert_in(env, "subdir/hello_dep_pb2.py", files)

    _assert_in(env, "helloworld_pb2.py", runfiles)
    _assert_in(env, "subdir/hello_dep_pb2.py", runfiles)

    asserts.equals(env, [], direct_original_sources)
    asserts.equals(env, ["helloworld_pb2.pyi"], direct_pyi_files)

    asserts.equals(env, [], transitive_original_sources)
    _assert_in(env, "helloworld_pb2.pyi", transitive_pyi_files)
    _assert_in(env, "subdir/hello_dep_pb2.pyi", transitive_pyi_files)
    _assert_in(env, "helloworld_pb2.py", transitive_sources)
    _assert_in(env, "subdir/hello_dep_pb2.py", transitive_sources)

    return analysistest.end(env)

_py_proto_library_provider_contents_test = analysistest.make(_py_proto_library_provider_contents_test_impl)

def _py_grpc_library_provider_contents_test_impl(ctx):
    env = analysistest.begin(ctx)

    target = analysistest.target_under_test(env)
    py_info = target[PyInfo]

    direct_original_sources = _short_paths(py_info.direct_original_sources)
    direct_pyi_files = _short_paths(py_info.direct_pyi_files)
    transitive_original_sources = _short_paths(py_info.transitive_original_sources)
    transitive_pyi_files = _short_paths(py_info.transitive_pyi_files)
    transitive_sources = _short_paths(py_info.transitive_sources)

    asserts.equals(env, ["helloworld_pb2_grpc.py"], direct_original_sources)
    asserts.equals(env, [], direct_pyi_files)

    _assert_in(env, "helloworld_pb2_grpc.py", transitive_original_sources)
    _assert_in(env, "grpc_library_replacement.py", transitive_original_sources)
    _assert_in(env, "helloworld_pb2.pyi", transitive_pyi_files)
    _assert_in(env, "subdir/hello_dep_pb2.pyi", transitive_pyi_files)
    _assert_in(env, "helloworld_pb2_grpc.py", transitive_sources)
    _assert_in(env, "helloworld_pb2.py", transitive_sources)
    _assert_in(env, "grpc_library_replacement.py", transitive_sources)

    asserts.false(env, "helloworld_pb2.py" in direct_original_sources)
    asserts.false(env, "grpc_library_replacement.py" in direct_original_sources)

    return analysistest.end(env)

_py_grpc_library_provider_contents_test = analysistest.make(_py_grpc_library_provider_contents_test_impl)

def _pyinfo_aspect_applicability_test_impl(ctx):
    env = analysistest.begin(ctx)

    target = analysistest.target_under_test(env)
    analyzed_targets = target[_StaticAnalysisInfo].direct_files_by_target

    _assert_in(env, "helloworld_py_pb2_grpc_library_changed", analyzed_targets)

    return analysistest.end(env)

_pyinfo_aspect_applicability_test = analysistest.make(
    _pyinfo_aspect_applicability_test_impl,
    extra_target_under_test_aspects = [_static_analysis_aspect],
)

def _pyinfo_aspect_direct_input_test_impl(ctx):
    env = analysistest.begin(ctx)

    target = analysistest.target_under_test(env)
    direct_files_by_target = target[_StaticAnalysisInfo].direct_files_by_target

    asserts.equals(
        env,
        ["helloworld_pb2.pyi"],
        direct_files_by_target["helloworld_py_pb2"],
    )
    asserts.equals(
        env,
        ["helloworld_pb2_grpc.py"],
        direct_files_by_target["helloworld_py_pb2_grpc_library_changed"],
    )

    return analysistest.end(env)

_pyinfo_aspect_direct_input_test = analysistest.make(
    _pyinfo_aspect_direct_input_test_impl,
    extra_target_under_test_aspects = [_static_analysis_aspect],
)

def python_rules_test_suite(name):
    _py_proto_library_provider_contents_test(
        name = "py_proto_library_provider_contents_test",
        target_under_test = ":helloworld_py_pb2",
    )

    _py_grpc_library_provider_contents_test(
        name = "py_grpc_library_provider_contents_test",
        target_under_test = ":helloworld_py_pb2_grpc_library_changed",
    )

    _pyinfo_aspect_applicability_test(
        name = "pyinfo_aspect_applicability_test",
        target_under_test = ":helloworld_grpc_consumer",
    )

    _pyinfo_aspect_direct_input_test(
        name = "pyinfo_aspect_direct_input_test",
        target_under_test = ":helloworld_grpc_consumer",
    )

    native.test_suite(
        name = name,
        tests = [
            "py_proto_library_provider_contents_test",
            "py_grpc_library_provider_contents_test",
            "pyinfo_aspect_applicability_test",
            "pyinfo_aspect_direct_input_test",
        ],
    )

# Copyright 2023 The gRPC Authors
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
Houses py_grpc_run_time_type_check_test.
"""

load("@grpc_python_dependencies//:requirements.bzl", "requirement")

_COPIED_MAIN_SUFFIX = ".typeguard.main"

def py_grpc_run_time_type_check_test(
        name,
        srcs,
        main = None,
        deps = None,
        data = None,
        **kwargs):
    """Runs a Python test with with run time type check enabled.

    Args:
      name: The name of the test.
      srcs: The source files.
      main: The main file of the test.
      deps: The dependencies of the test.
      data: The data dependencies of the test.
      **kwargs: Any other test arguments.
    """
    if main == None:
        if len(srcs) != 1:
            fail("When main is not provided, srcs must be of size 1.")
        main = srcs[0]
    deps = [] if deps == None else deps
    data = [] if data == None else data

    lib_name = name + ".typeguard.lib"
    native.py_library(
        name = lib_name,
        srcs = srcs,
    )
    augmented_deps = deps + [
        ":{}".format(lib_name),
        requirement("typeguard"),
    ]

    # The main file needs to be in the same package as the test file.
    copied_main_name = name + _COPIED_MAIN_SUFFIX
    copied_main_filename = copied_main_name + ".py"
    native.genrule(
        name = copied_main_name,
        srcs = ["//bazel:_run_time_type_check_main.py"],
        outs = [copied_main_filename],
        cmd = "cp $< $@",
    )

    native.py_test(
        name = name + ".typeguard",
        args = [name],
        data = data,
        deps = augmented_deps,
        srcs = [copied_main_filename],
        main = copied_main_filename,
        python_version = "PY3",
        flaky = False,
        **kwargs
    )

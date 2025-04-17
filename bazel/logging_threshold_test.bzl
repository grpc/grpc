# Copyright 2024 The gRPC Authors
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
Houses py_grpc_logging_threshold_test.
"""

_COPIED_MAIN_SUFFIX = ".logging_threshold.main"

def py_grpc_logging_threshold_test(
        name,
        srcs,
        main = None,
        deps = None,
        data = None,
        **kwargs):
    """Runs a Python unit test and checks amount of logging against a threshold.

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

    lib_name = name + ".logging_threshold.lib"
    native.py_library(
        name = lib_name,
        srcs = srcs,
    )
    augmented_deps = deps + [
        ":{}".format(lib_name),
    ]

    # The main file needs to be in the same package as the test file.
    copied_main_name = name + _COPIED_MAIN_SUFFIX
    copied_main_filename = copied_main_name + ".py"
    native.genrule(
        name = copied_main_name,
        srcs = ["//bazel:_logging_threshold_test_main.py"],
        outs = [copied_main_filename],
        cmd = "cp $< $@",
    )

    native.py_test(
        name = name + ".logging_threshold",
        args = ["$(location //bazel:_single_module_tester)", name],
        data = data + ["//bazel:_single_module_tester"],
        deps = augmented_deps,
        srcs = [copied_main_filename],
        main = copied_main_filename,
        python_version = "PY3",
        flaky = False,
        **kwargs
    )

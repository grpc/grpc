# Copyright 2025 gRPC authors.
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

"""Bazel rule for running Python tests with runtime type checking."""

load("@rules_python//python:defs.bzl", "py_test")

def py_grpc_run_time_type_check_test(name, srcs, **kwargs):
    """Runs a Python test with runtime type checking enabled.

    Args:
        name: The name of the test.
        srcs: The source files for the test.
        **kwargs: Additional arguments to pass to py_test.
    """

    # Remove main from kwargs since we set it explicitly
    test_kwargs = dict(kwargs)
    if "main" in test_kwargs:
        test_kwargs.pop("main")

    py_test(
        name = name,
        srcs = srcs + ["//bazel:_run_time_type_check_main"],
        main = "//bazel:_run_time_type_check_main.py",
        args = [name],  # Pass the test module name
        **test_kwargs
    )

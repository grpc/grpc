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
"""Python-related rules intended only for use internal to the repo."""

load("@rules_python//python:defs.bzl", "py_test")
load("//bazel:gevent_test.bzl", "py_grpc_gevent_test")
load("//bazel:logging_threshold_test.bzl", "py_grpc_logging_threshold_test")
load("//bazel:run_time_type_check_test.bzl", "py_grpc_run_time_type_check_test")

def internal_py_grpc_test(name, **kwargs):
    """Runs a test under all supported environments.

    Args:
      name: The name of the test.
      **kwargs: Any additional arguments to add to the test.
    """
    py_test(
        name = name + ".native",
        python_version = "PY3",
        **kwargs
    )
    py_grpc_gevent_test(name, **kwargs)
    py_grpc_logging_threshold_test(name, **kwargs)

    suite_kwargs = {}
    if "visibility" in kwargs:
        suite_kwargs["visibility"] = kwargs["visibility"]

    native.test_suite(
        name = name,
        tests = [
            name + ".native",
            name + ".gevent",
            name + ".logging_threshold",
        ],
        **suite_kwargs
    )

def internal_py_grpc_test_with_runtime_type_check(name, **kwargs):
    """Runs a test with runtime type checking enabled.

    Args:
      name: The name of the test.
      **kwargs: Any additional arguments to add to the test.
    """
    py_grpc_run_time_type_check_test(name, **kwargs)

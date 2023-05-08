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
"""
Houses py_grpc_gevent_test.
"""

load("@grpc_python_dependencies//:requirements.bzl", "requirement")

_GRPC_LIB = "//src/python/grpcio/grpc:grpcio"

_COPIED_MAIN_SUFFIX = ".gevent.main"

def py_grpc_gevent_test(
        name,
        srcs,
        main = None,
        deps = None,
        data = None,
        **kwargs):
    """Runs a Python test with gevent monkeypatched in.

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
    lib_name = name + ".gevent.lib"
    supplied_python_version = kwargs.pop("python_version", "")
    if supplied_python_version and supplied_python_version != "PY3":
        fail("py_grpc_gevent_test only supports python_version=PY3")
    native.py_library(
        name = lib_name,
        srcs = srcs,
    )
    augmented_deps = deps + [
        ":{}".format(lib_name),
        requirement("gevent"),
    ]
    if _GRPC_LIB not in augmented_deps:
        augmented_deps.append(_GRPC_LIB)

    # The main file needs to be in the same package as the test file.
    copied_main_name = name + _COPIED_MAIN_SUFFIX
    copied_main_filename = copied_main_name + ".py"
    native.genrule(
        name = copied_main_name,
        srcs = ["//bazel:_gevent_test_main.py"],
        outs = [copied_main_filename],
        cmd = "cp $< $@",
    )

    # TODO(https://github.com/grpc/grpc/issues/27542): Remove once gevent is deemed non-flaky.
    if "flaky" in kwargs:
        kwargs.pop("flaky")

    native.py_test(
        name = name + ".gevent",
        args = [name],
        data = data,
        deps = augmented_deps,
        srcs = [copied_main_filename],
        main = copied_main_filename,
        python_version = "PY3",
        flaky = False,
        **kwargs
    )

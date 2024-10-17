# Copyright 2021 gRPC authors.
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
"""Configuration macros for grpc microbenchmarking"""

load("//bazel:grpc_build_system.bzl", "grpc_cc_test")

HISTORY = 1

def grpc_benchmark_args():
    """Command line arguments for running a microbenchmark as a test"""
    return ["--benchmark_min_time=0.001s"]

def grpc_cc_benchmark(name, external_deps = [], tags = [], uses_polling = False, uses_event_engine = False, **kwargs):
    """Base rule for gRPC benchmarks.

    This is an opinionated configuration for gRPC benchmarks.

    We disable uses_polling, uses_event_engine by default so that we minimize
    unnecessary uses of CI time.

    Similarly, we disable running on Windows, Mac to save testing time there
    (our principle target for performance work is Linux).

    linkstatic is enabled always: this is the configuration real binaries use, and
    it affects performance, so we should use it on our benchmarks too!

    Args:
        name: base name of the test
        external_deps: per grpc_cc_test
        tags: per grpc_cc_test
        uses_polling: per grpc_cc_test, but defaulted False
        uses_event_engine: per grpc_cc_test, but defaulted False
        **kwargs: per grpc_cc_test
    """
    kwargs.pop("monitoring", None)
    grpc_cc_test(
        name = name,
        args = grpc_benchmark_args(),
        external_deps = ["benchmark"] + external_deps,
        tags = tags + ["no_mac", "no_windows", "bazel_only"],
        uses_polling = uses_polling,
        uses_event_engine = uses_event_engine,
        # cc_binary defaults to 1, and we are interested in performance
        # for that, so duplicate that setting here.
        linkstatic = 1,
        **kwargs
    )

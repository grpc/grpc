#!/usr/bin/env python3
#
# Copyright 2017 gRPC authors.
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
""" Python utility to build opt and counters benchmarks """

import argparse
import multiprocessing
import os
import shutil
import subprocess

import bm_constants


def _args():
    argp = argparse.ArgumentParser(description="Builds microbenchmarks")
    argp.add_argument(
        "-b",
        "--benchmarks",
        nargs="+",
        choices=bm_constants._AVAILABLE_BENCHMARK_TESTS,
        default=bm_constants._AVAILABLE_BENCHMARK_TESTS,
        help="Which benchmarks to build",
    )
    argp.add_argument(
        "-j",
        "--jobs",
        type=int,
        default=multiprocessing.cpu_count(),
        help=(
            "Deprecated. Bazel chooses number of CPUs to build with"
            " automatically."
        ),
    )
    argp.add_argument(
        "-n",
        "--name",
        type=str,
        help=(
            "Unique name of this build. To be used as a handle to pass to the"
            " other bm* scripts"
        ),
    )
    args = argp.parse_args()
    assert args.name
    return args


def _build_cmd(cfg, benchmarks):
    bazel_targets = [
        "//test/cpp/microbenchmarks:%s" % benchmark for benchmark in benchmarks
    ]
    # --dynamic_mode=off makes sure that we get a monolithic binary that can be safely
    # moved outside of the bazel-bin directory
    return [
        "tools/bazel",
        "build",
        "--config=%s" % cfg,
        "--dynamic_mode=off",
    ] + bazel_targets


def _build_config_and_copy(cfg, benchmarks, dest_dir):
    """Build given config and copy resulting binaries to dest_dir/CONFIG"""
    subprocess.check_call(_build_cmd(cfg, benchmarks))
    cfg_dir = dest_dir + "/%s" % cfg
    os.makedirs(cfg_dir)
    subprocess.check_call(
        ["cp"]
        + [
            "bazel-bin/test/cpp/microbenchmarks/%s" % benchmark
            for benchmark in benchmarks
        ]
        + [cfg_dir]
    )


def build(name, benchmarks, jobs):
    dest_dir = "bm_diff_%s" % name
    shutil.rmtree(dest_dir, ignore_errors=True)
    _build_config_and_copy("opt", benchmarks, dest_dir)


if __name__ == "__main__":
    args = _args()
    build(args.name, args.benchmarks, args.jobs)

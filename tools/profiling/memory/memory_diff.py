#!/usr/bin/env python3
#
# Copyright 2022 gRPC authors.
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

import argparse
import csv
import glob
import math
import multiprocessing
import os
import pathlib
import re
import shutil
import subprocess
import sys

sys.path.append(
    os.path.join(
        os.path.dirname(sys.argv[0]), "..", "..", "run_tests", "python_utils"
    )
)
import check_on_pr

argp = argparse.ArgumentParser(description="Perform diff on memory benchmarks")

argp.add_argument(
    "-d",
    "--diff_base",
    type=str,
    help="Commit or branch to compare the current one to",
)

argp.add_argument("-j", "--jobs", type=int, default=multiprocessing.cpu_count())

args = argp.parse_args()

_INTERESTING = {
    "call/client": (
        rb"client call memory usage: ([0-9\.]+) bytes per call",
        float,
    ),
    "call/server": (
        rb"server call memory usage: ([0-9\.]+) bytes per call",
        float,
    ),
    "channel/client": (
        rb"client channel memory usage: ([0-9\.]+) bytes per channel",
        float,
    ),
    "channel/server": (
        rb"server channel memory usage: ([0-9\.]+) bytes per channel",
        float,
    ),
    "call/xds_client": (
        rb"xds client call memory usage: ([0-9\.]+) bytes per call",
        float,
    ),
    "call/xds_server": (
        rb"xds server call memory usage: ([0-9\.]+) bytes per call",
        float,
    ),
    "channel/xds_client": (
        rb"xds client channel memory usage: ([0-9\.]+) bytes per channel",
        float,
    ),
    "channel/xds_server": (
        rb"xds server channel memory usage: ([0-9\.]+) bytes per channel",
        float,
    ),
    "channel_multi_address/xds_client": (
        rb"xds multi_address client channel memory usage: ([0-9\.]+) bytes per channel",
        float,
    ),
}

_SCENARIOS = {
    "default": [],
    "minstack": ["--scenario_config=minstack"],
    "chaotic_good": ["--scenario_config=chaotic_good"],
}

_BENCHMARKS = {
    "call": ["--benchmark_names=call", "--size=50000"],
    "channel": ["--benchmark_names=channel", "--size=10000"],
    "channel_multi_address": [
        "--benchmark_names=channel_multi_address",
        "--size=10000",
    ],
}


def _run():
    """Build with Bazel, then run, and extract interesting lines from the output."""
    subprocess.check_call(
        [
            "tools/bazel",
            "build",
            "-c",
            "opt",
            "test/core/memory_usage/memory_usage_test",
        ]
    )
    ret = {}
    for name, benchmark_args in _BENCHMARKS.items():
        for use_xds in (False, True):
            for scenario, extra_args in _SCENARIOS.items():
                # TODO(chenancy) Remove when minstack is implemented for channel
                if name == "channel" and scenario == "minstack":
                    continue
                if name == "channel_multi_address" and not use_xds:
                    continue
                argv = (
                    ["bazel-bin/test/core/memory_usage/memory_usage_test"]
                    + benchmark_args
                    + extra_args
                )
                if use_xds:
                    argv.append("--use_xds")
                try:
                    output = subprocess.check_output(argv)
                except subprocess.CalledProcessError as e:
                    print("Error running benchmark:", e)
                    continue
                for line in output.splitlines():
                    for key, (pattern, conversion) in _INTERESTING.items():
                        m = re.match(pattern, line)
                        if m:
                            ret[scenario + ": " + key] = conversion(m.group(1))
    return ret


cur = _run()
old = None

if args.diff_base:
    where_am_i = (
        subprocess.check_output(["git", "rev-parse", "--abbrev-ref", "HEAD"])
        .decode()
        .strip()
    )
    # checkout the diff base (="old")
    subprocess.check_call(["git", "checkout", args.diff_base])
    try:
        old = _run()
    finally:
        # restore the original revision (="cur")
        subprocess.check_call(["git", "checkout", where_am_i])

text = ""
if old is None:
    print(cur)
    for key, value in sorted(cur.items()):
        text += "{}: {}\n".format(key, value)
else:
    print(cur, old)
    call_decrease = 0
    call_increase = 0
    channel_decrease = 0
    channel_increase = 0
    call_significant = 200
    channel_significant = 1000
    for scenario in _SCENARIOS.keys():
        for key, value in sorted(_INTERESTING.items()):
            key = scenario + ": " + key
            if key in cur:
                if key not in old:
                    text += "{}: {}\n".format(key, cur[key])
                else:
                    text += "{}: {} -> {}\n".format(key, old[key], cur[key])
                    diff = cur[key] - old[key]
                    if "call" in key:
                        if diff < -call_significant:
                            call_decrease += 1
                        elif diff > call_significant:
                            call_increase += 1
                    else:
                        if diff < -channel_significant:
                            channel_decrease += 1
                        elif diff > channel_significant:
                            channel_increase += 1

    print("CALL: %d increases, %d decreases" % (call_increase, call_decrease))
    print(
        "CHANNEL: %d increases, %d decreases"
        % (channel_increase, channel_decrease)
    )
    # if anything increased ==> label it an increase
    # otherwise if anything decreased (and nothing increased) ==> label it a decrease
    # otherwise label it neutral
    # -- this biases reporting towards increases, which is what we want to act on
    #    most regularly
    if call_increase > 0:
        check_on_pr.label_increase_decrease_on_pr("per-call-memory", 1)
    elif call_decrease > 0:
        check_on_pr.label_increase_decrease_on_pr("per-call-memory", -1)
    else:
        check_on_pr.label_increase_decrease_on_pr("per-call-memory", 0)
    if channel_increase > 0:
        check_on_pr.label_increase_decrease_on_pr("per-channel-memory", 1)
    elif channel_decrease > 0:
        check_on_pr.label_increase_decrease_on_pr("per-channel-memory", -1)
    else:
        check_on_pr.label_increase_decrease_on_pr("per-channel-memory", 0)

print(text)
check_on_pr.check_on_pr("Memory Difference", "```\n%s\n```" % text)

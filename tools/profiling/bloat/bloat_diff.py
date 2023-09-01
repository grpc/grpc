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

import argparse
import csv
import glob
import math
import multiprocessing
import os
import pathlib
import shutil
import subprocess
import sys

sys.path.append(
    os.path.join(
        os.path.dirname(sys.argv[0]), "..", "..", "run_tests", "python_utils"
    )
)
import check_on_pr

argp = argparse.ArgumentParser(description="Perform diff on microbenchmarks")

argp.add_argument(
    "-d",
    "--diff_base",
    type=str,
    help="Commit or branch to compare the current one to",
)

argp.add_argument("-j", "--jobs", type=int, default=multiprocessing.cpu_count())

args = argp.parse_args()

# the libraries for which check bloat difference is calculated
LIBS = [
    "libgrpc.so",
    "libgrpc++.so",
]


def _build(output_dir):
    """Perform the cmake build under the output_dir."""
    shutil.rmtree(output_dir, ignore_errors=True)
    subprocess.check_call("mkdir -p %s" % output_dir, shell=True, cwd=".")
    subprocess.check_call(
        [
            "cmake",
            "-DgRPC_BUILD_TESTS=OFF",
            "-DBUILD_SHARED_LIBS=ON",
            "-DCMAKE_BUILD_TYPE=RelWithDebInfo",
            '-DCMAKE_C_FLAGS="-gsplit-dwarf"',
            '-DCMAKE_CXX_FLAGS="-gsplit-dwarf"',
            "..",
        ],
        cwd=output_dir,
    )
    subprocess.check_call("make -j%d" % args.jobs, shell=True, cwd=output_dir)


def _rank_diff_bytes(diff_bytes):
    """Determine how significant diff_bytes is, and return a simple integer representing that"""
    mul = 1
    if diff_bytes < 0:
        mul = -1
        diff_bytes = -diff_bytes
    if diff_bytes < 2 * 1024:
        return 0
    if diff_bytes < 16 * 1024:
        return 1 * mul
    if diff_bytes < 128 * 1024:
        return 2 * mul
    return 3 * mul


_build("bloat_diff_new")

if args.diff_base:
    where_am_i = (
        subprocess.check_output(["git", "rev-parse", "--abbrev-ref", "HEAD"])
        .decode()
        .strip()
    )
    # checkout the diff base (="old")
    subprocess.check_call(["git", "checkout", args.diff_base])
    subprocess.check_call(["git", "submodule", "update"])
    try:
        _build("bloat_diff_old")
    finally:
        # restore the original revision (="new")
        subprocess.check_call(["git", "checkout", where_am_i])
        subprocess.check_call(["git", "submodule", "update"])

pathlib.Path("bloaty-build").mkdir(exist_ok=True)
subprocess.check_call(
    ["cmake", "-G", "Unix Makefiles", "../third_party/bloaty"],
    cwd="bloaty-build",
)
subprocess.check_call("make -j%d" % args.jobs, shell=True, cwd="bloaty-build")

text = ""
diff_size = 0
for lib in LIBS:
    text += (
        "****************************************************************\n\n"
    )
    text += lib + "\n\n"
    old_version = glob.glob("bloat_diff_old/%s" % lib)
    new_version = glob.glob("bloat_diff_new/%s" % lib)
    for filename in [old_version, new_version]:
        if filename:
            subprocess.check_call(
                "strip %s -o %s.stripped" % (filename[0], filename[0]),
                shell=True,
            )
    assert len(new_version) == 1
    cmd = "bloaty-build/bloaty -d compileunits,symbols"
    if old_version:
        assert len(old_version) == 1
        text += subprocess.check_output(
            "%s -n 0 --debug-file=%s --debug-file=%s %s.stripped -- %s.stripped"
            % (
                cmd,
                new_version[0],
                old_version[0],
                new_version[0],
                old_version[0],
            ),
            shell=True,
        ).decode()
        sections = [
            x
            for x in csv.reader(
                subprocess.check_output(
                    "bloaty-build/bloaty -n 0 --csv %s -- %s"
                    % (new_version[0], old_version[0]),
                    shell=True,
                )
                .decode()
                .splitlines()
            )
        ]
        print(sections)
        for section in sections[1:]:
            # skip debug sections for bloat severity calculation
            if section[0].startswith(".debug"):
                continue
            # skip dynamic loader sections too
            if section[0].startswith(".dyn"):
                continue
            diff_size += int(section[2])
    else:
        text += subprocess.check_output(
            "%s %s.stripped -n 0 --debug-file=%s"
            % (cmd, new_version[0], new_version[0]),
            shell=True,
        ).decode()
    text += "\n\n"

severity = _rank_diff_bytes(diff_size)
print("SEVERITY: %d" % severity)

print(text)
check_on_pr.check_on_pr("Bloat Difference", "```\n%s\n```" % text)
check_on_pr.label_significance_on_pr("bloat", severity)

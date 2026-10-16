#!/usr/bin/env python3
#
# Copyright 2018 gRPC authors.
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
import glob
import multiprocessing
import os
import shutil
import subprocess
import sys

from parse_link_map import parse_link_map

sys.path.append(
    os.path.join(
        os.path.dirname(sys.argv[0]), "..", "..", "run_tests", "python_utils"
    )
)
import check_on_pr

# Only show diff 1KB or greater
_DIFF_THRESHOLD = 1000

_SIZE_LABELS = ("Core", "ObjC", "BoringSSL", "Protobuf", "Total")

argp = argparse.ArgumentParser(
    description="Binary size diff of gRPC Objective-C sample"
)

argp.add_argument(
    "-d",
    "--diff_base",
    type=str,
    help="Commit or branch to compare the current one to",
)

args = argp.parse_args()


def dir_size(dir):
    total = 0
    for dirpath, dirnames, filenames in os.walk(dir):
        for f in filenames:
            fp = os.path.join(dirpath, f)
            total += os.stat(fp).st_size
    return total


def get_size(where):
    build_dir = "src/objective-c/examples/Sample/Build/Build-%s/" % where
    link_map_filename = "Build/Intermediates.noindex/Sample.build/Release-iphoneos/Sample.build/Sample-LinkMap-normal-arm64.txt"
    # IMPORTANT: order needs to match labels in _SIZE_LABELS
    return parse_link_map(build_dir + link_map_filename)


def build(where):
    subprocess.check_call(["make", "clean"])
    shutil.rmtree(
        "src/objective-c/examples/Sample/Build/Build-%s" % where,
        ignore_errors=True,
    )
    subprocess.check_call(
        (
            "CONFIG=opt EXAMPLE_PATH=src/objective-c/examples/Sample"
            " SCHEME=Sample ./build_one_example.sh"
        ),
        shell=True,
        cwd="src/objective-c/tests",
    )
    os.rename(
        "src/objective-c/examples/Sample/Build/Build",
        "src/objective-c/examples/Sample/Build/Build-%s" % where,
    )


def _render_row(new, label, old):
    """Render row in 3-column output format."""
    try:
        formatted_new = "{:,}".format(int(new))
    except:
        formatted_new = new
    try:
        formatted_old = "{:,}".format(int(old))
    except:
        formatted_old = old
    return "{:>15}{:>15}{:>15}\n".format(formatted_new, label, formatted_old)


def _diff_sign(new, old, diff_threshold=None):
    """Generate diff sign based on values"""
    diff_sign = " "
    if (
        diff_threshold is not None
        and abs(new_size[i] - old_size[i]) >= diff_threshold
    ):
        diff_sign += "!"
    if new > old:
        diff_sign += "(>)"
    elif new < old:
        diff_sign += "(<)"
    else:
        diff_sign += "(=)"
    return diff_sign


text = "Objective-C binary sizes\n"

build("new")
new_size = get_size("new")
old_size = None

if args.diff_base:
    old = "old"
    where_am_i = (
        subprocess.check_output(["git", "rev-parse", "--abbrev-ref", "HEAD"])
        .decode()
        .strip()
    )
    subprocess.check_call(["git", "checkout", "--", "."])
    subprocess.check_call(["git", "checkout", args.diff_base])
    subprocess.check_call(["git", "submodule", "update", "--force"])
    try:
        build("old")
        old_size = get_size("old")
    finally:
        subprocess.check_call(["git", "checkout", "--", "."])
        subprocess.check_call(["git", "checkout", where_am_i])
        subprocess.check_call(["git", "submodule", "update", "--force"])

text += "**********************STATIC******************\n"
text += _render_row("New size", "", "Old size")
if old_size == None:
    for i in range(0, len(_SIZE_LABELS)):
        if i == len(_SIZE_LABELS) - 1:
            # skip line before rendering "Total"
            text += "\n"
        text += _render_row(new_size[i], _SIZE_LABELS[i], "")
else:
    has_diff = False
    # go through all labels but "Total"
    for i in range(0, len(_SIZE_LABELS) - 1):
        if abs(new_size[i] - old_size[i]) >= _DIFF_THRESHOLD:
            has_diff = True
        diff_sign = _diff_sign(
            new_size[i], old_size[i], diff_threshold=_DIFF_THRESHOLD
        )
        text += _render_row(
            new_size[i], _SIZE_LABELS[i] + diff_sign, old_size[i]
        )

    # render the "Total"
    i = len(_SIZE_LABELS) - 1
    diff_sign = _diff_sign(new_size[i], old_size[i])
    # skip line before rendering "Total"
    text += "\n"
    text += _render_row(new_size[i], _SIZE_LABELS[i] + diff_sign, old_size[i])
    if not has_diff:
        text += "\n No significant differences in binary sizes\n"
text += "\n"

print(text)

check_on_pr.check_on_pr("ObjC Binary Size", "```\n%s\n```" % text)

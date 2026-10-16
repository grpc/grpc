#!/usr/bin/env python3

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

import os
import re
import subprocess
import sys

from google.cloud import bigquery
import run_buildozer
import update_flakes_query

lookback_hours = 24 * 7 * 4


def include_test(test):
    if "@" in test:
        return False
    if test.startswith("//test/cpp/qps:"):
        return False
    return True


TEST_DIRS = ["test/core", "test/cpp"]
tests = {}
already_flaky = set()
for test_dir in TEST_DIRS:
    for line in subprocess.check_output(
        ["bazel", "query", "tests({}/...)".format(test_dir)]
    ).splitlines():
        test = line.strip().decode("utf-8")
        if not include_test(test):
            continue
        tests[test] = False
for test_dir in TEST_DIRS:
    for line in subprocess.check_output(
        ["bazel", "query", "attr(flaky, 1, tests({}/...))".format(test_dir)]
    ).splitlines():
        test = line.strip().decode("utf-8")
        if not include_test(test):
            continue
        already_flaky.add(test)

flaky_e2e = set()

client = bigquery.Client()
for row in client.query(
    update_flakes_query.QUERY.format(lookback_hours=lookback_hours)
).result():
    if "/macos/" in row.job_name:
        continue  # we know mac stuff is flaky
    if row.test_binary not in tests:
        m = re.match(
            r"^//test/core/end2end:([^@]*)@([^@]*)(.*)", row.test_binary
        )
        if m:
            flaky_e2e.add("{}@{}{}".format(m.group(1), m.group(2), m.group(3)))
            print("will mark end2end test {} as flaky".format(row.test_binary))
        else:
            print("skip obsolete test {}".format(row.test_binary))
        continue
    print("will mark {} as flaky".format(row.test_binary))
    tests[row.test_binary] = True

buildozer_commands = []
for test, flaky in sorted(tests.items()):
    if flaky:
        buildozer_commands.append("set flaky True|{}".format(test))
    elif test in already_flaky:
        buildozer_commands.append("remove flaky|{}".format(test))

with open("test/core/end2end/flaky.bzl", "w") as f:
    with open(sys.argv[0]) as my_source:
        for line in my_source:
            if line[0] != "#":
                break
        for line in my_source:
            if line[0] == "#":
                print(line.strip(), file=f)
                break
        for line in my_source:
            if line[0] != "#":
                break
            print(line.strip(), file=f)
    print(
        (
            '"""A list of flaky tests, consumed by generate_tests.bzl to set'
            ' flaky attrs."""'
        ),
        file=f,
    )
    print("FLAKY_TESTS = [", file=f)
    for line in sorted(list(flaky_e2e)):
        print('    "{}",'.format(line), file=f)
    print("]", file=f)

run_buildozer.run_buildozer(buildozer_commands)

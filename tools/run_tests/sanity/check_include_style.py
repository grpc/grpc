#!/usr/bin/env python3

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

import os
import re
import sys

os.chdir(os.path.join(os.path.dirname(sys.argv[0]), "../../.."))

BAD_REGEXES = [
    (r'\n#include "include/(.*)"', r"\n#include <\1>"),
    (r'\n#include "grpc(.*)"', r"\n#include <grpc\1>"),
    (r"\n#include <gtest(.*)>", r'\n#include "gtest\1"'),
    (r"\n#include <gmock(.*)>", r'\n#include "gmock\1"'),
]

fix = sys.argv[1:] == ["--fix"]
if fix:
    print("FIXING!")


def check_include_style(directory_root):
    bad_files = []
    for root, dirs, files in os.walk(directory_root):
        for filename in files:
            path = os.path.join(root, filename)
            if os.path.splitext(path)[1] not in [".c", ".cc", ".h"]:
                continue
            if filename.endswith(".pb.h") or filename.endswith(".pb.c"):
                continue
            # Skip check for upb generated code.
            if (
                filename.endswith(".upb.h")
                or filename.endswith(".upbdefs.h")
                or filename.endswith(".upbdefs.c")
            ):
                continue
            with open(path) as f:
                text = f.read()
            original = text
            for regex, replace in BAD_REGEXES:
                text = re.sub(regex, replace, text)
            if text != original:
                bad_lines = []
                for line_number, (original_line, text_line) in enumerate(
                    zip(original.splitlines(), text.splitlines())
                ):
                    if original_line != text_line:
                        bad_lines.append(
                            (line_number + 1, original_line, text_line)
                        )
                bad_files.append((path, bad_lines))
                if fix:
                    with open(path, "w") as f:
                        f.write(text)
    return bad_files


all_bad_files = []
all_bad_files += check_include_style(os.path.join("src", "core"))
all_bad_files += check_include_style(os.path.join("src", "cpp"))
all_bad_files += check_include_style(os.path.join("test", "core"))
all_bad_files += check_include_style(os.path.join("test", "cpp"))
all_bad_files += check_include_style(os.path.join("include", "grpc"))
all_bad_files += check_include_style(os.path.join("include", "grpcpp"))

if all_bad_files:
    for f, bad_lines in all_bad_files:
        print("%s has badly formed grpc system header files:" % f)
        for line_number, original_line, text_line in bad_lines:
            print(
                "\tline %d: %s -> %s" % (line_number, original_line, text_line)
            )
    sys.exit(1)

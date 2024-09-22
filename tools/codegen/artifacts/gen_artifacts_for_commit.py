#!/usr/bin/env python3

# Copyright 2024 gRPC authors.
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

import shutil
import subprocess

build_targets = []
generated_files = []


def flatten(fq_name):
    return fq_name.removeprefix("//").replace(":", "/")


def genrule(query_path, name, outs, **kwargs):
    global build_targets
    global generated_files
    build_targets.append(f"{query_path}:{name}")
    generated_files.extend(flatten(out) for out in outs)


source = ""
for query_path in ["//src/core"]:
    source = f"# {query_path}\n"
    cmd = [
        "bazel",
        "query",
        "--output=build",
        f"attr('tags', '\\bgrpc_generated_artifact\\b', {query_path}/...)",
    ]
    source += subprocess.check_output(cmd).decode("utf-8")
    source += "\n"

    exec(
        source,
        {
            "genrule": lambda **kwargs: genrule(
                query_path=query_path, **kwargs
            ),
        },
        {},
    )

cmd = ["bazel", "build"] + build_targets
subprocess.check_call(cmd)

for file in generated_files:
    shutil.copy(f"bazel-bin/{file}", f"{file}")

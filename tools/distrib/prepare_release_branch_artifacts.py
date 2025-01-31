#!/usr/bin/env python3

# Copyright 2025 gRPC authors.
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
import subprocess
import sys

_ROOT = os.path.abspath(os.path.join(os.path.dirname(sys.argv[0]), "../.."))
os.chdir(_ROOT)

subprocess.check("./generate_artifacts.sh", shell=True)

lines = []
skipping = False
with open(".gitignore") as f:
    for line in f:
        if line.startswith("# BEGIN CODEGEN"):
            skipping = True
        elif line.startswith("# END CODEGEN"):
            skipping = False
        elif not skipping:
            lines.append(line)
with open(".gitignore", "w") as f:
    f.write("".join(lines))


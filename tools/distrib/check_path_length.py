#!/usr/bin/env python3

# Copyright 2023 gRPC authors.
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

# Maximum path length for a path in the repository before we start seeing
# problems with Windows cloning the repository. (kind of arbitrary, less than
# Windows actual limit, but enough that we avoid problems).
maxlen = 150

errors = 0
for path in subprocess.check_output(["git", "ls-files"]).decode().splitlines():
    if len(path) > maxlen:
        print(f"Path too long: {path}")
        errors += 1

if errors:
    print(f"Found {errors} files with paths longer than {maxlen} characters")
    exit(1)

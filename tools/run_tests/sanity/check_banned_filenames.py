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
import sys

BANNED_FILENAMES = [
    "BUILD.gn",
]

os.chdir(os.path.join(os.path.dirname(sys.argv[0]), "../../.."))

bad = []
for filename in BANNED_FILENAMES:
    if os.path.exists(filename):
        bad.append(filename)

if bad:
    for file in bad:
        print("%s should not exist" % file)
    sys.exit(1)

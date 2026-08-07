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
import sys

os.chdir(os.path.join(os.path.dirname(sys.argv[0]), "../../.."))

# Allowance for overrides for specific files
EXPECTED_NAMES = {
    "src/proto/grpc/channelz": "channelz",
    "src/proto/grpc/status": "status",
    "src/proto/grpc/testing": "testing",
    "src/proto/grpc/testing/duplicate": "duplicate",
    "src/proto/grpc/lb/v1": "lb",
    "src/proto/grpc/testing/xds": "xds",
    "src/proto/grpc/testing/xds/v3": "xds_v3",
    "src/proto/grpc/core": "core",
    "src/proto/grpc/health/v1": "health",
    "src/proto/grpc/reflection/v1alpha": "reflection",
    "src/proto/grpc/reflection/v1": "reflection_v1",
}

errors = 0
for root, dirs, files in os.walk("."):
    if root.startswith("./"):
        root = root[len("./") :]
    # don't check third party
    if root.startswith("third_party/"):
        continue
    # only check BUILD files
    if "BUILD" not in files:
        continue
    text = open("%s/BUILD" % root).read()
    # find a grpc_package clause
    pkg_start = text.find("grpc_package(")
    if pkg_start == -1:
        continue
    # parse it, taking into account nested parens
    pkg_end = pkg_start + len("grpc_package(")
    level = 1
    while level == 1:
        if text[pkg_end] == ")":
            level -= 1
        elif text[pkg_end] == "(":
            level += 1
        pkg_end += 1
    # it's a python statement, so evaluate it to pull out the name of the package
    name = eval(
        text[pkg_start:pkg_end], {"grpc_package": lambda name, **kwargs: name}
    )
    # the name should be the path within the source tree, excepting some special
    # BUILD files (really we should normalize them too at some point)
    # TODO(ctiller): normalize all package names
    expected_name = EXPECTED_NAMES.get(root, root)
    if name != expected_name:
        print(
            "%s/BUILD should define a grpc_package with name=%r, not %r"
            % (root, expected_name, name)
        )
        errors += 1

if errors != 0:
    sys.exit(1)

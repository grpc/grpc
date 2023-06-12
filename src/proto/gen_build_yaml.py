#!/usr/bin/env python2.7
# Copyright 2015 gRPC authors.
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
"""Generates the appropriate build.json data for all the proto files."""

import yaml
import collections
import os
import re
import sys


def update_deps(key, proto_filename, deps, deps_external, is_trans, visited):
    if not proto_filename in visited:
        visited.append(proto_filename)
        with open(proto_filename) as inp:
            for line in inp:
                imp = re.search(r'import "([^"]*)"', line)
                if not imp:
                    continue
                imp_proto = imp.group(1)
                # This indicates an external dependency, which we should handle
                # differently and not traverse recursively
                if imp_proto.startswith("google/"):
                    if key not in deps_external:
                        deps_external[key] = []
                    deps_external[key].append(imp_proto[:-6])
                    continue
                # In case that the path is changed by copybara,
                # revert the change to avoid file error.
                if imp_proto.startswith("third_party/grpc"):
                    imp_proto = imp_proto[17:]
                if key not in deps:
                    deps[key] = []
                deps[key].append(imp_proto[:-6])
                if is_trans:
                    update_deps(
                        key, imp_proto, deps, deps_external, is_trans, visited
                    )


def main():
    proto_dir = os.path.abspath(os.path.dirname(sys.argv[0]))
    os.chdir(os.path.join(proto_dir, "../.."))

    deps = {}
    deps_trans = {}
    deps_external = {}
    deps_external_trans = {}
    for root, dirs, files in os.walk("src/proto"):
        for f in files:
            if f[-6:] != ".proto":
                continue
            look_at = os.path.join(root, f)
            deps_for = look_at[:-6]
            # First level deps
            update_deps(deps_for, look_at, deps, deps_external, False, [])
            # Transitive deps
            update_deps(
                deps_for, look_at, deps_trans, deps_external_trans, True, []
            )

    json = {
        "proto_deps": deps,
        "proto_transitive_deps": deps_trans,
        "proto_external_deps": deps_external,
        "proto_transitive_external_deps": deps_external_trans,
    }

    print(yaml.dump(json))


if __name__ == "__main__":
    main()

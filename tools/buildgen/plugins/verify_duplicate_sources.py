# Copyright 2020 gRPC authors.
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
"""Buildgen duplicate source validation plugin."""


def mako_plugin(dictionary):
    """The exported plugin code for verify_duplicate_sources.

    This validates that a certain set of libraries don't contain
    duplicate source files which may cause One Definition Rule (ODR)
    violation.
    """
    errors = []
    target_groups = (
        ("gpr", "grpc", "grpc++"),
        ("gpr", "grpc_unsecure", "grpc++_unsecure"),
    )
    lib_map = {lib["name"]: lib for lib in dictionary.get("libs")}
    for target_group in target_groups:
        src_map = {}
        for target in target_group:
            for src in lib_map[target]["src"]:
                if src.endswith(".cc"):
                    if src in src_map:
                        errors.append(
                            "Source {0} is used in both {1} and {2}".format(
                                src, src_map[src], target
                            )
                        )
                    else:
                        src_map[src] = target
    if errors:
        raise Exception("\n".join(errors))

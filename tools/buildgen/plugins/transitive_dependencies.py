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
"""Buildgen transitive dependencies

This takes the list of libs, node_modules, and targets from our
yaml dictionary, and adds to each the transitive closure
of the list of dependencies.
"""


def transitive_deps(lib_map, node):
    """Returns a list of transitive dependencies from node.

    Recursively iterate all dependent node in a depth-first fashion and
    list a result using a topological sorting.
    """
    result = []
    seen = set()
    start = node

    def recursive_helper(node):
        for dep in node.get("deps", []):
            if dep not in seen:
                seen.add(dep)
                next_node = lib_map.get(dep)
                if next_node:
                    recursive_helper(next_node)
                else:
                    # For some deps, the corrensponding library entry doesn't exist,
                    # but we still want to preserve the dependency so that the build
                    # system can provide custom handling for that depdendency.
                    result.append(dep)
        if node is not start:
            result.insert(0, node["name"])

    recursive_helper(node)
    return result


def mako_plugin(dictionary):
    """The exported plugin code for transitive_dependencies.

    Iterate over each list and check each item for a deps list. We add a
    transitive_deps property to each with the transitive closure of those
    dependency lists. The result list is sorted in a topological ordering.
    """
    lib_map = {lib["name"]: lib for lib in dictionary.get("libs")}

    for target_name, target_list in list(dictionary.items()):
        for target in target_list:
            if isinstance(target, dict):
                if "deps" in target or target_name == "libs":
                    if not "deps" in target:
                        # make sure all the libs have the "deps" field populated
                        target["deps"] = []
                    target["transitive_deps"] = transitive_deps(lib_map, target)

    python_dependencies = dictionary.get("python_dependencies")
    python_dependencies["transitive_deps"] = transitive_deps(
        lib_map, python_dependencies
    )

#!/usr/bin/env python3

# Copyright 2025 The gRPC Authors
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

# Utilities for extracting repositories using Bazel mod command

import json
import re
import subprocess
from typing import List, Set


def _transitive_deps(dep) -> dict[str, str]:
    deps_set = {dep["apparentName"]: dep["key"]}
    if "dependencies" in dep:
        for d in dep["dependencies"]:
            deps_set.update(_transitive_deps(d))
    return deps_set


def _parse_property_value(value: str) -> str | List[str]:
    # String value - quoted, with optional comma
    if match := re.match(r"^\"(.*)\",?$", value):
        return match[1]
    # Array value - square brackets, possible comma at the end
    if match := re.match(r"^\[(.*)\],?$", value):
        return [str(_parse_property_value(v)) for v in match[1].split(", ")]
    raise RuntimeError(f"Unparsable {value}")


def get_dependencies_json(
    dependencies: Set[str],
) -> dict[str, dict[str, str | List[str]]]:
    # 1. We need to get list of dependencies with the aliases used in gRPC
    deps = json.loads(
        subprocess.check_output(
            ["tools/bazel", "mod", "deps", "<root>", "--output", "json"]
        )
    )
    # 2. Find the dependencies we are interested in
    modules = {
        v: k for k, v in _transitive_deps(deps).items() if k in dependencies
    }
    # 3. Get repositories for the dependencies
    # Note: even "JSON" output of this command is actually Starlark. Adding
    # the `--output` switch so we detect if that is ever fixed.
    repositories = subprocess.check_output(
        ["tools/bazel", "mod", "show_repo", *modules.keys(), "--output", "json"]
    ).decode("utf-8")
    ignored_attributes: set[str] = set()
    result: dict[str, dict[str, str | List[str]]] = {}
    repo: dict[str, str | List[str]] | None = None
    for line in repositories.split("\n"):
        if match := re.match(r"^## (.*):$", line):
            if repo != None:
                raise RuntimeError("Unterminated repository: " + repositories)
            repo = {"module": match[1], "alias": modules[match[1]]}
            continue
        if not line or line.startswith("#") or line == "http_archive(":
            continue
        if repo == None:
            raise RuntimeError(
                f"Not parsing repository when encountered {line}"
            )
        if line == ")":
            result[str(repo["alias"])] = repo
            repo = None
            continue
        if match := re.match(r"^  (\w*) = (.*)$", line):
            if match[1] in {
                "integrity",
                "name",
                "sha256",
                "strip_prefix",
                "urls",
            }:
                repo[match[1]] = _parse_property_value(match[2])
            elif match[1] == "url":
                repo["urls"] = [str(_parse_property_value(match[2]))]
            else:
                ignored_attributes.add(match[1])
        else:
            raise RuntimeError(f"Unable to parse {line}")

    if repo != None:
        raise RuntimeError(f"Unterminated repo {repo}")
    print("Ignored attributes: " + ", ".join(sorted(ignored_attributes)))
    return result


if __name__ == "__main__":
    print(
        "\n\n".join(
            sorted(
                [
                    f"{name}:\n"
                    + "\n".join([f"  {k} = {v}" for k, v in dep.items()])
                    for name, dep in get_dependencies_json(
                        {
                            "com_google_googleapis",
                            "com_github_cncf_xds",
                            "com_envoyproxy_protoc_gen_validate",
                            "envoy_api",
                            "opencensus_proto",
                        }
                    ).items()
                ]
            )
        )
    )

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


def transitive_deps(dep) -> Set[str]:
    deps_set = {dep["name"]}
    if "dependencies" in dep:
        deps_set.update(set().union(*(transitive_deps(d) for d in dep["dependencies"])))
    return deps_set


def parse_value(value: str) -> str | List[str]:
    # String value - quoted, with optional comma
    if match := re.match(r"^\"(.*)\",?$", value):
        return match[1]
    # Array value - square brackets, possible comma at the end
    if match := re.match(r"^\[(.*)\],?$", value):
        return [str(parse_value(v)) for v in match[1].split(", ")]
    raise RuntimeError(f"Unparsable {value}")


def get_dependencies_json() -> list[dict[str, str | List[str]]]:
    deps = json.loads(
        subprocess.check_output(
            ["tools/bazel", "mod", "deps", "<root>", "--output", "json"]
        )
    )
    modules = sorted(transitive_deps(deps) - {"<root>", "grpc"})
    repositories = subprocess.check_output(
        ["tools/bazel", "mod", "show_repo", *modules, "--output", "json"]
    ).decode("utf-8")
    ignored_attributes: set[str] = set()
    repos: list[dict[str, str | List[str]]] = []
    repo: dict[str, str | List[str]] | None = None
    for line in repositories.split("\n"):
        if not line or line.startswith("#"):
            continue
        if line == "http_archive(":
            if repo != None:
                raise RuntimeError("Unterminated repository: " + repositories)
            repo = {}
            continue
        if repo == None:
            raise RuntimeError(f"Not parsing repository when encountered {line}")
        if line == ")":
            repos.append(repo)
            repo = None
            continue
        if match := re.match(r"^  (\w*) = (.*)$", line):
            if match[1] in {"integrity", "name", "sha256", "strip_prefix", "urls"}:
                repo[match[1]] = parse_value(match[2])
            elif match[1] == "url":
                repo["urls"] = [str(parse_value(match[2]))]
            else:
                ignored_attributes.add(match[1])
        else:
            raise RuntimeError(f"Unable to parse {line}")

    if repo != None:
        raise RuntimeError(f"Unterminated repo {repo}")
    print(f"Ignored attributes: {", ".join(sorted(ignored_attributes))}")
    return repos


if __name__ == "__main__":
    print(
        "\n\n".join(
            [
                "\n".join([f"{k} = {v}" for k, v in dep.items()])
                for dep in get_dependencies_json()
            ]
        )
    )

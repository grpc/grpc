#!/usr/bin/env python3

# Copyright 2019 gRPC authors.
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

import collections
import os
import re
import subprocess
import xml.etree.ElementTree as ET
import yaml

ABSEIL_PATH = "third_party/abseil-cpp"
OUTPUT_PATH = "src/abseil-cpp/preprocessed_builds.yaml"
CAPITAL_WORD = re.compile("[A-Z]+")
ABSEIL_CMAKE_RULE_BEGIN = re.compile("^absl_cc_.*\(", re.MULTILINE)
ABSEIL_CMAKE_RULE_END = re.compile("^\)", re.MULTILINE)

# Rule object representing the rule of Bazel BUILD.
Rule = collections.namedtuple(
    "Rule", "type name package srcs hdrs textual_hdrs deps visibility testonly"
)


def get_elem_value(elem, name):
    """Returns the value of XML element with the given name."""
    for child in elem:
        if child.attrib.get("name") == name:
            if child.tag == "string":
                return child.attrib.get("value")
            elif child.tag == "boolean":
                return child.attrib.get("value") == "true"
            elif child.tag == "list":
                return [
                    nested_child.attrib.get("value") for nested_child in child
                ]
            else:
                raise "Cannot recognize tag: " + child.tag
    return None


def normalize_paths(paths):
    """Returns the list of normalized path."""
    # e.g. ["//absl/strings:dir/header.h"] -> ["absl/strings/dir/header.h"]
    return [path.lstrip("/").replace(":", "/") for path in paths]


def parse_bazel_rule(elem, package):
    """Returns a rule from bazel XML rule."""
    return Rule(
        type=elem.attrib["class"],
        name=get_elem_value(elem, "name"),
        package=package,
        srcs=normalize_paths(get_elem_value(elem, "srcs") or []),
        hdrs=normalize_paths(get_elem_value(elem, "hdrs") or []),
        textual_hdrs=normalize_paths(
            get_elem_value(elem, "textual_hdrs") or []
        ),
        deps=get_elem_value(elem, "deps") or [],
        visibility=get_elem_value(elem, "visibility") or [],
        testonly=get_elem_value(elem, "testonly") or False,
    )


def read_bazel_build(package):
    """Runs bazel query on given package file and returns all cc rules."""
    # Use a wrapper version of bazel in gRPC not to use system-wide bazel
    # to avoid bazel conflict when running on Kokoro.
    BAZEL_BIN = "../../tools/bazel"
    result = subprocess.check_output(
        [BAZEL_BIN, "query", package + ":all", "--output", "xml"]
    )
    root = ET.fromstring(result)
    return [
        parse_bazel_rule(elem, package)
        for elem in root
        if elem.tag == "rule" and elem.attrib["class"].startswith("cc_")
    ]


def collect_bazel_rules(root_path):
    """Collects and returns all bazel rules from root path recursively."""
    rules = []
    for cur, _, _ in os.walk(root_path):
        build_path = os.path.join(cur, "BUILD.bazel")
        if os.path.exists(build_path):
            rules.extend(read_bazel_build("//" + cur))
    return rules


def parse_cmake_rule(rule, package):
    """Returns a rule from absl cmake rule.
    Reference: https://github.com/abseil/abseil-cpp/blob/master/CMake/AbseilHelpers.cmake
    """
    kv = {}
    bucket = None
    lines = rule.splitlines()
    for line in lines[1:-1]:
        if CAPITAL_WORD.match(line.strip()):
            bucket = kv.setdefault(line.strip(), [])
        else:
            if bucket is not None:
                bucket.append(line.strip())
            else:
                raise ValueError("Illegal syntax: {}".format(rule))
    return Rule(
        type=lines[0].rstrip("("),
        name="absl::" + kv["NAME"][0],
        package=package,
        srcs=[package + "/" + f.strip('"') for f in kv.get("SRCS", [])],
        hdrs=[package + "/" + f.strip('"') for f in kv.get("HDRS", [])],
        textual_hdrs=[],
        deps=kv.get("DEPS", []),
        visibility="PUBLIC" in kv,
        testonly="TESTONLY" in kv,
    )


def read_cmake_build(build_path, package):
    """Parses given CMakeLists.txt file and returns all cc rules."""
    rules = []
    with open(build_path, "r") as f:
        src = f.read()
        for begin_mo in ABSEIL_CMAKE_RULE_BEGIN.finditer(src):
            end_mo = ABSEIL_CMAKE_RULE_END.search(src[begin_mo.start(0) :])
            expr = src[
                begin_mo.start(0) : begin_mo.start(0) + end_mo.start(0) + 1
            ]
            rules.append(parse_cmake_rule(expr, package))
    return rules


def collect_cmake_rules(root_path):
    """Collects and returns all cmake rules from root path recursively."""
    rules = []
    for cur, _, _ in os.walk(root_path):
        build_path = os.path.join(cur, "CMakeLists.txt")
        if os.path.exists(build_path):
            rules.extend(read_cmake_build(build_path, cur))
    return rules


def pairing_bazel_and_cmake_rules(bazel_rules, cmake_rules):
    """Returns a pair map between bazel rules and cmake rules based on
    the similarity of the file list in the rule. This is because
    cmake build and bazel build of abseil are not identical.
    """
    pair_map = {}
    for rule in bazel_rules:
        best_crule, best_similarity = None, 0
        for crule in cmake_rules:
            similarity = len(
                set(rule.srcs + rule.hdrs + rule.textual_hdrs).intersection(
                    set(crule.srcs + crule.hdrs + crule.textual_hdrs)
                )
            )
            if similarity > best_similarity:
                best_crule, best_similarity = crule, similarity
        if best_crule:
            pair_map[(rule.package, rule.name)] = best_crule.name
    return pair_map


def resolve_hdrs(files):
    return [ABSEIL_PATH + "/" + f for f in files if f.endswith((".h", ".inc"))]


def resolve_srcs(files):
    return [ABSEIL_PATH + "/" + f for f in files if f.endswith(".cc")]


def resolve_deps(targets):
    return [(t[2:] if t.startswith("//") else t) for t in targets]


def generate_builds(root_path):
    """Generates builds from all BUILD files under absl directory."""
    bazel_rules = list(
        filter(
            lambda r: r.type == "cc_library" and not r.testonly,
            collect_bazel_rules(root_path),
        )
    )
    cmake_rules = list(
        filter(
            lambda r: r.type == "absl_cc_library" and not r.testonly,
            collect_cmake_rules(root_path),
        )
    )
    pair_map = pairing_bazel_and_cmake_rules(bazel_rules, cmake_rules)
    builds = []
    for rule in sorted(bazel_rules, key=lambda r: r.package[2:] + ":" + r.name):
        p = {
            "name": rule.package[2:] + ":" + rule.name,
            "cmake_target": pair_map.get((rule.package, rule.name)) or "",
            "headers": sorted(
                resolve_hdrs(rule.srcs + rule.hdrs + rule.textual_hdrs)
            ),
            "src": sorted(
                resolve_srcs(rule.srcs + rule.hdrs + rule.textual_hdrs)
            ),
            "deps": sorted(resolve_deps(rule.deps)),
        }
        builds.append(p)
    return builds


def main():
    previous_dir = os.getcwd()
    os.chdir(ABSEIL_PATH)
    builds = generate_builds("absl")
    os.chdir(previous_dir)
    with open(OUTPUT_PATH, "w") as outfile:
        outfile.write(yaml.dump(builds, indent=2))


if __name__ == "__main__":
    main()

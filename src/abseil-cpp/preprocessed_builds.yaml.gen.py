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
import yaml
import ast

ABSEIL_PATH = "third_party/abseil-cpp"
OUTPUT_PATH = "src/abseil-cpp/preprocessed_builds.yaml"
CAPITAL_WORD = re.compile("[A-Z]+")
ABSEIL_CMAKE_RULE_BEGIN = re.compile("^absl_cc_.*\(", re.MULTILINE)
ABSEIL_CMAKE_RULE_END = re.compile("^\)", re.MULTILINE)

# Rule object representing the rule of Bazel BUILD.
Rule = collections.namedtuple(
    "Rule", "type name package srcs hdrs textual_hdrs deps visibility testonly"
)


class SelectValue:
    def __init__(self, dict_val):
        self.dict_val = dict_val

    def __add__(self, other):
        if isinstance(other, list):
            return other + [self]
        if isinstance(other, SelectValue):
            return [self, other]
        return self

    def __radd__(self, other):
        if isinstance(other, list):
            return other + [self]
        return self

    def __repr__(self):
        return f"SelectValue({self.dict_val})"


def select(d):
    return SelectValue(d)


def evaluate_ast_node(node):
    if isinstance(node, ast.Constant):
        return node.value
    if isinstance(node, ast.List):
        return [evaluate_ast_node(el) for el in node.elts]
    if isinstance(node, ast.Dict):
        return {
            evaluate_ast_node(k): evaluate_ast_node(v)
            for k, v in zip(node.keys, node.values)
        }
    if (
        isinstance(node, ast.Call)
        and isinstance(node.func, ast.Name)
        and node.func.id == "select"
    ):
        if len(node.args) == 1:
            return SelectValue(evaluate_ast_node(node.args[0]))
    if isinstance(node, ast.BinOp) and isinstance(node.op, ast.Add):
        left = evaluate_ast_node(node.left)
        right = evaluate_ast_node(node.right)
        if isinstance(left, list) and isinstance(right, SelectValue):
            return left + [right]
        if isinstance(left, SelectValue) and isinstance(right, list):
            return [left] + right
        if isinstance(left, list) and isinstance(right, list):
            return left + right
        return [left, right]
    return f"UNSUPPORTED_AST_{type(node).__name__}"


def normalize_path(path):
    if path.startswith("//"):
        return path.lstrip("/").replace(":", "/")
    return path


def normalize_paths_recursive(val):
    if isinstance(val, list):
        return [normalize_paths_recursive(item) for item in val]
    if isinstance(val, dict):
        return {k: normalize_paths_recursive(v) for k, v in val.items()}
    if isinstance(val, SelectValue):
        return SelectValue(normalize_paths_recursive(val.dict_val))
    if isinstance(val, str):
        return normalize_path(val)
    return val


def parse_bazel_rules_from_build_output(build_output, package):
    tree = ast.parse(build_output)
    rules = []
    for node in tree.body:
        if isinstance(node, ast.Expr) and isinstance(node.value, ast.Call):
            call_node = node.value
            if isinstance(call_node.func, ast.Name):
                rule_type = call_node.func.id
                attrs = {}
                for kw in call_node.keywords:
                    attrs[kw.arg] = evaluate_ast_node(kw.value)

                def flatten_list(val):
                    if isinstance(val, list):
                        flat = []
                        for item in val:
                            if isinstance(item, SelectValue):
                                for k, v in item.dict_val.items():
                                    flat.extend(v)
                            else:
                                flat.append(item)
                        return flat
                    return []

                rules.append(
                    Rule(
                        type=rule_type,
                        name=attrs.get("name", ""),
                        package=package,
                        srcs=normalize_paths_recursive(attrs.get("srcs", [])),
                        hdrs=normalize_paths_recursive(attrs.get("hdrs", [])),
                        textual_hdrs=normalize_paths_recursive(
                            flatten_list(attrs.get("textual_hdrs", []))
                        ),
                        deps=resolve_deps(flatten_list(attrs.get("deps", []))),
                        visibility=attrs.get("visibility", []),
                        testonly=attrs.get("testonly", False),
                    )
                )
    return rules


def read_bazel_build(package):
    """Runs bazel query on given package file and returns all cc rules."""
    BAZEL_BIN = "../../tools/bazel"
    result = subprocess.check_output(
        [BAZEL_BIN, "query", package + ":all", "--output", "build"]
    ).decode("utf-8")
    return [
        r
        for r in parse_bazel_rules_from_build_output(result, package)
        if r.type.startswith("cc_")
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

    def flatten_paths(val):
        if isinstance(val, list):
            flat = []
            for item in val:
                if isinstance(item, SelectValue):
                    for k, v in item.dict_val.items():
                        flat.extend(v)
                else:
                    flat.append(item)
            return flat
        return []

    pair_map = {}
    for rule in bazel_rules:
        best_crule, best_similarity = None, 0
        rule_files = set(
            flatten_paths(rule.srcs)
            + flatten_paths(rule.hdrs)
            + rule.textual_hdrs
        )
        for crule in cmake_rules:
            crule_files = set(crule.srcs + crule.hdrs + crule.textual_hdrs)
            similarity = len(rule_files.intersection(crule_files))
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


def absl_internal_testonly(rule):
    return rule.testonly and rule.name != "status_matchers"


def generate_builds(root_path):
    """Generates builds from all BUILD files under absl directory."""
    bazel_rules = list(
        filter(
            lambda r: r.type == "cc_library" and not absl_internal_testonly(r),
            collect_bazel_rules(root_path),
        )
    )
    cmake_rules = list(
        filter(
            lambda r: r.type == "absl_cc_library",
            collect_cmake_rules(root_path),
        )
    )
    pair_map = pairing_bazel_and_cmake_rules(bazel_rules, cmake_rules)
    builds = []
    for rule in sorted(bazel_rules, key=lambda r: r.package[2:] + ":" + r.name):
        common_srcs = []
        win_srcs = []
        for item in rule.srcs:
            if isinstance(item, SelectValue):
                for k, v in item.dict_val.items():
                    if k in ("@platforms//os:windows", ":windows"):
                        win_srcs.extend(v)
            else:
                common_srcs.append(item)

        common_hdrs = []
        win_hdrs = []
        for item in rule.hdrs:
            if isinstance(item, SelectValue):
                for k, v in item.dict_val.items():
                    if k in ("@platforms//os:windows", ":windows"):
                        win_hdrs.extend(v)
            else:
                common_hdrs.append(item)

        win_resolved_srcs = resolve_srcs(win_srcs)
        win_resolved_hdrs = resolve_hdrs(win_srcs + win_hdrs)

        p = {
            "name": rule.package[2:] + ":" + rule.name,
            "cmake_target": pair_map.get((rule.package, rule.name)) or "",
            "headers": sorted(
                resolve_hdrs(common_srcs + common_hdrs + rule.textual_hdrs)
            ),
            "src": sorted(
                resolve_srcs(common_srcs + common_hdrs + rule.textual_hdrs)
            ),
            "deps": sorted(resolve_deps(rule.deps)),
        }
        if win_resolved_srcs:
            p["src_windows"] = sorted(win_resolved_srcs)
        if win_resolved_hdrs:
            p["headers_windows"] = sorted(win_resolved_hdrs)

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

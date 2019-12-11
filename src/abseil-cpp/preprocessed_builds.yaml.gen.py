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
import subprocess
import xml.etree.ElementTree as ET
import yaml

ABSEIL_PATH = "third_party/abseil-cpp"
OUTPUT_PATH = "src/abseil-cpp/preprocessed_builds.yaml"

# Rule object representing the rule of Bazel BUILD.
Rule = collections.namedtuple(
    "Rule", "type name package srcs hdrs textual_hdrs deps visibility testonly")


def get_elem_value(elem, name):
  """Returns the value of XML element with the given name."""
  for child in elem:
    if child.attrib.get("name") == name:
      if child.tag == "string":
        return child.attrib.get("value")
      elif child.tag == "boolean":
        return child.attrib.get("value") == "true"
      elif child.tag == "list":
        return [nested_child.attrib.get("value") for nested_child in child]
      else:
        raise "Cannot recognize tag: " + child.tag
  return None


def normalize_paths(paths):
  """Returns the list of normalized path."""
  # e.g. ["//absl/strings:dir/header.h"] -> ["absl/strings/dir/header.h"]
  return [path.lstrip("/").replace(":", "/") for path in paths]


def parse_rule(elem, package):
  """Returns a rule from bazel XML rule."""
  return Rule(
      type=elem.attrib["class"],
      name=get_elem_value(elem, "name"),
      package=package,
      srcs=normalize_paths(get_elem_value(elem, "srcs") or []),
      hdrs=normalize_paths(get_elem_value(elem, "hdrs") or []),
      textual_hdrs=normalize_paths(get_elem_value(elem, "textual_hdrs") or []),
      deps=get_elem_value(elem, "deps") or [],
      visibility=get_elem_value(elem, "visibility") or [],
      testonly=get_elem_value(elem, "testonly") or False)


def read_build(package):
  """Runs bazel query on given package file and returns all cc rules."""
  result = subprocess.check_output(
      ["bazel", "query", package + ":all", "--output", "xml"])
  root = ET.fromstring(result)
  return [
      parse_rule(elem, package)
      for elem in root
      if elem.tag == "rule" and elem.attrib["class"].startswith("cc_")
  ]


def collect_rules(root_path):
  """Collects and returns all rules from root path recursively."""
  rules = []
  for cur, _, _ in os.walk(root_path):
    build_path = os.path.join(cur, "BUILD.bazel")
    if os.path.exists(build_path):
      rules.extend(read_build("//" + cur))
  return rules


def resolve_hdrs(files):
  return [
      ABSEIL_PATH + "/" + f for f in files if f.endswith((".h", ".inc"))
  ]


def resolve_srcs(files):
  return [
      ABSEIL_PATH + "/" + f for f in files if f.endswith(".cc")
  ]


def resolve_deps(targets):
  return [(t[2:] if t.startswith("//") else t) for t in targets]


def generate_builds(root_path):
  """Generates builds from all BUILD files under absl directory."""
  rules = filter(lambda r: r.type == "cc_library" and not r.testonly,
                 collect_rules(root_path))
  builds = []
  for rule in sorted(rules, key=lambda r: r.package[2:] + ":" + r.name):
    p = {
        "name": rule.package[2:] + ":" + rule.name,
        "headers": sorted(resolve_hdrs(rule.srcs + rule.hdrs + rule.textual_hdrs)),
        "src": sorted(resolve_srcs(rule.srcs + rule.hdrs + rule.textual_hdrs)),
        "deps": sorted(resolve_deps(rule.deps)),
    }
    builds.append(p)
  return builds


def main():
  previous_dir = os.getcwd()
  os.chdir(ABSEIL_PATH)
  builds = generate_builds("absl")
  os.chdir(previous_dir)
  with open(OUTPUT_PATH, 'w') as outfile:
    outfile.write(yaml.dump(builds, indent=2, sort_keys=True))


if __name__ == "__main__":
  main()

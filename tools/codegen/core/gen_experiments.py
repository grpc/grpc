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

from __future__ import print_function

import collections
import ctypes
import json
import math
import os
import re
import sys

import yaml

with open('src/core/lib/experiments/experiments.yaml') as f:
    attrs = yaml.load(f.read())

print(attrs)


def c_str(s, encoding='ascii'):
    if isinstance(s, str):
        s = s.encode(encoding)
    result = ''
    for c in s:
        c = chr(c) if isinstance(c, int) else c
        if not (32 <= ord(c) < 127) or c in ('\\', '"'):
            result += '\\%03o' % ord(c)
        else:
            result += c
    return '"' + result + '"'


def snake_to_pascal(s):
    return ''.join(x.capitalize() for x in s.split('_'))


# utility: print a big comment block into a set of files
def put_banner(files, banner):
    for f in files:
        print('/*', file=f)
        for line in banner:
            print(' * %s' % line, file=f)
        print(' */', file=f)
        print(file=f)


def put_copyright(file):
    # copy-paste copyright notice from this file
    with open(sys.argv[0]) as my_source:
        copyright = []
        for line in my_source:
            if line[0] != '#':
                break
        for line in my_source:
            if line[0] == '#':
                copyright.append(line)
                break
        for line in my_source:
            if line[0] != '#':
                break
            copyright.append(line)
        put_banner([file], [line[2:].rstrip() for line in copyright])


EXPERIMENT_METADATA = """struct ExperimentMetadata {
  const char* name;
  const char* description;
  bool default_value;
  bool (*is_enabled)();
};"""

with open('src/core/lib/experiments/experiments.h', 'w') as H:
    put_copyright(H)

    put_banner(
        [H],
        ["Automatically generated by tools/codegen/core/gen_experiments.py"])

    print("#ifndef GRPC_CORE_LIB_EXPERIMENTS_EXPERIMENTS_H", file=H)
    print("#define GRPC_CORE_LIB_EXPERIMENTS_EXPERIMENTS_H", file=H)
    print(file=H)
    print("#include <grpc/support/port_platform.h>", file=H)
    print(file=H)
    print("#include <stddef.h>", file=H)
    print(file=H)
    print("namespace grpc_core {", file=H)
    print(file=H)
    for attr in attrs:
        print("bool Is%sEnabled();" % snake_to_pascal(attr['name']), file=H)
    print(file=H)
    print(EXPERIMENT_METADATA, file=H)
    print(file=H)
    print("constexpr const size_t kNumExperiments = %d;" % len(attrs), file=H)
    print(
        "extern const ExperimentMetadata g_experiment_metadata[kNumExperiments];",
        file=H)
    print(file=H)
    print("}  // namespace grpc_core", file=H)
    print(file=H)
    print("#endif  // GRPC_CORE_LIB_EXPERIMENTS_EXPERIMENTS_H", file=H)

with open('src/core/lib/experiments/experiments.cc', 'w') as C:
    put_copyright(C)

    put_banner(
        [C],
        ["Automatically generated by tools/codegen/core/gen_experiments.py"])

    print("#include <grpc/support/port_platform.h>", file=C)
    print("#include \"src/core/lib/experiments/experiments.h\"", file=C)
    print("#include \"src/core/lib/gprpp/global_config.h\"", file=C)
    print(file=C)
    for attr in attrs:
        print(
            "GPR_GLOBAL_CONFIG_DEFINE_BOOL(grpc_experimental_enable_%s, %s, %s);"
            % (attr['name'], 'true' if attr['default'] else 'false',
               c_str(attr['description'])),
            file=C)
    print(file=C)
    print("namespace grpc_core {", file=C)
    print(file=C)
    for attr in attrs:
        print("bool Is%sEnabled() {" % snake_to_pascal(attr['name']), file=C)
        print(
            "  static const bool enabled = GPR_GLOBAL_CONFIG_GET(grpc_experimental_enable_%s);"
            % attr['name'],
            file=C)
        print("  return enabled;", file=C)
        print("}", file=C)
    print(file=C)
    print("const ExperimentMetadata g_experiment_metadata[] = {", file=C)
    for attr in attrs:
        print("  {%s, %s, %s, Is%sEnabled}," %
              (c_str(attr['name']), c_str(attr['description']), 'true'
               if attr['default'] else 'false', snake_to_pascal(attr['name'])),
              file=C)
    print("};", file=C)
    print(file=C)
    print("}  // namespace grpc_core", file=C)

for file in os.scandir("tools/internal_ci/linux/experiments"):
    os.remove(file.path)
for file in os.scandir("tools/internal_ci/linux/pull_request/experiments"):
    os.remove(file.path)


def edit_config(src, attr):
    with open(src) as f:
        config = f.read().splitlines()
    out = []
    edit_next_value = False
    in_env_vars = False
    env_var_key = None
    env_var_value = None
    for line in config:
        if in_env_vars:
            if line.startswith("}"):
                assert (env_var_key)
                assert (env_var_value)
                if env_var_key == "BAZEL_FLAGS":
                    env_var_value += " --test_env=GRPC_EXPERIMENT_%s=true" % attr[
                        'name'].upper()
                out.append("  key: \"%s\"" % env_var_key)
                out.append("  value: \"%s\"" % env_var_value)
                in_env_vars = False
            m = re.search("key: \"(.*)\"", line)
            if m:
                env_var_key = m.group(1)
                continue
            m = re.search("value: \"(.*)\"", line)
            if m:
                env_var_value = m.group(1)
                continue
        out.append(line)
        if line.startswith("env_vars {"):
            assert (not in_env_vars)
            in_env_vars = True
            env_var_key = None
            env_var_value = None
    out.append("")
    out.append("env_vars {")
    out.append("  key: \"BAZEL_TESTS\"")
    out.append("  value: \"%s\"" % " ".join(attr["tests"]))
    out.append("}")
    out.append("")
    return '\n'.join(out)


for attr in attrs:
    if 'tests' not in attr or not attr['tests']:
        continue
    with open(
            "tools/internal_ci/linux/experiments/grpc_bazel_rbe_asan_experiment_%s.cfg"
            % attr['name'], 'w') as CFG:
        CFG.write(
            edit_config("tools/internal_ci/linux/grpc_bazel_rbe_asan.cfg",
                        attr))
    with open(
            "tools/internal_ci/linux/pull_request/experiments/grpc_bazel_rbe_asan_experiment_%s.cfg"
            % attr['name'], 'w') as CFG:
        CFG.write(
            edit_config(
                "tools/internal_ci/linux/pull_request/grpc_bazel_rbe_asan.cfg",
                attr))

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
import datetime
import json
import math
import os
import re
import sys

import yaml

with open('src/core/lib/experiments/experiments.yaml') as f:
    attrs = yaml.load(f.read(), Loader=yaml.FullLoader)

error = False
today = datetime.date.today()
two_quarters_from_now = today + datetime.timedelta(days=180)
for attr in attrs:
    if 'name' not in attr:
        print("experiment with no name: %r" % attr)
        error = True
        continue
    if 'description' not in attr:
        print("no description for experiment %s" % attr['name'])
        error = True
    if 'default' not in attr:
        print("no default for experiment %s" % attr['name'])
        error = True
    if 'expiry' not in attr:
        print("no expiry for experiment %s" % attr['name'])
        error = True
    expiry = datetime.datetime.strptime(attr['expiry'], '%Y/%m/%d').date()
    if expiry < today:
        print("experiment %s expired on %s" % (attr['name'], attr['expiry']))
        error = True
    if expiry > two_quarters_from_now:
        print("experiment %s expires far in the future on %s" %
              (attr['name'], attr['expiry']))
        error = True

if error:
    sys.exit(1)


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
    print("namespace {", file=C)
    for attr in attrs:
        print("const char* const description_%s = %s;" %
              (attr['name'], c_str(attr['description'])),
              file=C)
    print("}", file=C)
    print(file=C)
    for attr in attrs:
        print(
            "GPR_GLOBAL_CONFIG_DEFINE_BOOL(grpc_experimental_enable_%s, %s, description_%s);"
            % (attr['name'], 'true' if attr['default'] else 'false',
               attr['name']),
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
        print("  {%s, description_%s, %s, Is%sEnabled}," %
              (c_str(attr['name']), attr['name'], 'true'
               if attr['default'] else 'false', snake_to_pascal(attr['name'])),
              file=C)
    print("};", file=C)
    print(file=C)
    print("}  // namespace grpc_core", file=C)

tags_to_experiments = collections.defaultdict(list)
for attr in attrs:
    for tag in attr['test_tags']:
        tags_to_experiments[tag].append(attr['name'])

with open('bazel/experiments.bzl', 'w') as B:
    print("EXPERIMENTS=%r" % dict(tags_to_experiments), file=B)

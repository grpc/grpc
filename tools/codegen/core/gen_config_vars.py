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

with open('src/core/lib/config/config_vars.yaml') as f:
    attrs = yaml.load(f.read(), Loader=yaml.FullLoader)

def name(attr):
    if 'config' in attr: return attr['config']
    return attr['experiment']

def var_name(attr):
    if 'config' in attr: return 'grpc_' + attr['config']
    return 'grpc_experimental_' + attr['experiment']

error = False
today = datetime.date.today()
two_quarters_from_now = today + datetime.timedelta(days=180)
for attr in attrs:
    if 'config' not in attr and 'experiment' not in attr:
        print("not a config or an experiment: %r" % attr)
        error = True
        continue
    if 'config' in attr and 'experiment' in attr:
        print("can't be both a config and an experiment: %r", attr)
        error = True
    if 'config' in attr and attr['config'].startswith('experiment'):
        print("use 'experiment:' for experiments, not 'config:': %r", attr)
        error = True
    if 'description' not in attr:
        print("no description for %s" % name(attr))
        error = True
    if 'default' not in attr:
        print("no default for %s" % name(attr))
        error = True
    if 'experiment' in attr:
        if 'expiry' not in attr:
            print("no expiry for experiment %s" % name(attr))
            error = True
        expiry = datetime.datetime.strptime(attr['expiry'], '%Y/%m/%d').date()
        if expiry < today:
            print("experiment %s expired on %s" % (name(attr), attr['expiry']))
            error = True
        if expiry > two_quarters_from_now:
            print("experiment %s expires far in the future on %s" %
                (name(attr), attr['expiry']))
            error = True
        if 'owner' not in attr:
            print("no owner for experiment %s", name(attr))
            error = True

if error:
    sys.exit(1)


def c_str(s, encoding='ascii'):
    if s is None: return '""'
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


RETURN_TYPE = {
    "int": "int32_t",
    "string": "absl::string_view",
    "bool": "bool",
}

MEMBER_TYPE = {
    "int": "int32_t",
    "string": "std::string",
    "bool": "bool",
}

SORT_ORDER_FOR_PACKING = {
    "int": 0,
    "bool": 1,
    "string": 2
}

DEFAULT_VALUE = {
    "int": lambda x, name: x,
    "bool": lambda x, name: "true" if x else "false",
    "string": lambda x, name: "default_" + name,
}

attrs_in_packing_order = sorted(attrs, key=lambda a: SORT_ORDER_FOR_PACKING[a['type']])

with open('src/core/lib/config/config_vars.h', 'w') as H:
    put_copyright(H)

    put_banner(
        [H],
        ["Automatically generated by tools/codegen/core/gen_config_vars.py"])

    print("#ifndef GRPC_CORE_LIB_CONFIG_CONFIG_VARS_H", file=H)
    print("#define GRPC_CORE_LIB_CONFIG_CONFIG_VARS_H", file=H)
    print(file=H)
    print("#include <grpc/support/port_platform.h>", file=H)
    print(file=H)
    print("#include <string>", file=H)
    print("#include <functional>", file=H)
    print("#include <atomic>", file=H)
    print("#include \"absl/strings/string_view.h\"", file=H)
    print("#include \"absl/types/optional.h\"", file=H)
    print("#include \"absl/types/span.h\"", file=H)
    print("#include \"src/core/lib/config/config_var_metadata.h\"", file=H)
    print(file=H)
    print("namespace grpc_core {", file=H)
    print(file=H)
    print("class ConfigVars {",file=H)
    print(" public:",file=H)
    print("  ConfigVars(const ConfigVars&) = delete;",file=H)
    print("  ConfigVars& operator=(const ConfigVars&) = delete;",file=H)
    print("  // Get the core configuration; if it does not exist, create it.", file=H)
    print("  static const ConfigVars& Get() {", file=H)
    print("    auto* p = config_vars_.load(std::memory_order_acquire);", file=H)
    print("    if (p != nullptr) return *p;", file=H)
    print("    return Load();", file=H)
    print("  }",file=H)
    print("  // Drop the config vars. Users must ensure no other threads are", file=H)
    print("  // accessing the configuration.", file=H)
    print("  static void Reset();", file=H)
    for attr in attrs:
        for line in attr['description'].splitlines():
            print("  // %s" % line, file=H)
        print("  %s %s() const { return %s_; }" % (
            RETURN_TYPE[attr['type']], snake_to_pascal(name(attr)), name(attr)),file=H)
    print("  static absl::Span<const ConfigVarMetadata> metadata();",file=H)
    print(" private:",file=H)
    print("  ConfigVars();",file=H)
    print("  static const ConfigVars& Load();",file=H)
    print("  static std::atomic<ConfigVars*> config_vars_;",file=H)
    for attr in attrs_in_packing_order:
        print("  %s %s_;" % (
            MEMBER_TYPE[attr['type']], name(attr)),file=H)
    print("};",file=H)
    print(file=H)
    print("}  // namespace grpc_core", file=H)
    print(file=H)
    print("#endif  // GRPC_CORE_LIB_CONFIG_CONFIG_VARS_H", file=H)

with open('src/core/lib/config/config_vars.cc', 'w') as C:
    put_copyright(C)

    put_banner(
        [C],
        ["Automatically generated by tools/codegen/core/gen_config_vars.py"])

    print("#include <grpc/support/port_platform.h>", file=C)
    print("#include <vector>", file=C)
    print("#include \"src/core/lib/config/config_vars.h\"", file=C)
    print("#include \"src/core/lib/config/config_source.h\"", file=C)
    print(file=C)
    print("namespace {", file=C)
    for attr in attrs:
        print("const char* const description_%s = %s;" %
              (name(attr), c_str(attr['description'])),
              file=C)
    for attr in attrs:
        if attr['type'] == "string":
            print("const char* const default_%s = %s;" %
                (name(attr), c_str(attr['default'])),
                file=C)
    for attr in attrs:
        print("GRPC_CONFIG_DEFINE_%s(%s, description_%s, %s);" % (
            attr["type"].upper(),
            var_name(attr),
            name(attr),
            DEFAULT_VALUE[attr['type']](attr['default'], name(attr))
                    )            ,file=C)
    print("}", file=C)
    print(file=C)
    print("namespace grpc_core {", file=C)
    print(file=C)
    print("ConfigVars::ConfigVars() :", file=C)
    print(",".join("%s_(GRPC_CONFIG_LOAD_%s(%s, description_%s, %s))" % (
        name(attr),
        attr['type'].upper(), 
        var_name(attr),
        name(attr),
        DEFAULT_VALUE[attr['type']](attr['default'], name(attr))
        ) for attr in attrs_in_packing_order), file=C)
    print("{}", file=C)
    print(file=C)
    print("absl::Span<const ConfigVarMetadata> ConfigVars::metadata() {",file=C)
    print("  static const auto* metadata = new std::vector<ConfigVarMetadata>{",file=C)
    for attr in attrs:
        print("    {", file=C)
        print("      %s," % c_str(name(attr)), file=C)
        print("      description_%s," % name(attr), file=C)
        print("      %s," % ('true' if 'experiment' in attr else 'false'), file=C)
        print("      ConfigVarMetadata::%s{%s, &ConfigVars::%s}," % (
            snake_to_pascal(attr['type']),
            DEFAULT_VALUE[attr['type']](attr['default'], name(attr)),
            snake_to_pascal(name(attr))), file=C)
        print("    },", file=C)
    print("  };",file=C)
    print("  return *metadata;",file=C)
    print("}",file=C)
    print(file=C)
    print("}", file=C)

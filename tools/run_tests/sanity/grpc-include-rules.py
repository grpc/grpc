#!/usr/bin/python3

# Copyright 2021 gRPC authors.
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

import os
import re
import sys

os.chdir(os.path.join(os.path.dirname(sys.argv[0]), '../../..'))

fixed = []


def is_own_header(filename, header):
    (f_dirname, f_basename) = os.path.split(filename)
    (f_basename, f_ext) = os.path.splitext(f_basename)

    (h_dirname, h_basename) = os.path.split(header)
    (h_basename, h_ext) = os.path.splitext(h_basename)

    if f_dirname == h_dirname and f_basename == h_basename:
        return True
    if f_dirname.startswith('test'):
        if f_basename.endswith('_test'):
            f_basename = f_basename[:-len('_test')]
        if f_basename == h_basename:
            return True
    return False


def fix(filename):
    if filename.startswith('src/core/ext/upbdefs-generated'):
        return
    if filename.startswith('src/core/ext/upb-generated'):
        return
    if filename in [
            'include/grpc/support/port_platform.h',
            'include/grpc/impl/codegen/port_platform.h',
            'test/core/end2end/end2end_tests.cc',
            'test/core/surface/public_headers_must_be_c89.c'
    ]:
        return
    system_includes = []
    project_includes = []
    with open(filename) as f:
        text = f.read()
    state = "preamble"
    preamble = []
    postamble = []
    comment = []
    includes = []
    warned_about_skipping = False
    for line in text.splitlines():
        if state == "preamble":
            if line.startswith("#include"):
                state = "includes"
            else:
                preamble.append(line)
        if state == "includes":
            if re.search(r"^\s*//.*", line):
                comment.append(line)
            elif line.strip() == '':
                if comment:
                    comment.append(line)
            elif line.startswith('#include'):
                line = line[len('#include'):]
                m = re.search(r'"([^"]*)"(.*)', line)
                comment = [line for line in comment if line != '']
                if m:
                    includes.append(
                        (m.group(1), "project", comment, m.group(2)))
                m = re.search(r'<([^>]*)>(.*)', line)
                if m:
                    includes.append((m.group(1), "system", comment, m.group(2)))
                comment = []
            else:
                state = "postamble"
        if state == "postamble":
            if line.startswith("#include"):
                if not warned_about_skipping:
                    print(
                        "WARNING: includes found after main block in %s: skipping formatting"
                        % filename)
                    warned_about_skipping = True
            postamble.append(line)
    while preamble and not preamble[-1].strip():
        preamble = preamble[:-1]

    if not includes:
        return

    out = ''
    for line in preamble:
        out += '%s\n' % line

    new_includes = []
    for include in includes:
        if include[1] == "project" and include[0].startswith('include/grpc'):
            include = (include[0][len('include/'):], "system", include[2],
                       include[3])
        if include[1] == "project" and include[0].startswith('grpc'):
            include = (include[0], "system", include[2], include[3])
        found = False
        for new_include in new_includes:
            if new_include[0] == include[0] and new_include[1] == include[1]:
                found = True
        if not found:
            new_includes.append(include)
    includes = sorted(new_includes)

    # first thing must be port_platform
    new_includes = []
    has_port_platform = False
    for include in includes:
        if include[1] == "system" and include[
                0] == 'grpc/support/port_platform.h':
            has_port_platform = True
            continue
        if include[1] == "system" and include[
                0] == 'grpc/impl/codegen/port_platform.h':
            has_port_platform = True
            continue
        new_includes.append(include)
    includes = new_includes
    if filename.startswith('include/grpc/'):
        has_port_platform = True
    if filename.startswith('src/core/'):
        has_port_platform = True
    if has_port_platform:
        if '/impl/codegen/' in filename:
            out += '\n#include <grpc/impl/codegen/port_platform.h>\n'
        else:
            out += '\n#include <grpc/support/port_platform.h>\n'

    # Then own header
    new_includes = []
    for include in includes:
        if include[1] == "project" and is_own_header(filename, include[0]):
            out += '\n#include "%s"%s\n' % (include[0], include[3])
        else:
            new_includes.append(include)
    includes = new_includes

    # Then C headers
    new_includes = []
    first = True
    for include in includes:
        if include[1] == "system" and os.path.splitext(
                include[0])[1] != '' and not include[0].startswith('grpc'):
            if first:
                out += '\n'
                first = False
            for line in include[2]:
                out += '%s\n' % line
            out += '#include <%s>%s\n' % (include[0], include[3])
        else:
            new_includes.append(include)
    includes = new_includes

    # Then C++ headers
    new_includes = []
    first = True
    for include in includes:
        if include[1] == "system" and os.path.splitext(
                include[0])[1] == '' and not include[0].startswith('grpc'):
            if first:
                out += '\n'
                first = False
            for line in include[2]:
                out += '%s\n' % line
            out += '#include <%s>%s\n' % (include[0], include[3])
        else:
            new_includes.append(include)
    includes = new_includes

    # Other libraries headers
    new_includes = []
    first = True
    for include in includes:
        if (include[1] == "project" and not include[0].startswith('src/') and
                not include[0].startswith('test/') and
                not include[0].startswith('grpc/') and
                not include[0].startswith('grpcpp/')):
            if first:
                out += '\n'
                first = False
            for line in include[2]:
                out += '%s\n' % line
            out += '#include "%s"%s\n' % (include[0], include[3])
        else:
            new_includes.append(include)
    includes = new_includes

    # our 'system' headers
    new_includes = []
    first = True
    for include in includes:
        if include[1] == "system" and include[0].startswith('grpc'):
            if first:
                out += '\n'
                first = False
            for line in include[2]:
                out += '%s\n' % line
            out += '#include <%s>%s\n' % (include[0], include[3])
        else:
            new_includes.append(include)
    includes = new_includes

    # our project headers
    new_includes = []
    first = True
    for include in includes:
        if include[1] == "project":
            if first:
                out += '\n'
                first = False
            for line in include[2]:
                out += '%s\n' % line
            out += '#include "%s"%s\n' % (include[0], include[3])
        else:
            new_includes.append(include)
    includes = new_includes

    if includes:
        print("Error: missed includes in formatting: %s", includes)
        sys.exit(1)

    out += '\n'
    for line in comment:
        out += '%s\n' % line
    for line in postamble:
        out += '%s\n' % line

    if out.strip() != text.strip():
        fixed.append(filename)
        with open(filename, 'w') as f:
            f.write(out)


def fix_dir(dirname):
    for root, dirs, files in os.walk(dirname):
        for filename in files:
            if os.path.splitext(filename)[1] in ['.c', '.cc', '.h']:
                fix(os.path.join(root, filename))


if sys.argv[1:]:
    for filename in sys.argv[1:]:
        fix(filename)
else:
    fix_dir("include")
    fix_dir("src/core")
    fix_dir("src/cpp")
    fix_dir("test/core")
    fix_dir("test/cpp")

if fixed:
    print("Fixed: %s" % fixed)
    sys.exit(1)

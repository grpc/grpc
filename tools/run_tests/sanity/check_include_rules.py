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

import collections
import os
import re
import sys

os.chdir(os.path.join(os.path.dirname(sys.argv[0]), '../../..'))

fixed = []


def is_own_header(filename, header, header_type):
    return False


PORT_PLATFORM = -1
OWN = 0
SYSTEM_C = 1
SYSTEM_CPP = 2
OTHER_LIBRARIES = 3
GRPC_SYSTEM = 4
GRPC_PROJECT = 5


def categorize_header(filename, header, header_type):
    (f_dirname, f_basename) = os.path.split(filename)
    (f_basename, f_ext) = os.path.splitext(f_basename)

    (h_dirname, h_basename) = os.path.split(header)
    (h_basename, h_ext) = os.path.splitext(h_basename)

    if header_type == 'system' and header in [
            'grpc/support/port_platform.h', 'grpc/impl/codegen/port_platform.h'
    ]:
        return PORT_PLATFORM

    if header_type == 'project':
        if f_dirname == h_dirname and f_basename == h_basename:
            return OWN
        if f_dirname.startswith('test'):
            if f_basename.endswith('_test'):
                f_basename = f_basename[:-len('_test')]
            if f_basename == h_basename:
                return OWN
    elif header_type == 'system':
        if h_dirname in ['grpcpp', 'grpc']:
            if f_basename == h_basename:
                return OWN

    # Then C headers
    if header_type == "system" and os.path.splitext(
            header)[1] != '' and not header.startswith('grpc'):
        return SYSTEM_C

    # Then C++ headers
    if header_type == "system" and os.path.splitext(
            header)[1] == '' and not header.startswith('grpc'):
        return SYSTEM_CPP

    # Other libraries headers
    if (header_type == "project" and not header.startswith('src/') and
            not header.startswith('test/') and
            not header.startswith('grpc/') and
            not header.startswith('grpcpp/')):
        return OTHER_LIBRARIES

    # our 'system' headers
    if header_type == "system" and header.startswith('grpc'):
        return GRPC_SYSTEM

    # our project headers
    if header_type == "project":
        return GRPC_PROJECT

    return GRPC_PROJECT


def fix(filename):
    if filename.startswith('src/core/ext/upbdefs-generated'):
        return
    if filename.startswith('src/core/ext/upb-generated'):
        return
    if filename in [
            'include/grpc/support/port_platform.h',
            'include/grpc/impl/codegen/port_platform.h',
            'test/core/end2end/end2end_nosec_tests.cc',
            'test/core/surface/public_headers_must_be_c89.c'
    ]:
        return
    print(filename)
    with open(filename) as f:
        text = f.read()
    state = "preamble"
    # text before includes
    preamble = []
    # text after includes
    postamble = []
    # currently buffering comment
    comment = []
    # one include file
    Include = collections.namedtuple("Include", [
        "ifstack", "category", "path", "header_type", "comments_above",
        "comment_right"
    ])
    # list of all includes
    includes = []
    warned_about_skipping = False
    lines = text.splitlines()
    ifstack = []
    line_ifstack = []
    for line_num, line in enumerate(lines):
        if line.startswith('#if '):
            ifstack = list(ifstack)
            ifstack.append(line[len('#if '):])
        elif line.startswith('#ifdef'):
            ifstack = list(ifstack)
            line = line[len('#ifdef'):].strip()
            sp = line.find(' ')
            if sp == -1:
              ifstack.append('defined(%s)' % line)
            else:
              ifstack.append('defined(%s) %s' % (line[:sp], line[sp:]))
        elif line.startswith('#ifndef'):
            ifstack = list(ifstack)
            line = line[len('#ifndef'):].strip()
            sp = line.find(' ')
            if sp == -1:
              ifstack.append('!defined(%s)' % line)
            else:
              ifstack.append('!defined(%s) %s' % (line[:sp], line[sp:]))
        elif line.startswith('#else'):
            ifstack = list(ifstack)
            print(ifstack)
            ifstack[-1] = '!' + ifstack[-1]
        elif line.startswith('#elif '):
            ifstack = list(ifstack)
            ifstack[-1] = line[len('#elif '):]
        elif line.startswith('#endif'):
            ifstack = ifstack[:-1]
        line_ifstack.append(ifstack)
    preamble_ifstack = None
    for line_num, line in enumerate(lines):
        if state == "preamble":
            if line.startswith("#include"):
                preamble_ifstack = line_ifstack[line_num]
                state = "postamble"
            else:
                preamble.append(line)
        if state == "postamble":
            if re.search(r"^\s*//.*", line):
                comment.append(line)
            elif line.strip() == '':
                comment.append(line)
            elif line.startswith('#include'):
                this_ifstack = line_ifstack[line_num]
                usable = True
                if usable and len(this_ifstack) < len(preamble_ifstack):
                    usable = all(a == b for a, b in zip(
                        this_ifstack[:len(preamble_ifstack)], preamble_ifstack))
                if not usable:
                    print('Unusable include %s:%d: %s' %
                          (filename, line_num, line))
                    postamble = postamble + comment + [line]
                    comment = []
                else:
                    ifstack = tuple(this_ifstack[len(preamble_ifstack):])
                    line = line[len('#include'):]
                    for header_type, regex in (("project", r'"([^"]*)"(.*)'),
                                               ("system", r'<([^>]*)>(.*)')):
                        m = re.search(regex, line)
                        if m:
                            header = m.group(1)
                            if header_type == "project" and header.startswith(
                                    'include/grpc'):
                                header_type = "system"
                                header = header[len('include/'):]
                            if header_type == "project" and header.startswith(
                                    'grpc'):
                                header_type = "system"
                            includes.append(
                                Include(
                                    ifstack,
                                    categorize_header(filename, header,
                                                      header_type), header,
                                    header_type,
                                    [c for c in comment if c.strip() != ''],
                                    m.group(2)))
                            comment = []
            else:
                postamble = postamble + comment + [line]
                comment = []
    while preamble and not preamble[-1].strip():
        preamble = preamble[:-1]

    needs_port_platform = False
    new_includes = []
    for include in includes:
        if include.category == PORT_PLATFORM:
            needs_port_platform = True
        else:
            new_includes.append(include)
    includes = new_includes
    if filename.startswith('include/grpc/'):
        needs_port_platform = True
    if filename.startswith('src/core/'):
        needs_port_platform = True
    if needs_port_platform:
        if '/impl/codegen/' in filename:
            port_platform = "grpc/impl/codegen/port_platform.h"
        else:
            port_platform = 'grpc/support/port_platform.h'
        includes.append(
            Include((), PORT_PLATFORM, port_platform, "system", [], ""))

    if not includes:
        return

    new_includes = []
    for include in includes:
        found = False
        for new_include in new_includes:
            if new_include.path == include.path and new_include.category == include.category:
                found = True
        if not found:
            new_includes.append(include)
    includes = sorted(new_includes)

    out = preamble
    while out and out[-1].strip() == '':
      out = out[:-1]

    category = PORT_PLATFORM - 1
    ifstack = ()
    for include in includes:
      if include.ifstack != ifstack:
        for cond in ifstack:
          out.append('#endif  // %s' % cond)
        out.append('')
        for cond in include.ifstack:
          out.append('#if %s' % cond)
        category = include.category
        ifstack = include.ifstack
      if include.category != category:
        category = include.category
        out.append('')
      out.extend(include.comments_above)
      if include.header_type == "project":
        out.append('#include "%s"%s' % (include.path, include.comment_right))
      else:
        out.append('#include <%s>%s' % (include.path, include.comment_right))
    for cond in ifstack:
      out.append('#endif')
    out.append('')

    out.extend(postamble)

    out = '\n'.join(out)
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

#!/usr/bin/env python3

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


def to_inc(filename):
    """Given filename, synthesize what should go in an include statement to get that file"""
    if filename.startswith("include/"):
        return '<%s>' % filename[len("include/"):]
    return '"%s"' % filename


def set_pragma(filename, pragma):
    """Set the file-level IWYU pragma in filename"""
    lines = []
    saw_first_define = False
    for line in open(filename).read().splitlines():
        if line.startswith('// IWYU pragma: '):
            continue
        lines.append(line)
        if not saw_first_define and line.startswith('#define '):
            saw_first_define = True
            lines.append('')
            lines.append('// IWYU pragma: %s' % pragma)
            lines.append('')
    open(filename, 'w').write('\n'.join(lines) + '\n')


def set_exports(pub, cg):
    """In file pub, mark the include for cg with IWYU pragma: export"""
    lines = []
    for line in open(pub).read().splitlines():
        if line.startswith('#include %s' % to_inc(cg)):
            lines.append('#include %s  // IWYU pragma: export' % to_inc(cg))
        else:
            lines.append(line)
    open(pub, 'w').write('\n'.join(lines) + '\n')


CG_ROOTS_GRPC = (
    (r'sync', 'grpc/support/sync.h'),
    (r'atm', 'grpc/support/atm.h'),
)

CG_ROOTS_GRPCPP = []


def fix_tree(tree, cg_roots):
    """Fix one include tree"""
    # Map of filename --> paths including that filename
    reverse_map = collections.defaultdict(list)
    # The same, but for things with '/impl/codegen' in their names
    cg_reverse_map = collections.defaultdict(list)
    for root, dirs, files in os.walk(tree):
        root_map = cg_reverse_map if '/impl/codegen' in root else reverse_map
        for filename in files:
            root_map[filename].append(root)
    # For each thing in '/impl/codegen' figure out what exports it
    for filename, paths in cg_reverse_map.items():
        # Exclude non-headers
        if not filename.endswith('.h'):
            continue
        pragma = None
        # Check for our 'special' headers: if we see one of these, we just
        # hardcode where they go to because there's some complicated rules.
        for root, target in cg_roots:
            if filename.startswith(root):
                pragma = 'private, include <%s>' % target
                if len(paths) == 1:
                    path = paths[0]
                    if filename.startswith(root + '.'):
                        set_exports('include/' + target, path + '/' + filename)
                    if filename.startswith(root + '_'):
                        set_exports(path + '/' + root + '.h',
                                    path + '/' + filename)
        # If the path for a file in /impl/codegen is ambiguous, just don't bother
        if not pragma and len(paths) == 1:
            path = paths[0]
            # Check if we have an exporting candidate
            if filename in reverse_map:
                proper = reverse_map[filename]
                # And that it too is unambiguous
                if len(proper) == 1:
                    # Build the two relevant pathnames
                    cg = path + '/' + filename
                    pub = proper[0] + '/' + filename
                    # And see if the public file actually includes the /impl/codegen file
                    if ('#include %s' % to_inc(cg)) in open(pub).read():
                        # Finally, if it does, we'll set that pragma
                        pragma = 'private, include %s' % to_inc(pub)
                        # And mark the export
                        set_exports(pub, cg)
        # If we can't find a good alternative include to point people to,
        # mark things private anyway... we don't want to recommend people include
        # from impl/codegen
        if not pragma:
            pragma = 'private'
        for path in paths:
            set_pragma(path + '/' + filename, pragma)


fix_tree('include/grpc', CG_ROOTS_GRPC)
fix_tree('include/grpcpp', CG_ROOTS_GRPCPP)

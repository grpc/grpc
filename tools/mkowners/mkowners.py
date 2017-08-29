#!/usr/bin/env python3
# Copyright 2017 gRPC authors.
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

import argparse
import collections
import operator
import os
import re
import subprocess

#
# Find the root of the git tree
#

git_root = (subprocess
            .check_output(['git', 'rev-parse', '--show-toplevel'])
            .decode('utf-8')
            .strip())

#
# Parse command line arguments
#

default_out = os.path.join(git_root, '.github', 'CODEOWNERS')

argp = argparse.ArgumentParser('Generate .github/CODEOWNERS file')
argp.add_argument('--out', '-o',
                  type=str,
                  default=default_out,
                  help='Output file (default %s)' % default_out)
args = argp.parse_args()

#
# Walk git tree to locate all OWNERS files
#

owners_files = [os.path.join(root, 'OWNERS')
                for root, dirs, files in os.walk(git_root)
                if 'OWNERS' in files]

#
# Parse owners files
#

Owners = collections.namedtuple('Owners', 'parent directives dir')
Directive = collections.namedtuple('Directive', 'who globs')

def parse_owners(filename):
  with open(filename) as f:
    src = f.read().splitlines()
  parent = True
  directives = []
  for line in src:
    line = line.strip()
    # line := directive | comment
    if not line: continue
    if line[0] == '#': continue
    # it's a directive
    directive = None
    if line == 'set noparent':
      parent = False
    elif line == '*':
      directive = Directive(who='*', globs=[])
    elif ' ' in line:
      (who, globs) = line.split(' ', 1)
      globs_list = [glob
                    for glob in globs.split(' ')
                    if glob]
      directive = Directive(who=who, globs=globs_list)
    else:
      directive = Directive(who=line, globs=[])
    if directive:
      directives.append(directive)
  return Owners(parent=parent,
                directives=directives,
                dir=os.path.relpath(os.path.dirname(filename), git_root))

owners_data = sorted([parse_owners(filename)
                      for filename in owners_files],
                     key=operator.attrgetter('dir'))

#
# Modify owners so that parented OWNERS files point to the actual
# Owners tuple with their parent field
#

new_owners_data = []
for owners in owners_data:
  if owners.parent == True:
    best_parent = None
    best_parent_score = None
    for possible_parent in owners_data:
      if possible_parent is owners: continue
      rel = os.path.relpath(owners.dir, possible_parent.dir)
      # '..' ==> we had to walk up from possible_parent to get to owners
      #      ==> not a parent
      if '..' in rel: continue
      depth = len(rel.split(os.sep))
      if not best_parent or depth < best_parent_score:
        best_parent = possible_parent
        best_parent_score = depth
    if best_parent:
      owners = owners._replace(parent = best_parent.dir)
    else:
      owners = owners._replace(parent = None)
  new_owners_data.append(owners)
owners_data = new_owners_data

#
# In bottom to top order, process owners data structures to build up
# a CODEOWNERS file for GitHub
#

def full_dir(rules_dir, sub_path):
  return os.path.join(rules_dir, sub_path) if rules_dir != '.' else sub_path

# glob using git
gg_cache = {}
def git_glob(glob):
  global gg_cache
  if glob in gg_cache: return gg_cache[glob]
  r = set(subprocess
      .check_output(['git', 'ls-files', os.path.join(git_root, glob)])
      .decode('utf-8')
      .strip()
      .splitlines())
  gg_cache[glob] = r
  return r

def expand_directives(root, directives):
  globs = collections.OrderedDict()
  # build a table of glob --> owners
  for directive in directives:
    for glob in directive.globs or ['**']:
      if glob not in globs:
        globs[glob] = []
      if directive.who not in globs[glob]:
        globs[glob].append(directive.who)
  # expand owners for intersecting globs
  sorted_globs = sorted(globs.keys(),
                        key=lambda g: len(git_glob(full_dir(root, g))),
                        reverse=True)
  out_globs = collections.OrderedDict()
  for glob_add in sorted_globs:
    who_add = globs[glob_add]
    pre_items = [i for i in out_globs.items()]
    out_globs[glob_add] = who_add.copy()
    for glob_have, who_have in pre_items:
      files_add = git_glob(full_dir(root, glob_add))
      files_have = git_glob(full_dir(root, glob_have))
      intersect = files_have.intersection(files_add)
      if intersect:
        for f in sorted(files_add): # sorted to ensure merge stability
          if f not in intersect:
            out_globs[os.path.relpath(f, start=root)] = who_add
        for who in who_have:
          if who not in out_globs[glob_add]:
            out_globs[glob_add].append(who)
  return out_globs

def add_parent_to_globs(parent, globs, globs_dir):
  if not parent: return
  for owners in owners_data:
    if owners.dir == parent:
      owners_globs = expand_directives(owners.dir, owners.directives)
      for oglob, oglob_who in owners_globs.items():
        for gglob, gglob_who in globs.items():
          files_parent = git_glob(full_dir(owners.dir, oglob))
          files_child = git_glob(full_dir(globs_dir, gglob))
          intersect = files_parent.intersection(files_child)
          gglob_who_orig = gglob_who.copy()
          if intersect:
            for f in sorted(files_child): # sorted to ensure merge stability
              if f not in intersect:
                who = gglob_who_orig.copy()
                globs[os.path.relpath(f, start=globs_dir)] = who
            for who in oglob_who:
              if who not in gglob_who:
                gglob_who.append(who)
      add_parent_to_globs(owners.parent, globs, globs_dir)
      return
  assert(False)

todo = owners_data.copy()
done = set()
with open(args.out, 'w') as out:
  out.write('# Auto-generated by the tools/mkowners/mkowners.py tool\n')
  out.write('# Uses OWNERS files in different modules throughout the\n')
  out.write('# repository as the source of truth for module ownership.\n')
  written_globs = []
  while todo:
    head, *todo = todo
    if head.parent and not head.parent in done:
      todo.append(head)
      continue
    globs = expand_directives(head.dir, head.directives)
    add_parent_to_globs(head.parent, globs, head.dir)
    for glob, owners in globs.items():
      skip = False
      for glob1, owners1, dir1 in reversed(written_globs):
        files = git_glob(full_dir(head.dir, glob))
        files1 = git_glob(full_dir(dir1, glob1))
        intersect = files.intersection(files1)
        if files == intersect:
          if sorted(owners) == sorted(owners1):
            skip = True # nothing new in this rule
            break
        elif intersect:
          # continuing would cause a semantic change since some files are
          # affected differently by this rule and CODEOWNERS is order dependent
          break
      if not skip:
        out.write('/%s %s\n' % (
            full_dir(head.dir, glob), ' '.join(owners)))
        written_globs.append((glob, owners, head.dir))
    done.add(head.dir)

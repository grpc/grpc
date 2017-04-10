#!/usr/bin/env python

import sys
import re
import os

flags = re.M + re.S
apis = (r'^(\n[a-zA-Z_][a-zA-Z0-9]+(?: [*]*)?(?:%s)\((?:.*?){)(.*)' % '|'.join(
  x.strip() for x in open('grpc.def').read().splitlines()[1:]))
apis = re.compile(apis, flags)
c_comment = re.compile(r'^(/[*](?:.*?)[*]/)(.*)', flags)
split_comment = re.compile(r'^\n[ ]*/[*](.*?)[*]/\n(.*)', flags)
cpp_comment = re.compile(r'^(\n//[^\n]*\n)(.*)', flags)
include = re.compile(r'^(#include (?:[^\n]*)\n)(.*)', flags)

in_namespace = False
dst = ''

def enter():
  global in_namespace
  global dst
  if in_namespace: return
  dst += '\nnamespace grpc_core {\n\n'
  in_namespace = True

def leave():
  global in_namespace
  global dst
  if not in_namespace: return
  dst += '\n} // namespace grpc_core\n'
  in_namespace = False

for root, dirs, files in os.walk('src/core'):
  for filename in files:
    base, ext = os.path.splitext(filename)
    if ext != '.c': continue
    src = open(os.path.join(root, filename)).read()
    while src:
      m = split_comment.match(src)
      if m:
        dst += '\n'
        for line in m.group(1).splitlines():
          line = line.strip()
          if line and re.match('^[*]+$', line):
            dst += '//' + ('/' * len(line)) + '\n'
            continue
          if line and line[0] == '*': line = line[1:]
          dst += '// %s\n' % line
        src = m.group(2)
      m = c_comment.match(src)
      if m:
        dst += m.group(1)
        src = m.group(2)
        continue
      m = cpp_comment.match(src)
      if m:
        dst += m.group(1)
        src = m.group(2)
        continue
      m = include.match(src)
      if m:
        leave()
        dst += m.group(1)
        src = m.group(2)
        continue
      m = apis.match(src)
      if m:
        leave()
        dst += 'extern "C" '
        dst += m.group(1)
        depth = 1
        src = m.group(2)
        while src and depth:
          if src[0] == '{': depth += 1
          if src[0] == '}': depth -= 1
          dst += src[0]
          src = src[1:]
        continue
      if src[0] not in ' \t\r\n':
        enter()
      dst += src[0]
      src = src[1:]
    leave()
    open(os.path.join(root, base + '.cc')).write(dst)
    os.unlink(os.path.join(root, filename))

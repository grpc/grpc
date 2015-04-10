#!/usr/bin/env python
'''Generate BUILD file to be used with bazel

This script to be run within the top-level directory of grpc, e.g

tools/buildgen/generate_bazel_build.py
'''
import os
import sys

import bunch
import simplejson

def build_deps(target_dict):
  deps = []
  if target_dict.get('secure', 'no') == 'yes':
    deps = [
      "//external:libssl",
    ]
  if target_dict.get('build', None) == 'protoc':
    deps.append("//external:protobuf_compiler")
  if target_dict['name'] == 'grpc++_unsecure' or target_dict['name'] == 'grpc++':
    deps.append("//external:protobuf_clib")
  for d in target_dict.get('deps', []):
    if d.find('//') == 0 or d[0] == ':':
      deps.append(d)
    else:
      deps.append(':%s' % (d))
  return deps

def print_list_for_build(l):
  return '\n'.join('    "%s",' % (x) for x in l)

def print_file_list_for_build(l, top_dir):
  return '\n'.join('    "%s",' % (os.path.join(top_dir, x)) for x in l)

def translate_rule(target_dict, grpc_top_dir, build_output, bazel_type):
  '''Translate the given target defined by json to bazel build rule to the given output'''
  build_output.write('''
%s(
  name = '%s',
  includes = [
    "%s",
    # This is sub-optimial because we can't put something like -I. to copts.
    ".",
  ],''' % (bazel_type,
       target_dict['name'],
       os.path.join(grpc_top_dir, 'include')))
  private_headers = target_dict.get('headers', [])

  if bazel_type == 'cc_library':
    build_output.write('''
  hdrs = [
%s
  ],''' % (print_file_list_for_build(target_dict.get('public_headers', []),
                                     grpc_top_dir)))

  build_output.write('''
  srcs = [
%s
  ],
  deps = [
%s
  ]
)
''' % (print_file_list_for_build(private_headers + target_dict.get('src', []),
                                 grpc_top_dir),
       print_list_for_build(build_deps(target_dict))
       ))
    
def proto_file_name_to_target_name(proto_file):
  '''Given a proto file name like examples/tips/label.proto, convert it to a
  build target name like examples_tips_label_proto'''
  return proto_file.replace('/', '_').replace('.', '_')


def fix_proto_src(target_dict, proto_file_set):
  srcs = target_dict.get('src', [])
  new_srcs = []
  if target_dict.get('deps') == None:
    target_dict['deps'] = []
  for s in srcs:
    if s.endswith('.proto'):
      target_dict['deps'].append(proto_file_name_to_target_name(s))
      proto_file_set.add(s)
    else:
      new_srcs.append(s)
  target_dict['src'] = new_srcs

FILE_HEADER = '''# GRPC Bazel BUILD file.
# This currently builds C and C++ code.

# Copyright 2015, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

licenses(["notice"])  # 3-clause BSD

package(default_visibility = ["//visibility:public"])
'''

def main(argv):
  grpc_top_dir = ''
  # Skip generating proto rule since bazle hasn't supported proto_library yet.
  GENERATE_PROTO = False
  # Skip generating TEST at the moment because proto_library can't be
  # built by bazel yet
  GENERATE_TEST = False
  json_dict = {}
  for fname in ['build.json']:
    dict_file = open(os.path.join(grpc_top_dir, fname), 'r')
    bunch.merge_json(json_dict, simplejson.loads(dict_file.read()))
    dict_file.close()
  filegroups = {}
  for fg in json_dict.get('filegroups'):
    filegroups[fg['name']] = fg
  proto_file_set = set()

  build_output = open('BUILD', 'w+')
  build_output.write(FILE_HEADER)

  # Expand filegroups
  for lib in json_dict.get('libs'):
    for fg_name in lib.get('filegroups', []):
      fg = filegroups[fg_name]

      src = lib.get('src', [])
      src.extend(fg.get('src', []))
      lib['src'] = src

      headers = lib.get('headers', [])
      headers.extend(fg.get('headers', []))
      lib['headers'] = headers

      public_headers = lib.get('public_headers', [])
      public_headers.extend(fg.get('public_headers', []))
      lib['public_headers'] = public_headers
    fix_proto_src(lib, proto_file_set)
    if not GENERATE_TEST and lib.get('build', None) == 'private':
      continue
    translate_rule(lib, grpc_top_dir, build_output, 'cc_library')

  for tgt in json_dict.get('targets'):
    if tgt['build'] == 'test':
      if not GENERATE_TEST:
        continue
      fix_proto_src(tgt, proto_file_set)
      if tgt['language'] == 'c++':
        tgt['deps'].append('//external:gunit')
      translate_rule(tgt, grpc_top_dir, build_output, 'cc_test')
    elif tgt['build'] == 'protoc':
      translate_rule(tgt, grpc_top_dir, build_output, 'cc_binary')
  for proto_file in proto_file_set:
    if not GENERATE_PROTO:
      break
    build_output.write('''
proto_library(
  name = "%s",
  srcs = [
    "%s",
  ],
)
''' % (proto_file_name_to_target_name(proto_file),
       proto_file))
  build_output.close()
  

if __name__ == '__main__':
  sys.exit(main(sys.argv))

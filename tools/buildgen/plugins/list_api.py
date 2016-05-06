#!/usr/bin/env python2.7

# Copyright 2016, Google Inc.
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

import collections
import fnmatch
import os
import re
import sys
import yaml


_RE_API = r'(?:GPRAPI|GRPCAPI|CENSUSAPI)([^;]*);'


def list_c_apis(filenames):
  for filename in filenames:
    with open(filename, 'r') as f:
      text = f.read()
    for m in re.finditer(_RE_API, text):
      api_declaration = re.sub('[ \r\n\t]+', ' ', m.group(1))
      type_and_name, args_and_close = api_declaration.split('(', 1)
      args = args_and_close[:args_and_close.rfind(')')].strip()
      last_space = type_and_name.rfind(' ')
      last_star = type_and_name.rfind('*')
      type_end = max(last_space, last_star)
      return_type = type_and_name[0:type_end+1].strip()
      name = type_and_name[type_end+1:].strip()
      yield {'return_type': return_type, 'name': name, 'arguments': args, 'header': filename}


def headers_under(directory):
  for root, dirnames, filenames in os.walk(directory):
    for filename in fnmatch.filter(filenames, '*.h'):
      yield os.path.join(root, filename)


def mako_plugin(dictionary):
  apis = []
  headers = []

  for lib in dictionary['libs']:
    if lib['name'] in ['grpc', 'gpr']:
      headers.extend(lib['public_headers'])

  apis.extend(list_c_apis(sorted(set(headers))))
  dictionary['c_apis'] = apis


if __name__ == '__main__':
  print yaml.dump([api for api in list_c_apis(headers_under('include/grpc'))])


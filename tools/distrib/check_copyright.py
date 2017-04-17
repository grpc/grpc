#!/usr/bin/env python2.7

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

import argparse
import datetime
import os
import re
import sys
import subprocess

# find our home
ROOT = os.path.abspath(
    os.path.join(os.path.dirname(sys.argv[0]), '../..'))
os.chdir(ROOT)

# parse command line
argp = argparse.ArgumentParser(description='copyright checker')
argp.add_argument('-o', '--output',
                  default='details',
                  choices=['list', 'details'])
argp.add_argument('-s', '--skips',
                  default=0,
                  action='store_const',
                  const=1)
argp.add_argument('-a', '--ancient',
                  default=0,
                  action='store_const',
                  const=1)
argp.add_argument('-f', '--fix',
                  default=False,
                  action='store_true');
argp.add_argument('--precommit',
                  default=False,
                  action='store_true')
args = argp.parse_args()

# open the license text
with open('LICENSE') as f:
  LICENSE = f.read().splitlines()

# license format by file extension
# key is the file extension, value is a format string
# that given a line of license text, returns what should
# be in the file
LICENSE_PREFIX = {
  '.bat':       r'@rem\s*',
  '.c':         r'\s*(?://|\*)\s*',
  '.cc':        r'\s*(?://|\*)\s*',
  '.h':         r'\s*(?://|\*)\s*',
  '.m':         r'\s*\*\s*',
  '.php':       r'\s*\*\s*',
  '.js':        r'\s*\*\s*',
  '.py':        r'#\s*',
  '.pyx':       r'#\s*',
  '.pxd':       r'#\s*',
  '.pxi':       r'#\s*',
  '.rb':        r'#\s*',
  '.sh':        r'#\s*',
  '.proto':     r'//\s*',
  '.cs':        r'//\s*',
  '.mak':       r'#\s*',
  'Makefile':   r'#\s*',
  'Dockerfile': r'#\s*',
  'LICENSE':    '',
  'BUILD':      r'#\s*',
}

_EXEMPT = frozenset((
  # Generated protocol compiler output.
  'examples/python/helloworld/helloworld_pb2.py',
  'examples/python/helloworld/helloworld_pb2_grpc.py',
  'examples/python/multiplex/helloworld_pb2.py',
  'examples/python/multiplex/helloworld_pb2_grpc.py',
  'examples/python/multiplex/route_guide_pb2.py',
  'examples/python/multiplex/route_guide_pb2_grpc.py',
  'examples/python/route_guide/route_guide_pb2.py',
  'examples/python/route_guide/route_guide_pb2_grpc.py',

  'src/core/ext/filters/client_channel/lb_policy/grpclb/proto/grpc/lb/v1/load_balancer.pb.h',
  'src/core/ext/filters/client_channel/lb_policy/grpclb/proto/grpc/lb/v1/load_balancer.pb.c',
  'src/cpp/server/health/health.pb.h',
  'src/cpp/server/health/health.pb.c',

  # An older file originally from outside gRPC.
  'src/php/tests/bootstrap.php',
  # census.proto copied from github
  'tools/grpcz/census.proto',
  # status.proto copied from googleapis
  'src/proto/grpc/status/status.proto',
))


RE_YEAR = r'Copyright (?P<first_year>[0-9]+\-)?(?P<last_year>[0-9]+), Google Inc\.'
RE_LICENSE = dict(
    (k, r'\n'.join(
        LICENSE_PREFIX[k] +
        (RE_YEAR if re.search(RE_YEAR, line) else re.escape(line))
        for line in LICENSE))
     for k, v in LICENSE_PREFIX.iteritems())

if args.precommit:
  FILE_LIST_COMMAND = 'git status -z | grep -Poz \'(?<=^[MARC][MARCD ] )[^\s]+\''
else:
  FILE_LIST_COMMAND = 'git ls-tree -r --name-only -r HEAD | ' \
                      'grep -v ^third_party/ |' \
                      'grep -v "\(ares_config.h\|ares_build.h\)"'

def load(name):
  with open(name) as f:
    return f.read()

def save(name, text):
  with open(name, 'w') as f:
    f.write(text)

assert(re.search(RE_LICENSE['LICENSE'], load('LICENSE')))
assert(re.search(RE_LICENSE['Makefile'], load('Makefile')))


def log(cond, why, filename):
  if not cond: return
  if args.output == 'details':
    print '%s: %s' % (why, filename)
  else:
    print filename


# scan files, validate the text
ok = True
filename_list = []
try:
  filename_list = subprocess.check_output(FILE_LIST_COMMAND,
                                          shell=True).splitlines()
except subprocess.CalledProcessError:
  sys.exit(0)

for filename in filename_list:
  if filename in _EXEMPT:
    continue
  ext = os.path.splitext(filename)[1]
  base = os.path.basename(filename)
  if ext in RE_LICENSE:
    re_license = RE_LICENSE[ext]
  elif base in RE_LICENSE:
    re_license = RE_LICENSE[base]
  else:
    log(args.skips, 'skip', filename)
    continue
  try:
    text = load(filename)
  except:
    continue
  m = re.search(re_license, text)
  if m:
    pass
  elif 'DO NOT EDIT' not in text and filename != 'src/boringssl/err_data.c':
    log(1, 'copyright missing', filename)
    ok = False

sys.exit(0 if ok else 1)

#!/usr/bin/env python2.7

# Copyright 2015-2016, Google Inc.
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

from __future__ import print_function
import argparse
import subprocess
import sys
import os.path
import sys
import tempfile
import importlib

# Example: tools/codegen/core/gen_grpclb_test_response.py \
#          --lb_proto src/proto/grpc/lb/v1/load_balancer.proto \
#          127.0.0.1:1234 10.0.0.1:4321

# 1) Compile src/proto/grpc/lb/v1/load_balancer.proto to a temp location
parser = argparse.ArgumentParser()
parser.add_argument('--lb_proto', required=True)
parser.add_argument('-e', '--expiration_interval_secs', type=int)
parser.add_argument('-o', '--output')
parser.add_argument('-q', '--quiet', default=False, action='store_true')
parser.add_argument('ipports', nargs='+')
args = parser.parse_args()

if not os.path.isfile(args.lb_proto):
  print("ERROR: file '{}' cannot be accessed (not found, no permissions, etc.)"
        .format(args.lb_proto), file=sys.stderr)
  sys.exit(1)

proto_dirname = os.path.dirname(args.lb_proto)
output_dir = tempfile.mkdtemp()

protoc_cmd = 'protoc -I{} --python_out={} {}'.format(
    proto_dirname, output_dir, args.lb_proto)

with tempfile.TemporaryFile() as stderr_tmpfile:
  if subprocess.call(protoc_cmd, stderr=stderr_tmpfile, shell=True) != 0:
    stderr_tmpfile.seek(0)
    print("ERROR: while running '{}': {}".
          format(protoc_cmd, stderr_tmpfile.read()))
    sys.exit(2)

# 2) import the output .py file.
module_name = os.path.splitext(os.path.basename(args.lb_proto))[0] + '_pb2'
sys.path.append(output_dir)
pb_module = importlib.import_module(module_name)

# 3) Generate!
lb_response = pb_module.LoadBalanceResponse()
if args.expiration_interval_secs:
  lb_response.server_list.expiration_interval.seconds = \
  args.expiration_interval_secs

for ipport in args.ipports:
  ip, port = ipport.split(':')
  server = lb_response.server_list.servers.add()
  server.ip_address = ip
  server.port = int(port)
  server.load_balance_token = b'token{}'.format(port)

serialized_bytes = lb_response.SerializeToString()
serialized_hex = ''.join('\\x{:02x}'.format(ord(c)) for c in serialized_bytes)
if args.output:
  with open(args.output, 'w') as f:
    f.write(serialized_bytes)
if not args.quiet:
  print(str(lb_response))
  print(serialized_hex)

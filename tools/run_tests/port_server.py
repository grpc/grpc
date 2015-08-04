#!/usr/bin/env python
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

"""Manage TCP ports for unit tests; started by run_tests.py"""

import argparse
import BaseHTTPServer
import hashlib
import os
import socket
import sys
import time

argp = argparse.ArgumentParser(description='Server for httpcli_test')
argp.add_argument('-p', '--port', default=12345, type=int)
args = argp.parse_args()

print 'port server running on port %d' % args.port

pool = []
in_use = {}

with open(sys.argv[0]) as f:
  _MY_VERSION = hashlib.sha1(f.read()).hexdigest()


def refill_pool():
  """Scan for ports not marked for being in use"""
  for i in range(10000, 65000):
    if len(pool) > 100: break
    if i in in_use:
      age = time.time() - in_use[i]
      if age < 600:
        continue
      del in_use[i]
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
      s.bind(('localhost', i))
      pool.append(i)
    except:
      pass # we really don't care about failures
    finally:
      s.close()


def allocate_port():
  global pool
  global in_use
  if not pool:
    refill_pool()
  port = pool[0]
  pool = pool[1:]
  in_use[port] = time.time()
  return port


class Handler(BaseHTTPServer.BaseHTTPRequestHandler):

  def do_GET(self):
    if self.path == '/get':
      # allocate a new port, it will stay bound for ten minutes and until
      # it's unused
      self.send_response(200)
      self.send_header('Content-Type', 'text/plain')
      self.end_headers()
      p = allocate_port()
      self.log_message('allocated port %d' % p)
      self.wfile.write('%d' % p)
    elif self.path == '/version_and_pid':
      # fetch a version string and the current process pid
      self.send_response(200)
      self.send_header('Content-Type', 'text/plain')
      self.end_headers()
      self.wfile.write('%s+%d' % (_MY_VERSION, os.getpid()))


BaseHTTPServer.HTTPServer(('', args.port), Handler).serve_forever()


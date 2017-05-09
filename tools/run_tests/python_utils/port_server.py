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

"""Manage TCP ports for unit tests; started by run_tests.py"""

import argparse
from BaseHTTPServer import HTTPServer, BaseHTTPRequestHandler
import hashlib
import os
import socket
import sys
import time
import random
from SocketServer import ThreadingMixIn
import threading
import platform


# increment this number whenever making a change to ensure that
# the changes are picked up by running CI servers
# note that all changes must be backwards compatible
_MY_VERSION = 20


if len(sys.argv) == 2 and sys.argv[1] == 'dump_version':
  print _MY_VERSION
  sys.exit(0)


argp = argparse.ArgumentParser(description='Server for httpcli_test')
argp.add_argument('-p', '--port', default=12345, type=int)
argp.add_argument('-l', '--logfile', default=None, type=str)
args = argp.parse_args()

if args.logfile is not None:
  sys.stdin.close()
  sys.stderr.close()
  sys.stdout.close()
  sys.stderr = open(args.logfile, 'w')
  sys.stdout = sys.stderr

print 'port server running on port %d' % args.port

pool = []
in_use = {}
mu = threading.Lock()

def can_connect(port):
  # this test is only really useful on unices where SO_REUSE_PORT is available
  # so on Windows, where this test is expensive, skip it
  if platform.system() == 'Windows': return False
  s = socket.socket()
  try:
    s.connect(('localhost', port))
    return True
  except socket.error, e:
    return False
  finally:
    s.close()

def can_bind(port, proto):
  s = socket.socket(proto, socket.SOCK_STREAM)
  s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
  try:
    s.bind(('localhost', port))
    return True
  except socket.error, e:
    return False
  finally:
    s.close()


def refill_pool(max_timeout, req):
  """Scan for ports not marked for being in use"""
  chk = list(range(1025, 32766))
  random.shuffle(chk)
  for i in chk:
    if len(pool) > 100: break
    if i in in_use:
      age = time.time() - in_use[i]
      if age < max_timeout:
        continue
      req.log_message("kill old request %d" % i)
      del in_use[i]
    if can_bind(i, socket.AF_INET) and can_bind(i, socket.AF_INET6) and not can_connect(i):
      req.log_message("found available port %d" % i)
      pool.append(i)


def allocate_port(req):
  global pool
  global in_use
  global mu
  mu.acquire()
  max_timeout = 600
  while not pool:
    refill_pool(max_timeout, req)
    if not pool:
      req.log_message("failed to find ports: retrying soon")
      mu.release()
      time.sleep(1)
      mu.acquire()
      max_timeout /= 2
  port = pool[0]
  pool = pool[1:]
  in_use[port] = time.time()
  mu.release()
  return port


keep_running = True


class Handler(BaseHTTPRequestHandler):

  def setup(self):
    # If the client is unreachable for 5 seconds, close the connection
    self.timeout = 5
    BaseHTTPRequestHandler.setup(self)

  def do_GET(self):
    global keep_running
    global mu
    if self.path == '/get':
      # allocate a new port, it will stay bound for ten minutes and until
      # it's unused
      self.send_response(200)
      self.send_header('Content-Type', 'text/plain')
      self.end_headers()
      p = allocate_port(self)
      self.log_message('allocated port %d' % p)
      self.wfile.write('%d' % p)
    elif self.path[0:6] == '/drop/':
      self.send_response(200)
      self.send_header('Content-Type', 'text/plain')
      self.end_headers()
      p = int(self.path[6:])
      mu.acquire()
      if p in in_use:
        del in_use[p]
        pool.append(p)
        k = 'known'
      else:
        k = 'unknown'
      mu.release()
      self.log_message('drop %s port %d' % (k, p))
    elif self.path == '/version_number':
      # fetch a version string and the current process pid
      self.send_response(200)
      self.send_header('Content-Type', 'text/plain')
      self.end_headers()
      self.wfile.write(_MY_VERSION)
    elif self.path == '/dump':
      # yaml module is not installed on Macs and Windows machines by default
      # so we import it lazily (/dump action is only used for debugging)
      import yaml
      self.send_response(200)
      self.send_header('Content-Type', 'text/plain')
      self.end_headers()
      mu.acquire()
      now = time.time()
      out = yaml.dump({'pool': pool, 'in_use': dict((k, now - v) for k, v in in_use.items())})
      mu.release()
      self.wfile.write(out)
    elif self.path == '/quitquitquit':
      self.send_response(200)
      self.end_headers()
      self.server.shutdown()

class ThreadedHTTPServer(ThreadingMixIn, HTTPServer):
  """Handle requests in a separate thread"""

ThreadedHTTPServer(('', args.port), Handler).serve_forever()

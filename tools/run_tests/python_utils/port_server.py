#!/usr/bin/env python2.7
# Copyright 2015 gRPC authors.
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
"""Manage TCP ports for unit tests; started by run_tests.py"""

from __future__ import print_function

import argparse
from six.moves.BaseHTTPServer import HTTPServer, BaseHTTPRequestHandler
from six.moves.socketserver import ThreadingMixIn
import hashlib
import os
import socket
import sys
import time
import random
import threading
import platform

# increment this number whenever making a change to ensure that
# the changes are picked up by running CI servers
# note that all changes must be backwards compatible
_MY_VERSION = 21

if len(sys.argv) == 2 and sys.argv[1] == 'dump_version':
    print(_MY_VERSION)
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

print('port server running on port %d' % args.port)

pool = []
in_use = {}
mu = threading.Lock()

# Cronet restricts the following ports to be used (see
# https://cs.chromium.org/chromium/src/net/base/port_util.cc). When one of these
# ports is used in a Cronet test, the test would fail (see issue #12149). These
# ports must be excluded from pool.
cronet_restricted_ports = [
    1, 7, 9, 11, 13, 15, 17, 19, 20, 21, 22, 23, 25, 37, 42, 43, 53, 77, 79, 87,
    95, 101, 102, 103, 104, 109, 110, 111, 113, 115, 117, 119, 123, 135, 139,
    143, 179, 389, 465, 512, 513, 514, 515, 526, 530, 531, 532, 540, 556, 563,
    587, 601, 636, 993, 995, 2049, 3659, 4045, 6000, 6665, 6666, 6667, 6668,
    6669, 6697
]


def can_connect(port):
    # this test is only really useful on unices where SO_REUSE_PORT is available
    # so on Windows, where this test is expensive, skip it
    if platform.system() == 'Windows':
        return False
    s = socket.socket()
    try:
        s.connect(('localhost', port))
        return True
    except socket.error as e:
        return False
    finally:
        s.close()


def can_bind(port, proto):
    s = socket.socket(proto, socket.SOCK_STREAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        s.bind(('localhost', port))
        return True
    except socket.error as e:
        return False
    finally:
        s.close()


def refill_pool(max_timeout, req):
    """Scan for ports not marked for being in use"""
    chk = [
        port for port in range(1025, 32766)
        if port not in cronet_restricted_ports
    ]
    random.shuffle(chk)
    for i in chk:
        if len(pool) > 100:
            break
        if i in in_use:
            age = time.time() - in_use[i]
            if age < max_timeout:
                continue
            req.log_message("kill old request %d" % i)
            del in_use[i]
        if can_bind(i, socket.AF_INET) and can_bind(
                i, socket.AF_INET6) and not can_connect(i):
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
            self.wfile.write(str(p).encode('ascii'))
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
            self.wfile.write(str(_MY_VERSION).encode('ascii'))
        elif self.path == '/dump':
            # yaml module is not installed on Macs and Windows machines by default
            # so we import it lazily (/dump action is only used for debugging)
            import yaml
            self.send_response(200)
            self.send_header('Content-Type', 'text/plain')
            self.end_headers()
            mu.acquire()
            now = time.time()
            out = yaml.dump({
                'pool': pool,
                'in_use': dict((k, now - v) for k, v in in_use.items())
            })
            mu.release()
            self.wfile.write(out.encode('ascii'))
        elif self.path == '/quitquitquit':
            self.send_response(200)
            self.end_headers()
            self.server.shutdown()


class ThreadedHTTPServer(ThreadingMixIn, HTTPServer):
    """Handle requests in a separate thread"""


ThreadedHTTPServer(('', args.port), Handler).serve_forever()

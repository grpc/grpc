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

"""Server for httpcli_test"""

import argparse
import BaseHTTPServer
import os
import ssl
import sys

_PEM = os.path.abspath(os.path.join(os.path.dirname(sys.argv[0]), '../../..', 'src/core/tsi/test_creds/server1.pem'))
_KEY = os.path.abspath(os.path.join(os.path.dirname(sys.argv[0]), '../../..', 'src/core/tsi/test_creds/server1.key'))
print _PEM
open(_PEM).close()

argp = argparse.ArgumentParser(description='Server for httpcli_test')
argp.add_argument('-p', '--port', default=10080, type=int)
argp.add_argument('-s', '--ssl', default=False, action='store_true')
args = argp.parse_args()

print 'server running on port %d' % args.port

class Handler(BaseHTTPServer.BaseHTTPRequestHandler):
	def good(self):
		self.send_response(200)
		self.send_header('Content-Type', 'text/html')
		self.end_headers()
		self.wfile.write('<html><head><title>Hello world!</title></head>')
		self.wfile.write('<body><p>This is a test</p></body></html>')

	def do_GET(self):
		if self.path == '/get':
			self.good()

	def do_POST(self):
		content = self.rfile.read(int(self.headers.getheader('content-length')))
		if self.path == '/post' and content == 'hello':
			self.good()

httpd = BaseHTTPServer.HTTPServer(('localhost', args.port), Handler)
if args.ssl:
	httpd.socket = ssl.wrap_socket(httpd.socket, certfile=_PEM, keyfile=_KEY, server_side=True)
httpd.serve_forever()

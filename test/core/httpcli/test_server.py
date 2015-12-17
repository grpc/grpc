#!/usr/bin/env python2.7

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

#!/usr/bin/env python2.7

"""Server for httpcli_test"""

import argparse
import BaseHTTPServer

argp = argparse.ArgumentParser(description='Server for httpcli_test')
argp.add_argument('-p', '--port', default=10080, type=int)
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

BaseHTTPServer.HTTPServer(('', args.port), Handler).serve_forever()

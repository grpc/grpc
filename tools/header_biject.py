#!/usr/bin/env python2.7

import urllib

def is_allowed(c):
  return c in '0123456789abcdefghijklmnopqrstuvwxyz.-_'

def pctenc(c):
  return '%%%02x' % ord(c)

def url_to_header(url):
  if isinstance(url, int):
    return 'grpc-details_int-%d' % url
  return 'grpc-details-%s' % ''.join(c if is_allowed(c) else pctenc(c) for c in url)

def header_to_url(header):
  if header[0:17] == 'grpc-details_int-':
    return int(header[17:])
  if header[0:13] == 'grpc-details-':
    return urllib.unquote(header[13:])

def test(url):
  print '%r ---> %r ---> %r' % (url, url_to_header(url), header_to_url(url_to_header(url)))
  assert header_to_url(url_to_header(url)) == url

test(3498123)
test('foo')
test('\0\1\2')
test('a/b/c')
test('1.2.3')
test('type.googleapis.com/google.protobuf.Duration')


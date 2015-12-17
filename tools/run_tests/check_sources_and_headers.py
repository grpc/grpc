#!/usr/bin/env python2.7

import json
import os
import re
import sys

root = os.path.abspath(os.path.join(os.path.dirname(sys.argv[0]), '../..'))
with open(os.path.join(root, 'tools', 'run_tests', 'sources_and_headers.json')) as f:
	js = json.loads(f.read())

re_inc1 = re.compile(r'^#\s*include\s*"([^"]*)"')
assert re_inc1.match('#include "foo"').group(1) == 'foo'
re_inc2 = re.compile(r'^#\s*include\s*<((grpc|grpc\+\+)/[^"]*)>')
assert re_inc2.match('#include <grpc++/foo>').group(1) == 'grpc++/foo'

def get_target(name):
	for target in js:
		if target['name'] == name:
			return target
	assert False, 'no target %s' % name

def target_has_header(target, name):
#	print target['name'], name
	if name in target['headers']:
		return True
	for dep in target['deps']:
		if target_has_header(get_target(dep), name):
			return True
	if name == 'src/core/profiling/stap_probes.h':
		return True
	return False

errors = 0
for target in js:
	for fn in target['src']:
		with open(os.path.join(root, fn)) as f:
			src = f.read().splitlines()
		for line in src:
			m = re_inc1.match(line)
			if m:
				if not target_has_header(target, m.group(1)):
					print (
						'target %s (%s) does not name header %s as a dependency' % (
							target['name'], fn, m.group(1)))
					errors += 1
			m = re_inc2.match(line)
			if m:
				if not target_has_header(target, 'include/' + m.group(1)):
					print (
						'target %s (%s) does not name header %s as a dependency' % (
							target['name'], fn, m.group(1)))
					errors += 1

assert errors == 0
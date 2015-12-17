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

import shutil
import sys
import os
import yaml

boring_ssl_root = os.path.abspath(os.path.join(
		os.path.dirname(sys.argv[0]), 
		'../../third_party/boringssl'))
sys.path.append(os.path.join(boring_ssl_root, 'util'))

import generate_build_files

def map_dir(filename):
	if filename[0:4] == 'src/':
		return 'third_party/boringssl/' + filename[4:]
	else:
		return 'src/boringssl/' + filename

class Grpc(object):

	yaml = None

	def WriteFiles(self, files, asm_outputs):
		#print 'files: %r' % files
		#print 'asm_outputs: %r' % asm_outputs

		self.yaml = {
			'#': 'generated with tools/buildgen/gen_boring_ssl_build_yaml.py',
			'libs': [
					{
						'name': 'boringssl',
						'build': 'private',
						'language': 'c',
						'src': [
							map_dir(f)
							for f in files['ssl'] + files['crypto']
						],
						'headers': [
							map_dir(f)
							for f in files['ssl_headers'] + files['crypto_headers']
						]
					}
			]
		}


os.chdir(os.path.dirname(sys.argv[0]))
os.mkdir('src')
try:
	for f in os.listdir(boring_ssl_root):
		os.symlink(os.path.join(boring_ssl_root, f),
							 os.path.join('src', f))

	g = Grpc()
	generate_build_files.main([g])

	print yaml.dump(g.yaml)

finally:
	shutil.rmtree('src')

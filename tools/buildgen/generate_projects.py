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

import glob
import os
import shutil
import sys
import tempfile
import multiprocessing
sys.path.append(os.path.join(os.path.dirname(sys.argv[0]), '..', 'run_tests'))

assert sys.argv[1:], 'run generate_projects.sh instead of this directly'

import jobset

os.chdir(os.path.join(os.path.dirname(sys.argv[0]), '..', '..'))
json = sys.argv[1:]

test = {} if 'TEST' in os.environ else None

plugins = sorted(glob.glob('tools/buildgen/plugins/*.py'))

jobs = []
for root, dirs, files in os.walk('templates'):
  for f in files:
    if os.path.splitext(f)[1] == '.template':
      out_dir = '.' + root[len('templates'):]
      out = out_dir + '/' + os.path.splitext(f)[0]
      if not os.path.exists(out_dir):
        os.makedirs(out_dir)
      cmd = ['python2.7', 'tools/buildgen/mako_renderer.py']
      for plugin in plugins:
        cmd.append('-p')
        cmd.append(plugin)
      for js in json:
        cmd.append('-d')
        cmd.append(js)
      cmd.append('-o')
      if test is None:
        cmd.append(out)
      else:
        tf = tempfile.mkstemp()
        test[out] = tf[1]
        os.close(tf[0])
        cmd.append(test[out])
      cmd.append(root + '/' + f)
      jobs.append(jobset.JobSpec(cmd, shortname=out))

jobset.run(jobs, maxjobs=multiprocessing.cpu_count())

if test is not None:
  for s, g in test.iteritems():
    if os.path.isfile(g):
      assert 0 == os.system('diff %s %s' % (s, g)), s
      os.unlink(g)
    else:
      assert 0 == os.system('diff -r %s %s' % (s, g)), s
      shutil.rmtree(g, ignore_errors=True)

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

import argparse
import glob
import os
import shutil
import sys
import tempfile
import multiprocessing
sys.path.append(os.path.join(os.path.dirname(sys.argv[0]), '..', 'run_tests', 'python_utils'))

assert sys.argv[1:], 'run generate_projects.sh instead of this directly'

import jobset

os.chdir(os.path.join(os.path.dirname(sys.argv[0]), '..', '..'))

argp = argparse.ArgumentParser()
argp.add_argument('build_files', nargs='+', default=[])
argp.add_argument('--templates', nargs='+', default=[])
argp.add_argument('--output_merged', default=None, type=str)
argp.add_argument('--jobs', '-j', default=multiprocessing.cpu_count(), type=int)
argp.add_argument('--base', default='.', type=str)
args = argp.parse_args()

json = args.build_files

test = {} if 'TEST' in os.environ else None

plugins = sorted(glob.glob('tools/buildgen/plugins/*.py'))

templates = args.templates
if not templates:
  for root, dirs, files in os.walk('templates'):
    for f in files:
      templates.append(os.path.join(root, f))

pre_jobs = []
base_cmd = ['python2.7', 'tools/buildgen/mako_renderer.py']
cmd = base_cmd[:]
for plugin in plugins:
  cmd.append('-p')
  cmd.append(plugin)
for js in json:
  cmd.append('-d')
  cmd.append(js)
cmd.append('-w')
preprocessed_build = '.preprocessed_build'
cmd.append(preprocessed_build)
if args.output_merged is not None:
  cmd.append('-M')
  cmd.append(args.output_merged)
pre_jobs.append(jobset.JobSpec(cmd, shortname='preprocess', timeout_seconds=None))

jobs = []
for template in reversed(sorted(templates)):
  root, f = os.path.split(template)
  if os.path.splitext(f)[1] == '.template':
    out_dir = args.base + root[len('templates'):]
    out = out_dir + '/' + os.path.splitext(f)[0]
    if not os.path.exists(out_dir):
      os.makedirs(out_dir)
    cmd = base_cmd[:]
    cmd.append('-P')
    cmd.append(preprocessed_build)
    cmd.append('-o')
    if test is None:
      cmd.append(out)
    else:
      tf = tempfile.mkstemp()
      test[out] = tf[1]
      os.close(tf[0])
      cmd.append(test[out])
    cmd.append(args.base + '/' + root + '/' + f)
    jobs.append(jobset.JobSpec(cmd, shortname=out, timeout_seconds=None))

jobset.run(pre_jobs, maxjobs=args.jobs)
jobset.run(jobs, maxjobs=args.jobs)

if test is not None:
  for s, g in test.iteritems():
    if os.path.isfile(g):
      assert 0 == os.system('diff %s %s' % (s, g)), s
      os.unlink(g)
    else:
      assert 0 == os.system('diff -r %s %s' % (s, g)), s
      shutil.rmtree(g, ignore_errors=True)

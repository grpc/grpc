#!/usr/bin/env python2.7
# Copyright 2017, Google Inc.
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

import multiprocessing
import os
import subprocess
import sys

import python_utils.jobset as jobset
import python_utils.start_port_server as start_port_server

flamegraph_dir = os.path.join(os.path.expanduser('~'), 'FlameGraph')

os.chdir(os.path.join(os.path.dirname(sys.argv[0]), '../..'))
if not os.path.exists('reports'):
  os.makedirs('reports')

port_server_port = 32766
start_port_server.start_port_server(port_server_port)

def fnize(s):
  out = ''
  for c in s:
    if c in '<>, /':
      if len(out) and out[-1] == '_': continue
      out += '_'
    else:
      out += c
  return out

# index html
index_html = """
<html>
<head>
<title>Microbenchmark Results</title>
</head>
<body>
"""

def heading(name):
  global index_html
  index_html += "<h1>%s</h1>\n" % name

def link(txt, tgt):
  global index_html
  index_html += "<p><a href=\"%s\">%s</a></p>\n" % (tgt, txt)

benchmarks = []
profile_analysis = []
cleanup = []

for bm_name in sys.argv[1:]:
  # generate latency profiles
  heading('Latency Profiles: %s' % bm_name)
  subprocess.check_call(
      ['make', bm_name,
       'CONFIG=basicprof', '-j', '%d' % multiprocessing.cpu_count()])
  for line in subprocess.check_output(['bins/basicprof/%s' % bm_name,
                                       '--benchmark_list_tests']).splitlines():
    link(line, '%s.txt' % fnize(line))
    benchmarks.append(
        jobset.JobSpec(['bins/basicprof/%s' % bm_name, '--benchmark_filter=^%s$' % line],
                       environ={'LATENCY_TRACE': '%s.trace' % fnize(line)}))
    profile_analysis.append(
        jobset.JobSpec([sys.executable,
                        'tools/profiling/latency_profile/profile_analyzer.py',
                        '--source', '%s.trace' % fnize(line), '--fmt', 'simple',
                        '--out', 'reports/%s.txt' % fnize(line)], timeout_seconds=None))
    cleanup.append(jobset.JobSpec(['rm', '%s.trace' % fnize(line)]))
    if len(benchmarks) >= 2 * multiprocessing.cpu_count():
      jobset.run(benchmarks, maxjobs=multiprocessing.cpu_count()/2,
                 add_env={'GRPC_TEST_PORT_SERVER': 'localhost:%d' % port_server_port})
      jobset.run(profile_analysis, maxjobs=multiprocessing.cpu_count())
      jobset.run(cleanup, maxjobs=multiprocessing.cpu_count())
      benchmarks = []
      profile_analysis = []
      cleanup = []
  if len(benchmarks):
    jobset.run(benchmarks, maxjobs=multiprocessing.cpu_count()/2,
               add_env={'GRPC_TEST_PORT_SERVER': 'localhost:%d' % port_server_port})
    jobset.run(profile_analysis, maxjobs=multiprocessing.cpu_count())
    jobset.run(cleanup, maxjobs=multiprocessing.cpu_count())

  # generate flamegraphs
  heading('Flamegraphs: %s' % bm_name)
  subprocess.check_call(
      ['make', bm_name,
       'CONFIG=mutrace', '-j', '%d' % multiprocessing.cpu_count()])
  for line in subprocess.check_output(['bins/mutrace/%s' % bm_name,
                                       '--benchmark_list_tests']).splitlines():
    subprocess.check_call(['sudo', 'perf', 'record', '-g', '-c', '1000',
                           'bins/mutrace/%s' % bm_name,
                           '--benchmark_filter=^%s$' % line,
                           '--benchmark_min_time=20'])
    with open('/tmp/bm.perf', 'w') as f:
      f.write(subprocess.check_output(['sudo', 'perf', 'script']))
    with open('/tmp/bm.folded', 'w') as f:
      f.write(subprocess.check_output([
          '%s/stackcollapse-perf.pl' % flamegraph_dir, '/tmp/bm.perf']))
    link(line, '%s.svg' % fnize(line))
    with open('reports/%s.svg' % fnize(line), 'w') as f:
      f.write(subprocess.check_output([
          '%s/flamegraph.pl' % flamegraph_dir, '/tmp/bm.folded']))

index_html += "</body>\n</html>\n"
with open('reports/index.html', 'w') as f:
  w.write(index_html)

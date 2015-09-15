#!/usr/bin/env python
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

"""Run interop (cross-language) tests in parallel."""

import argparse
import itertools
import xml.etree.cElementTree as ET
import jobset

# TODO(jtattermusch): add php and python once we get them working
_LANGUAGES = ['c++', 'node', 'csharp', 'ruby']

# TODO(jtattermusch): add empty_stream once C++ start supporting it.
# TODO(jtattermusch): add support for auth tests.
_TEST_CASES = ['large_unary', 'empty_unary', 'ping_pong',
               'client_streaming', 'server_streaming',
               'cancel_after_begin', 'cancel_after_first_response',
               'timeout_on_sleeping_server']

argp = argparse.ArgumentParser(description='Run interop tests.')
argp.add_argument('-l', '--language',
                  choices=['all'] + sorted(_LANGUAGES),
                  nargs='+',
                  default=['all'])
args = argp.parse_args()

languages = [l for l in itertools.chain.from_iterable(
                      iter(_LANGUAGES) if x == 'all' else [x]
                      for x in args.language)]

jobs = []
jobNumber = 0
for language in languages:
  for test in _TEST_CASES:
    test_job = jobset.JobSpec(
          cmdline=['tools/run_tests/run_interop_test.sh', '%s' % language, '%s' % test], 
          shortname="cloud_to_prod:%s:%s" % (language, test),
          timeout_seconds=60)
    jobs.append(test_job)
    jobNumber+=1

root = ET.Element('testsuites')
testsuite = ET.SubElement(root, 'testsuite', id='1', package='grpc', name='tests')

jobset.run(jobs, maxjobs=jobNumber, xml_report=testsuite)

tree = ET.ElementTree(root)
tree.write('report.xml', encoding='UTF-8')



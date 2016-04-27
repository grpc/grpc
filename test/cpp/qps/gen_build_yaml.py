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

import json
import pipes
import shutil
import sys
import os
import yaml

run_tests_root = os.path.abspath(os.path.join(
    os.path.dirname(sys.argv[0]),
    '../../../tools/run_tests'))
sys.path.append(run_tests_root)

import performance.scenario_config as scenario_config

print yaml.dump({
  'tests': [
    {
      'name': 'json_run_localhost',
      'shortname': 'json_run_localhost:%s' % js['name'],
      'args': ['--scenario_json', pipes.quote(json.dumps(js))],
      'ci_platforms': ['linux', 'mac', 'posix', 'windows'],
      'platforms': ['linux', 'mac', 'posix', 'windows'],
      'flaky': False,
      'language': 'c++',
      'boringssl': True,
      'defaults': 'boringssl',
      'cpu_cost': 1000.0,
      'exclude_configs': []
    }
    for js in scenario_config.CXXLanguage().scenarios()
  ]
})

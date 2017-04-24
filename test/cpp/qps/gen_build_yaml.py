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

configs_from_yaml = yaml.load(open(os.path.join(os.path.dirname(sys.argv[0]), '../../../build.yaml')))['configs'].keys()

def mutate_scenario(scenario_json, is_tsan):
  # tweak parameters to get fast test times
  scenario_json = dict(scenario_json)
  scenario_json['warmup_seconds'] = 0
  scenario_json['benchmark_seconds'] = 1
  outstanding_rpcs_divisor = 1
  if is_tsan and (
      scenario_json['client_config']['client_type'] == 'SYNC_CLIENT' or
      scenario_json['server_config']['server_type'] == 'SYNC_SERVER'):
    outstanding_rpcs_divisor = 10
  scenario_json['client_config']['outstanding_rpcs_per_channel'] = max(1,
      int(scenario_json['client_config']['outstanding_rpcs_per_channel'] / outstanding_rpcs_divisor))
  return scenario_json

def _scenario_json_string(scenario_json, is_tsan):
  scenarios_json = {'scenarios': [scenario_config.remove_nonproto_fields(mutate_scenario(scenario_json, is_tsan))]}
  return json.dumps(scenarios_json)

def threads_required(scenario_json, where, is_tsan):
  scenario_json = mutate_scenario(scenario_json, is_tsan)
  if scenario_json['%s_config' % where]['%s_type' % where] == 'ASYNC_%s' % where.upper():
    return scenario_json['%s_config' % where].get('async_%s_threads' % where, 0)
  return scenario_json['client_config']['outstanding_rpcs_per_channel'] * scenario_json['client_config']['client_channels']

def guess_cpu(scenario_json, is_tsan):
  client = threads_required(scenario_json, 'client', is_tsan)
  server = threads_required(scenario_json, 'server', is_tsan)
  # make an arbitrary guess if set to auto-detect
  # about the size of the jenkins instances we have for unit tests
  if client == 0 or server == 0: return 'capacity'
  return (scenario_json['num_clients'] * client +
          scenario_json['num_servers'] * server)

print yaml.dump({
  'tests': [
    {
      'name': 'json_run_localhost',
      'shortname': 'json_run_localhost:%s' % scenario_json['name'],
      'args': ['--scenarios_json', _scenario_json_string(scenario_json, False)],
      'ci_platforms': ['linux'],
      'platforms': ['linux'],
      'flaky': False,
      'language': 'c++',
      'boringssl': True,
      'defaults': 'boringssl',
      'cpu_cost': guess_cpu(scenario_json, False),
      'exclude_configs': ['tsan', 'asan'],
      'timeout_seconds': 6*60,
      'excluded_poll_engines': scenario_json.get('EXCLUDED_POLL_ENGINES', [])
    }
    for scenario_json in scenario_config.CXXLanguage().scenarios()
    if 'scalable' in scenario_json.get('CATEGORIES', [])
  ] + [
    {
      'name': 'json_run_localhost',
      'shortname': 'json_run_localhost:%s_low_thread_count' % scenario_json['name'],
      'args': ['--scenarios_json', _scenario_json_string(scenario_json, True)],
      'ci_platforms': ['linux'],
      'platforms': ['linux'],
      'flaky': False,
      'language': 'c++',
      'boringssl': True,
      'defaults': 'boringssl',
      'cpu_cost': guess_cpu(scenario_json, True),
      'exclude_configs': sorted(c for c in configs_from_yaml if c not in ('tsan', 'asan')),
      'timeout_seconds': 6*60,
      'excluded_poll_engines': scenario_json.get('EXCLUDED_POLL_ENGINES', [])
   }
    for scenario_json in scenario_config.CXXLanguage().scenarios()
    if 'scalable' in scenario_json.get('CATEGORIES', [])
  ]
})

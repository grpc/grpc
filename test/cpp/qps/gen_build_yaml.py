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

from __future__ import print_function
import json
import pipes
import shutil
import sys
import os
import yaml

run_tests_root = os.path.abspath(
    os.path.join(os.path.dirname(sys.argv[0]), '../../../tools/run_tests'))
sys.path.append(run_tests_root)

import performance.scenario_config as scenario_config

configs_from_yaml = yaml.load(
    open(
        os.path.join(os.path.dirname(sys.argv[0]),
                     '../../../build_handwritten.yaml')))['configs'].keys()


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
    scenario_json['client_config']['outstanding_rpcs_per_channel'] = max(
        1,
        int(scenario_json['client_config']['outstanding_rpcs_per_channel'] /
            outstanding_rpcs_divisor))
    return scenario_json


def _scenario_json_string(scenario_json, is_tsan):
    scenarios_json = {
        'scenarios': [
            scenario_config.remove_nonproto_fields(
                mutate_scenario(scenario_json, is_tsan))
        ]
    }
    return json.dumps(scenarios_json)


def threads_required(scenario_json, where, is_tsan):
    scenario_json = mutate_scenario(scenario_json, is_tsan)
    if scenario_json['%s_config' % where]['%s_type' %
                                          where] == 'ASYNC_%s' % where.upper():
        return scenario_json['%s_config' % where].get(
            'async_%s_threads' % where, 0)
    return scenario_json['client_config'][
        'outstanding_rpcs_per_channel'] * scenario_json['client_config'][
            'client_channels']


def guess_cpu(scenario_json, is_tsan):
    client = threads_required(scenario_json, 'client', is_tsan)
    server = threads_required(scenario_json, 'server', is_tsan)
    # make an arbitrary guess if set to auto-detect
    # about the size of the jenkins instances we have for unit tests
    if client == 0 or server == 0:
        return 'capacity'
    return (scenario_json['num_clients'] * client +
            scenario_json['num_servers'] * server)


def maybe_exclude_gcov(scenario_json):
    if scenario_json['client_config']['client_channels'] > 100:
        return ['gcov']
    return []


# Originally, this method was used to generate qps test cases for build.yaml,
# but since the test cases are now extracted from bazel BUILD file,
# this is not used for generating run_tests.py test cases anymore.
# Nevertheless, the output is still used by json_run_localhost_scenario_gen.py
# and qps_json_driver_scenario_gen.py to generate the scenario list for bazel.
# TODO(jtattermusch): cleanup this file, so that it only generates data needed
# by bazel.
def generate_yaml():
    return {
        'tests':
            [{
                'name':
                    'json_run_localhost',
                'shortname':
                    'json_run_localhost:%s' % scenario_json['name'],
                'args': [
                    '--scenarios_json',
                    _scenario_json_string(scenario_json, False)
                ],
                'ci_platforms': ['linux'],
                'platforms': ['linux'],
                'flaky':
                    False,
                'language':
                    'c++',
                'boringssl':
                    True,
                'defaults':
                    'boringssl',
                'cpu_cost':
                    guess_cpu(scenario_json, False),
                'exclude_configs': ['tsan', 'asan'] +
                                   maybe_exclude_gcov(scenario_json),
                'timeout_seconds':
                    2 * 60,
                'excluded_poll_engines':
                    scenario_json.get('EXCLUDED_POLL_ENGINES', []),
                'auto_timeout_scaling':
                    False
            }
             for scenario_json in scenario_config.CXXLanguage().scenarios()
             if 'scalable' in scenario_json.get('CATEGORIES', [])] +
            [{
                'name':
                    'qps_json_driver',
                'shortname':
                    'qps_json_driver:inproc_%s' % scenario_json['name'],
                'args': [
                    '--run_inproc', '--scenarios_json',
                    _scenario_json_string(scenario_json, False)
                ],
                'ci_platforms': ['linux'],
                'platforms': ['linux'],
                'flaky':
                    False,
                'language':
                    'c++',
                'boringssl':
                    True,
                'defaults':
                    'boringssl',
                'cpu_cost':
                    guess_cpu(scenario_json, False),
                'exclude_configs': ['tsan', 'asan'],
                'timeout_seconds':
                    6 * 60,
                'excluded_poll_engines':
                    scenario_json.get('EXCLUDED_POLL_ENGINES', [])
            }
             for scenario_json in scenario_config.CXXLanguage().scenarios()
             if 'inproc' in scenario_json.get('CATEGORIES', [])] +
            [{
                'name':
                    'json_run_localhost',
                'shortname':
                    'json_run_localhost:%s_low_thread_count' %
                    scenario_json['name'],
                'args': [
                    '--scenarios_json',
                    _scenario_json_string(scenario_json, True)
                ],
                'ci_platforms': ['linux'],
                'platforms': ['linux'],
                'flaky':
                    False,
                'language':
                    'c++',
                'boringssl':
                    True,
                'defaults':
                    'boringssl',
                'cpu_cost':
                    guess_cpu(scenario_json, True),
                'exclude_configs':
                    sorted(c
                           for c in configs_from_yaml
                           if c not in ('tsan', 'asan')),
                'timeout_seconds':
                    10 * 60,
                'excluded_poll_engines':
                    scenario_json.get('EXCLUDED_POLL_ENGINES', []),
                'auto_timeout_scaling':
                    False
            }
             for scenario_json in scenario_config.CXXLanguage().scenarios()
             if 'scalable' in scenario_json.get('CATEGORIES', [])]
    }

#!/usr/bin/env python

# Copyright 2018 gRPC authors.
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

import os
import sys
import subprocess

os.chdir(os.path.join(os.path.dirname(sys.argv[0]), '../../../test/cpp/qps'))
subprocess.call(['./json_run_localhost_scenario_gen.py'])
subprocess.call(['./qps_json_driver_scenario_gen.py'])
subprocess.call(['buildifier', '-v', '-r', '.'])

output = subprocess.check_output(['git', 'status', '--porcelain'])
qps_json_driver_bzl = 'test/cpp/qps/qps_json_driver_scenarios.bzl'
json_run_localhost_bzl = 'test/cpp/qps/json_run_localhost_scenarios.bzl'

if qps_json_driver_bzl in output or json_run_localhost_bzl in output:
    print('qps benchmark scenarios have been updated, please commit '
          'test/cpp/qps/qps_json_driver_scenarios.bzl and/or '
          'test/cpp/qps/json_run_localhost_scenarios.bzl')
    sys.exit(1)

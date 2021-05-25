#!/usr/bin/env python2.7

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

import gen_build_yaml as gen
import json

COPYRIGHT = """
# Copyright 2021 The gRPC Authors
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
"""


def generate_args():
    all_scenario_set = gen.generate_yaml()
    all_scenario_set = all_scenario_set['tests']
    qps_json_driver_scenario_set = \
        [item for item in all_scenario_set if item['name'] == 'qps_json_driver']
    qps_json_driver_arg_set = \
        [item['args'][2] for item in qps_json_driver_scenario_set \
        if 'args' in item and len(item['args']) > 2]
    deserialized_scenarios = [json.loads(item)['scenarios'][0] \
                            for item in qps_json_driver_arg_set]
    all_scenarios = {scenario['name'].encode('ascii', 'ignore'): \
                   '\'{\'scenarios\' : [' + json.dumps(scenario) + ']}\'' \
                   for scenario in deserialized_scenarios}

    serialized_scenarios_str = str(all_scenarios).encode('ascii', 'ignore')
    with open('qps_json_driver_scenarios.bzl', 'w') as f:
        f.write(COPYRIGHT)
        f.write('"""Scenarios of qps driver."""\n\n')
        f.write('QPS_JSON_DRIVER_SCENARIOS = ' + serialized_scenarios_str +
                '\n')


generate_args()

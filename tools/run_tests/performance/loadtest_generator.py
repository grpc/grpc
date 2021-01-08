#!/usr/bin/env python3

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

# Generate a multi-document YAML file that contains load tests based
# on scenario json definitions and a template load test definition.

import json
import sys
import scenario_config_exporter
import yaml
import uuid

if __name__ == "__main__":
    # Load the scenarios to run from scenario_config.py
    scenarios = scenario_config_exporter.get_json_scenarios(
        'csharp', scenario_name_regex='.*', category='scalable', keep_crosslanguage=False)

    loadtest_template_file = '/usr/local/google/home/jtattermusch/github/test-infra/config/samples/csharp_prebuilt_example.yaml'

    with open(loadtest_template_file, 'r') as f:
        loadtest_template_str = f.read()

    load_tests = []
    for scenario in scenarios:
        load_test = yaml.load(loadtest_template_str, Loader=yaml.FullLoader)
        # generate unique name for each of the loadtest
        load_test['metadata']['name'] += "-" + str(uuid.uuid4())
        # TODO: add a label to easily identify the loadtests...
        load_test['spec']['scenariosJSON'] = json.dumps(
            {'scenarios': [scenario]})
        load_tests.append(load_test)

    outfile_name = 'generated_loadtests.yaml'
    with open(outfile_name, 'w') as outfile:
        # generate a multi-document YAML with multiple loadtests in it
        yaml.dump_all(load_tests, outfile, default_flow_style=False)
    print('Generated %s scenarios to %s' % (len(scenarios), outfile_name))

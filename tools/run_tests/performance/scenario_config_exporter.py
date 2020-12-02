#!/usr/bin/env python3

# Copyright 2020 The gRPC Authors
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

# Helper script to extract JSON scenario definitions from scenario_config.py
# Useful to construct "ScenariosJSON" configuration accepted by the OSS benchmarks framework
# See https://github.com/grpc/test-infra/blob/master/config/samples/cxx_example_loadtest.yaml

import json
import re
import scenario_config
import sys


def get_json_scenarios(language_name, scenario_name_regex='.*', category='all'):
    """Returns list of scenarios that match given constraints."""
    result = []
    scenarios = scenario_config.LANGUAGES[language_name].scenarios()
    for scenario_json in scenarios:
        if re.search(scenario_name_regex, scenario_json['name']):
            # if the 'CATEGORIES' key is missing, treat scenario as part of 'scalable' and 'smoketest'
            # this matches the behavior of run_performance_tests.py
            scenario_categories = scenario_json.get('CATEGORIES',
                                                    ['scalable', 'smoketest'])
            # TODO(jtattermusch): consider adding filtering for 'CLIENT_LANGUAGE' and 'SERVER_LANGUAGE'
            # fields, before the get stripped away.
            if category in scenario_categories or category == 'all':
                scenario_json_stripped = scenario_config.remove_nonproto_fields(
                    scenario_json)
                result.append(scenario_json_stripped)
    return result


def dump_to_json_files(json_scenarios, filename_prefix='scenario_dump_'):
    """Dump a list of json scenarios to json files"""
    for scenario in json_scenarios:
        filename = "%s%s.json" % (filename_prefix, scenario['name'])
        print('Writing file %s' % filename, file=sys.stderr)
        with open(filename, 'w') as outfile:
            # the dump file should have {"scenarios" : []} as the top level element
            json.dump({'scenarios': [scenario]}, outfile, indent=2)


if __name__ == "__main__":
    # example usage: extract C# scenarios and dump them as .json files
    scenarios = get_json_scenarios('csharp',
                                   scenario_name_regex='.*',
                                   category='scalable')
    dump_to_json_files(scenarios, 'scenario_dump_')

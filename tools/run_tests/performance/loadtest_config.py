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

# Helper script to extract JSON scenario definitions from scenario_config.py
# Useful to construct "ScenariosJSON" configuration accepted by the OSS benchmarks framework
# See https://github.com/grpc/test-infra/blob/master/config/samples/cxx_example_loadtest.yaml


import argparse
import copy
import datetime
import json
import os
import string
import sys

from typing import Any, Dict, Iterable, Optional

import yaml

import scenario_config
import scenario_config_exporter


def default_loadtest_prefix() -> str:
    """Constructs and returns a default prefix for LoadTest names."""
    user = os.environ.get('USER', 'loadtest')
    time_str = datetime.datetime.now().strftime('%Y%m%d%H%M%S')
    return user + time_str


def validate_loadtest_name(name: str) -> None:
    """Validates that a LoadTest name is in the expected format."""
    base_name, _, suffix = name.partition('--')
    if '--' in suffix:
        raise ValueError(
            'Multiple double dashes in LoadTest name: %s' % name)
    elements = base_name.split('-')
    if suffix:
        elements.extend(suffix.split('-'))
    for element in elements:
        if not element.isalnum():
            print(elements, file=sys.stderr)
            raise ValueError(
                'Invalid element in LoadTest name "%s": %s', name, element)


def loadtest_name(prefix: str, scenario_name: str, run_index: str,
                  queue_name: str) -> str:
    """Constructs and returns a valid name for a LoadTest resource."""
    elements = []
    if prefix:
        elements.append(prefix)
    if run_index:
        elements.append(run_index)
    elements.append(scenario_name.replace('_', '-'))
    if queue_name:
        elements.extend(['', queue_name])
    name = '-'.join(elements)
    validate_loadtest_name(name)
    return name


def gen_run_indices(runs_per_test: int) -> Iterable[str]:
    """Generates run indices for multiple runs, as formatted strings."""
    if runs_per_test < 2:
        yield ''
        return
    prefix_length = len('{:d}'.format(runs_per_test - 1))
    prefix_fmt = '{{:{:d}d}}'.format(prefix_length)
    for i in range(runs_per_test):
        yield prefix_fmt.format(i)


def gen_loadtest_configs(base_config: yaml.YAMLObject,
                         scenarios: Iterable[Dict[str, Any]],
                         loadtest_name_prefix: str = '',
                         queue_name: str = '',
                         runs_per_test: int = 1) -> Iterable[yaml.YAMLObject]:
    """Generates LoadTest configurations as YAML objects."""
    prefix = loadtest_name_prefix or default_loadtest_prefix()
    for run_index in gen_run_indices(runs_per_test):
        for scenario in scenarios:

            name = loadtest_name(prefix, scenario['name'],
                                 run_index, queue_name)
            scenario_str = json.dumps({'scenarios': scenario}, indent="  ")
            config = copy.deepcopy(base_config)
            config['metadata']['name'] = name
            config['spec']['scenariosJSON'] = scenario_str
            yield config


def parse_substitution_args(args: Optional[Iterable[str]]) -> Dict[str, str]:
    """Parses arguments in the form key=value into a dictionary."""
    d = dict()
    if args is None:
        return d
    for arg in args:
        key, equals, value = arg.partition('=')
        if equals != '=':
            raise ValueError('Expected key=value: ' + value)
        d[key] = value
    return d


def configure_yaml() -> None:
    """Configures the YAML library to dump data in the expected format."""
    def str_presenter(dumper, data):
        if '\n' in data:
            return dumper.represent_scalar(
                'tag:yaml.org,2002:str', data, style='|')
        return dumper.represent_scalar('tag:yaml.org,2002:str', data)

    yaml.add_representer(str, str_presenter)


def main() -> None:
    language_choices = sorted(scenario_config.LANGUAGES.keys())
    argp = argparse.ArgumentParser(description='Generates load test configs.')
    argp.add_argument('-l',
                      '--language',
                      choices=language_choices,
                      required=True,
                      help='Language to benchmark.')
    argp.add_argument('-t',
                      '--template',
                      type=str,
                      required=True,
                      help='LoadTest configuration yaml file template.')
    argp.add_argument('-s',
                      '--substitutions',
                      action='extend',
                      nargs='+',
                      type=str,
                      help='Template substitutions in the form key=value.')
    argp.add_argument('-p',
                      '--prefix',
                      type=str,
                      default='',
                      help='Test name prefix.')
    argp.add_argument('-q',
                      '--concurrency_queue',
                      type=str,
                      default='',
                      help='Test runner concurrency queue.')
    argp.add_argument('-r',
                      '--regex',
                      default='.*',
                      type=str,
                      help='Regex to select scenarios to run.')
    argp.add_argument('--category',
                      choices=['all', 'scalable', 'smoketest', 'sweep'],
                      default='all',
                      help='Select a category of tests to run.')
    argp.add_argument(
        '--client_language',
        choices=language_choices,
        help='Select only scenarios with a specified client language.')
    argp.add_argument(
        '--server_language',
        choices=language_choices,
        help='Select only scenarios with a specified server language.')
    argp.add_argument('--runs_per_test',
                      default=1,
                      type=int,
                      help='Number of copies to generate for each test.')
    argp.add_argument('-o',
                      '--output',
                      type=str,
                      help='Output file name. Output to stdout if not set.')
    args = argp.parse_args()

    substitution_args_dict = parse_substitution_args(args.substitutions)

    with open(args.template) as f:
        base_config = yaml.safe_load(string.Template(
            f.read()).substitute(substitution_args_dict))

    scenario_filter = scenario_config_exporter.scenario_filter(
        scenario_name_regex=args.regex,
        category=args.category,
        client_language=args.client_language,
        server_language=args.server_language)

    scenarios = scenario_config_exporter.gen_scenarios(
        args.language, scenario_filter)

    configs = gen_loadtest_configs(
        base_config, scenarios, loadtest_name_prefix=args.prefix,
        queue_name=args.concurrency_queue, runs_per_test=args.runs_per_test)

    configure_yaml()

    with open(args.output, 'w') if args.output else sys.stdout as f:
        yaml.dump_all(configs, stream=f)


if __name__ == '__main__':
    main()

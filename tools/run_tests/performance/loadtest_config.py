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

# Script to generate test configurations for the OSS benchmarks framework.
#
# This script filters test scenarios and generates uniquely named configurations
# for each test. Configurations are dumped in multipart YAML format.

import argparse
import copy
import datetime
import json
import os
import string
import sys
import uuid

from typing import Any, Dict, Iterable, List, Mapping, Optional

import yaml

import scenario_config
import scenario_config_exporter


def default_prefix() -> str:
    """Constructs and returns a default prefix for LoadTest names."""
    return os.environ.get('USER', 'loadtest')


def now_string() -> str:
    return datetime.datetime.now().strftime('%Y%m%d%H%M%S')


def validate_loadtest_name(name: str) -> None:
    """Validates that a LoadTest name is in the expected format."""
    if len(name) > 63:
        raise ValueError(
            'LoadTest name must be less than 63 characters long: %s' % name)
    if not all((s.isalnum() for s in name.split('-'))):
        raise ValueError('Invalid elements in LoadTest name: %s' % name)


def loadtest_base_name(scenario_name: str, uniquifiers: Iterable[str]) -> str:
    """Constructs and returns the base name for a LoadTest resource."""
    elements = scenario_name.split('_')
    elements.extend(uniquifiers)
    return '-'.join(elements)


def loadtest_name(prefix: str, scenario_name: str,
                  uniquifiers: Iterable[str]) -> str:
    """Constructs and returns a valid name for a LoadTest resource."""
    base_name = loadtest_base_name(scenario_name, uniquifiers)
    elements = []
    if prefix:
        elements.append(prefix)
    elements.append(str(uuid.uuid5(uuid.NAMESPACE_DNS, base_name)))
    name = '-'.join(elements)
    validate_loadtest_name(name)
    return name


def validate_annotations(annotations: Dict[str, str]) -> None:
    """Validates that annotations do not contain reserved names.

    These names are automatically added by the config generator.
    """
    names = set(('scenario', 'uniquifiers')).intersection(annotations)
    if names:
        raise ValueError('Annotations contain reserved names: %s' % names)


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
                         scenarios: Iterable[Mapping[str, Any]],
                         loadtest_name_prefix: str,
                         uniquifiers: Iterable[str],
                         annotations: Mapping[str, str],
                         runs_per_test: int = 1) -> Iterable[yaml.YAMLObject]:
    """Generates LoadTest configurations as YAML objects."""
    validate_annotations(annotations),
    prefix = loadtest_name_prefix or default_prefix()
    for scenario in scenarios:
        for run_index in gen_run_indices(runs_per_test):
            uniq = uniquifiers + [run_index] if run_index else uniquifiers
            name = loadtest_name(prefix, scenario['name'], uniq)
            scenario_str = json.dumps({'scenarios': scenario}, indent='  ')

            config = copy.deepcopy(base_config)
            metadata = config['metadata']
            metadata['name'] = name
            if 'labels' not in metadata:
                metadata['labels'] = dict()
            metadata['labels']['prefix'] = prefix
            if 'annotations' not in metadata:
                metadata['annotations'] = dict()
            metadata['annotations'].update(annotations)
            metadata['annotations'].update({
                'scenario': scenario['name'],
                'uniquifiers': uniq,
            })
            config['spec']['scenariosJSON'] = scenario_str

            yield config


def parse_key_value_args(args: Optional[Iterable[str]]) -> Dict[str, str]:
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
            return dumper.represent_scalar('tag:yaml.org,2002:str',
                                           data,
                                           style='|')
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
                      default=[],
                      type=str,
                      help='Template substitutions, in the form key=value.')
    argp.add_argument('-p',
                      '--prefix',
                      default='',
                      type=str,
                      help='Test name prefix.')
    argp.add_argument('-u',
                      '--uniquifiers',
                      action='extend',
                      nargs='+',
                      default=[],
                      type=str,
                      help='One or more strings to make the test name unique.')
    argp.add_argument(
        '-d',
        nargs='?',
        const=True,
        default=False,
        type=bool,
        help='Use creation date and time as an addditional uniquifier.')
    argp.add_argument('-a',
                      '--annotations',
                      action='extend',
                      nargs='+',
                      default=[],
                      type=str,
                      help='Test annotations, in the form key=value.')
    argp.add_argument('-r',
                      '--regex',
                      default='.*',
                      type=str,
                      help='Regex to select scenarios to run.')
    argp.add_argument(
        '--category',
        choices=['all', 'inproc', 'scalable', 'smoketest', 'sweep'],
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

    substitutions = parse_key_value_args(args.substitutions)

    with open(args.template) as f:
        base_config = yaml.safe_load(
            string.Template(f.read()).substitute(substitutions))

    scenario_filter = scenario_config_exporter.scenario_filter(
        scenario_name_regex=args.regex,
        category=args.category,
        client_language=args.client_language,
        server_language=args.server_language)

    scenarios = scenario_config_exporter.gen_scenarios(args.language,
                                                       scenario_filter)

    uniquifiers = args.uniquifiers
    if args.d:
        uniquifiers.append(now_string())

    annotations = parse_key_value_args(args.annotations)

    configs = gen_loadtest_configs(base_config,
                                   scenarios,
                                   loadtest_name_prefix=args.prefix,
                                   uniquifiers=uniquifiers,
                                   annotations=annotations,
                                   runs_per_test=args.runs_per_test)

    configure_yaml()

    with open(args.output, 'w') if args.output else sys.stdout as f:
        yaml.dump_all(configs, stream=f)


if __name__ == '__main__':
    main()

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
#
# See documentation below:
# https://github.com/grpc/grpc/blob/master/tools/run_tests/performance/README.md#grpc-oss-benchmarks

import argparse
import copy
import datetime
import itertools
import os
import string
import sys
import uuid

from typing import Any, Dict, Iterable, Mapping, Optional, Type

import json
import yaml

import scenario_config
import scenario_config_exporter

CONFIGURATION_FILE_HEADER_COMMENT = """
# Load test configurations generated from a template by loadtest_config.py.
# See documentation below:
# https://github.com/grpc/grpc/blob/master/tools/run_tests/performance/README.md#grpc-oss-benchmarks
"""

# TODO(paulosjca): Merge label_language and image_language into one function.
# These functions are necessary because 'c++' is not allowed as a label value in
# kubernetes, and because languages share images in the existing templates. Once
# the templates are reorganized and most image mapping is removed, the two
# functions can be merged into one.


def label_language(language: str) -> str:
    """Convert scenario language to place in a resource label."""
    return {
        'c++': 'cxx',
    }.get(language, language)


def image_language(language: str) -> str:
    """Convert scenario languages to image languages."""
    return {
        'c++': 'cxx',
        'node_purejs': 'node',
        'php7': 'php',
        'php7_protobuf_c': 'php',
        'python_asyncio': 'python',
    }.get(language, language)


def default_prefix() -> str:
    """Constructs and returns a default prefix for LoadTest names."""
    return os.environ.get('USER', 'loadtest')


def now_string() -> str:
    """Returns the current date and time in string format."""
    return datetime.datetime.now().strftime('%Y%m%d%H%M%S')


def validate_loadtest_name(name: str) -> None:
    """Validates that a LoadTest name is in the expected format."""
    if len(name) > 63:
        raise ValueError(
            'LoadTest name must be less than 63 characters long: %s' % name)
    if not all((s.isalnum() for s in name.split('-'))):
        raise ValueError('Invalid elements in LoadTest name: %s' % name)


def loadtest_base_name(scenario_name: str,
                       uniquifier_elements: Iterable[str]) -> str:
    """Constructs and returns the base name for a LoadTest resource."""
    name_elements = scenario_name.split('_')
    name_elements.extend(uniquifier_elements)
    return '-'.join(name_elements)


def loadtest_name(prefix: str, scenario_name: str,
                  uniquifier_elements: Iterable[str]) -> str:
    """Constructs and returns a valid name for a LoadTest resource."""
    base_name = loadtest_base_name(scenario_name, uniquifier_elements)
    name_elements = []
    if prefix:
        name_elements.append(prefix)
    name_elements.append(str(uuid.uuid5(uuid.NAMESPACE_DNS, base_name)))
    name = '-'.join(name_elements)
    validate_loadtest_name(name)
    return name


def validate_annotations(annotations: Dict[str, str]) -> None:
    """Validates that annotations do not contain reserved names.

    These names are automatically added by the config generator.
    """
    names = set(('scenario', 'uniquifier')).intersection(annotations)
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


def gen_loadtest_configs(
        base_config: Mapping[str, Any],
        base_config_clients: Iterable[Mapping[str, Any]],
        base_config_servers: Iterable[Mapping[str, Any]],
        scenario_name_regex: str,
        language_config: scenario_config_exporter.LanguageConfig,
        loadtest_name_prefix: str,
        uniquifier_elements: Iterable[str],
        annotations: Mapping[str, str],
        runs_per_test: int = 1) -> Iterable[Dict[str, Any]]:
    """Generates LoadTest configurations for a given language config.

    The LoadTest configurations are generated as YAML objects.
    """
    validate_annotations(annotations)
    prefix = loadtest_name_prefix or default_prefix()
    cl = image_language(language_config.client_language or
                        language_config.language)
    sl = image_language(language_config.server_language or
                        language_config.language)
    scenario_filter = scenario_config_exporter.scenario_filter(
        scenario_name_regex=scenario_name_regex,
        category=language_config.category,
        client_language=language_config.client_language,
        server_language=language_config.server_language)
    scenarios = scenario_config_exporter.gen_scenarios(language_config.language,
                                                       scenario_filter)

    for scenario in scenarios:
        for run_index in gen_run_indices(runs_per_test):
            uniq = (uniquifier_elements +
                    [run_index] if run_index else uniquifier_elements)
            name = loadtest_name(prefix, scenario['name'], uniq)
            scenario_str = json.dumps({'scenarios': scenario},
                                      indent='  ') + '\n'

            config = copy.deepcopy(base_config)

            metadata = config['metadata']
            metadata['name'] = name
            if 'labels' not in metadata:
                metadata['labels'] = dict()
            metadata['labels']['language'] = label_language(
                language_config.language)
            metadata['labels']['prefix'] = prefix
            if 'annotations' not in metadata:
                metadata['annotations'] = dict()
            metadata['annotations'].update(annotations)
            metadata['annotations'].update({
                'scenario': scenario['name'],
                'uniquifier': '-'.join(uniq),
            })

            spec = config['spec']

            # Select clients with the required language.
            spec['clients'] = [
                client for client in base_config_clients
                if client['language'] == cl
            ]
            if not spec['clients']:
                raise IndexError('Client language not found in template: %s' %
                                 cl)

            # Select servers with the required language.
            spec['servers'] = [
                server for server in base_config_servers
                if server['language'] == sl
            ]
            if not spec['servers']:
                raise IndexError('Server language not found in template: %s' %
                                 sl)

            spec['scenariosJSON'] = scenario_str

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


def config_dumper(header_comment: str) -> Type[yaml.SafeDumper]:
    """Returns a custom dumper to dump configurations in the expected format."""

    class ConfigDumper(yaml.SafeDumper):

        def expect_stream_start(self):
            super().expect_stream_start()
            if isinstance(self.event, yaml.StreamStartEvent):
                self.write_indent()
                self.write_indicator(header_comment, need_whitespace=False)

        def expect_block_sequence(self):
            super().expect_block_sequence()
            self.increase_indent()

        def expect_block_sequence_item(self, first=False):
            if isinstance(self.event, yaml.SequenceEndEvent):
                self.indent = self.indents.pop()
            super().expect_block_sequence_item(first)

    def str_presenter(dumper, data):
        if '\n' in data:
            return dumper.represent_scalar('tag:yaml.org,2002:str',
                                           data,
                                           style='|')
        return dumper.represent_scalar('tag:yaml.org,2002:str', data)

    ConfigDumper.add_representer(str, str_presenter)

    return ConfigDumper


def main() -> None:
    language_choices = sorted(scenario_config.LANGUAGES.keys())
    argp = argparse.ArgumentParser(
        description='Generates load test configs from a template.',
        fromfile_prefix_chars='@')
    argp.add_argument('-l',
                      '--language',
                      action='append',
                      choices=language_choices,
                      required=True,
                      help='Language(s) to benchmark.',
                      dest='languages')
    argp.add_argument('-t',
                      '--template',
                      type=str,
                      required=True,
                      help='LoadTest configuration yaml file template.')
    argp.add_argument('-s',
                      '--substitution',
                      action='append',
                      default=[],
                      help='Template substitution(s), in the form key=value.',
                      dest='substitutions')
    argp.add_argument('-p',
                      '--prefix',
                      default='',
                      type=str,
                      help='Test name prefix.')
    argp.add_argument('-u',
                      '--uniquifier_element',
                      action='append',
                      default=[],
                      help='String element(s) to make the test name unique.',
                      dest='uniquifier_elements')
    argp.add_argument(
        '-d',
        action='store_true',
        help='Use creation date and time as an additional uniquifier element.')
    argp.add_argument('-a',
                      '--annotation',
                      action='append',
                      default=[],
                      help='metadata.annotation(s), in the form key=value.',
                      dest='annotations')
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
        '--allow_client_language',
        action='append',
        choices=language_choices,
        default=[],
        help='Allow cross-language scenarios with this client language.',
        dest='allow_client_languages')
    argp.add_argument(
        '--allow_server_language',
        action='append',
        choices=language_choices,
        default=[],
        help='Allow cross-language scenarios with this server language.',
        dest='allow_server_languages')
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

    uniquifier_elements = args.uniquifier_elements
    if args.d:
        uniquifier_elements.append(now_string())

    annotations = parse_key_value_args(args.annotations)

    with open(args.template) as f:
        base_config = yaml.safe_load(
            string.Template(f.read()).substitute(substitutions))

    spec = base_config['spec']
    base_config_clients = spec['clients']
    del spec['clients']
    base_config_servers = spec['servers']
    del spec['servers']

    client_languages = [''] + args.allow_client_languages
    server_languages = [''] + args.allow_server_languages
    config_generators = []
    for l, cl, sl in itertools.product(args.languages, client_languages,
                                       server_languages):
        language_config = scenario_config_exporter.LanguageConfig(
            category=args.category,
            language=l,
            client_language=cl,
            server_language=sl)
        config_generators.append(
            gen_loadtest_configs(base_config,
                                 base_config_clients,
                                 base_config_servers,
                                 args.regex,
                                 language_config,
                                 loadtest_name_prefix=args.prefix,
                                 uniquifier_elements=uniquifier_elements,
                                 annotations=annotations,
                                 runs_per_test=args.runs_per_test))
    configs = (config for config in itertools.chain(*config_generators))

    with open(args.output, 'w') if args.output else sys.stdout as f:
        yaml.dump_all(configs,
                      stream=f,
                      Dumper=config_dumper(
                          CONFIGURATION_FILE_HEADER_COMMENT.strip()),
                      default_flow_style=False)


if __name__ == '__main__':
    main()

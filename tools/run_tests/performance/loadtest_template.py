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

# This script generates a load test configuration template from a collection of
# load test configurations.
#
# Configuration templates contain client and server configurations for multiple
# languages, and may contain template substitution keys. These templates are
# used to generate load test configurations by selecting clients and servers for
# the required languages. The source files for template generation may be load
# test configurations or load test configuration templates. Load test
# configuration generation is performed by loadtest_config.py. See documentation
# below:
# https://github.com/grpc/grpc/blob/master/tools/run_tests/performance/README.md

import argparse
import sys

from typing import Any, Dict, Iterable, Mapping, Type

import yaml

import loadtest_config

TEMPLATE_FILE_HEADER_COMMENT = """
# Template generated from load test configurations by loadtest_template.py.
#
# Configuration templates contain client and server configurations for multiple
# languages, and may contain template substitution keys. These templates are
# used to generate load test configurations by selecting clients and servers for
# the required languages. The source files for template generation may be load
# test configurations or load test configuration templates. Load test
# configuration generation is performed by loadtest_config.py. See documentation
# below:
# https://github.com/grpc/grpc/blob/master/tools/run_tests/performance/README.md
"""


def loadtest_template(
        input_file_names: Iterable[str],
        metadata: Mapping[str, Any],
        inject_client_pool: bool,
        inject_server_pool: bool,
        inject_big_query_table: bool,
        inject_timeout_seconds: bool,
        inject_ttl_seconds: bool) -> Dict[str, Any]:  # yapf: disable
    """Generates the load test template."""
    clients = list()
    servers = list()
    spec = dict()
    client_languages = set()
    server_languages = set()
    template = {
        'apiVersion': 'e2etest.grpc.io/v1',
        'kind': 'LoadTest',
        'metadata': metadata,
    }
    for input_file_name in input_file_names:
        with open(input_file_name) as f:
            input_config = yaml.safe_load(f.read())

            if input_config.get('apiVersion') != template['apiVersion']:
                raise ValueError('Unexpected api version in file {}: {}'.format(
                    input_file_name, input_config.get('apiVersion')))
            if input_config.get('kind') != template['kind']:
                raise ValueError('Unexpected kind in file {}: {}'.format(
                    input_file_name, input_config.get('kind')))

            for client in input_config['spec']['clients']:
                if client['language'] in client_languages:
                    continue
                if inject_client_pool:
                    client['pool'] = '${client_pool}'
                clients.append(client)
                client_languages.add(client['language'])

            for server in input_config['spec']['servers']:
                if server['language'] in server_languages:
                    continue
                if inject_server_pool:
                    server['pool'] = '${server_pool}'
                servers.append(server)
                server_languages.add(server['language'])

            input_spec = input_config['spec']
            del input_spec['clients']
            del input_spec['servers']
            del input_spec['scenariosJSON']
            spec.update(input_config['spec'])

    clients.sort(key=lambda x: x['language'])
    servers.sort(key=lambda x: x['language'])

    spec.update({
        'clients': clients,
        'servers': servers,
    })

    if inject_big_query_table:
        if 'results' not in spec:
            spec['results'] = dict()
        spec['results']['bigQueryTable'] = '${big_query_table}'
    if inject_timeout_seconds:
        spec['timeoutSeconds'] = '${timeout_seconds}'
    if inject_ttl_seconds:
        spec['ttlSeconds'] = '${ttl_seconds}'

    template['spec'] = spec

    return template


def template_dumper(header_comment: str) -> Type[yaml.SafeDumper]:
    """Returns a custom dumper to dump templates in the expected format."""

    class TemplateDumper(yaml.SafeDumper):

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

    return TemplateDumper


def main() -> None:
    argp = argparse.ArgumentParser(
        description='Creates a load test config generator template.',
        fromfile_prefix_chars='@')
    argp.add_argument('-i',
                      '--inputs',
                      action='extend',
                      nargs='+',
                      type=str,
                      help='Input files.')
    argp.add_argument('-o',
                      '--output',
                      type=str,
                      help='Output file. Outputs to stdout if not set.')
    argp.add_argument(
        '--inject_client_pool',
        action='store_true',
        help='Set spec.client(s).pool values to \'${client_pool}\'.')
    argp.add_argument(
        '--inject_server_pool',
        action='store_true',
        help='Set spec.server(s).pool values to \'${server_pool}\'.')
    argp.add_argument(
        '--inject_big_query_table',
        action='store_true',
        help='Set spec.results.bigQueryTable to \'${big_query_table}\'.')
    argp.add_argument('--inject_timeout_seconds',
                      action='store_true',
                      help='Set spec.timeoutSeconds to \'${timeout_seconds}\'.')
    argp.add_argument('--inject_ttl_seconds',
                      action='store_true',
                      help='Set timeout ')
    argp.add_argument('-n',
                      '--name',
                      default='',
                      type=str,
                      help='metadata.name.')
    argp.add_argument('-a',
                      '--annotation',
                      action='append',
                      type=str,
                      help='metadata.annotation(s), in the form key=value.',
                      dest='annotations')
    args = argp.parse_args()

    annotations = loadtest_config.parse_key_value_args(args.annotations)

    metadata = {'name': args.name}
    if annotations:
        metadata['annotations'] = annotations

    template = loadtest_template(
        input_file_names=args.inputs,
        metadata=metadata,
        inject_client_pool=args.inject_client_pool,
        inject_server_pool=args.inject_server_pool,
        inject_big_query_table=args.inject_big_query_table,
        inject_timeout_seconds=args.inject_timeout_seconds,
        inject_ttl_seconds=args.inject_ttl_seconds)

    with open(args.output, 'w') if args.output else sys.stdout as f:
        yaml.dump(template,
                  stream=f,
                  Dumper=template_dumper(TEMPLATE_FILE_HEADER_COMMENT.strip()),
                  default_flow_style=False)


if __name__ == '__main__':
    main()

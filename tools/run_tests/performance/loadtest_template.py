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
# The following example generates a basic template from the example configs
# in https://github.com/grpc/test-infra/tree/master/config/samples:
#
# $ ./tools/run_tests/performance/loadtest_template.py \
#     -i ../test-infra/config/samples/*.yaml \
#     -o ./tools/run_tests/performance/templates/basic_template.yaml \
#     --name basic_template

import argparse
import sys

from typing import Any, Dict, Iterable, Mapping

import yaml

import loadtest_config

DEFAULT_KEYS = {
    'client_pool': '${client_pool}',
    'server_pool': '${server_pool}',
    'big_query_table': '${big_query_table}',
    'timeoutSeconds': '',
    'ttlSeconds': '',
}


def validate_keys(keys: Mapping[str, str]) -> None:
    """Validates that substitution keys are a subset of default keys."""
    extra_keys = set(keys).difference(DEFAULT_KEYS)
    if extra_keys:
        raise ValueError('Unrecognized replacement keys: %s', ' '.join(keys))


def loadtest_set_keys(
    template: Mapping[str, Any],
    keys: Mapping[str, str],
) -> None:
    """Sets substitution keys in the template.

     These keys are set so they can be replaced later by the config gemerator.
     """
    if keys.get('client_pool'):
        client_pool = keys['client_pool']
        clients = template['spec']['clients']
        for client in clients:
            client['pool'] = client_pool

    if keys.get('server_pool'):
        server_pool = keys['server_pool']
        servers = template['spec']['servers']
        for server in servers:
            server['pool'] = server_pool

    if keys.get('big_query_table'):
        template['spec']['big_query_table'] = keys['big_query_table']

    for key in ('timeoutSeconds', 'ttlSeconds'):
        if keys.get(key):
            template[key] = keys[key]


def loadtest_template(input_file_names: Iterable[str], keys: Mapping[str, str],
                      metadata: Mapping[str, Any]) -> Dict[str, Any]:
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
                clients.append(client)
                client_languages.add(client['language'])

            for server in input_config['spec']['servers']:
                if server['language'] in server_languages:
                    continue
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

    template['spec'] = spec

    loadtest_set_keys(template, keys)

    return template


def main() -> None:
    argp = argparse.ArgumentParser(
        description='Creates a load test config generator template.')
    argp.add_argument('-i',
                      '--inputs',
                      action='extend',
                      nargs='*',
                      type=str,
                      required=True,
                      help='Input files.')
    argp.add_argument('-o',
                      '--output',
                      type=str,
                      help='Output file. Outputs to stdout if not set.')
    argp.add_argument('-k',
                      '--keys',
                      action='extend',
                      nargs='*',
                      default=[],
                      type=str,
                      help='Value of keys to insert, in the form key=value.')
    argp.add_argument('-n',
                      '--name',
                      default='',
                      type=str,
                      help='Name to insert.')
    argp.add_argument('-a',
                      '--annotations',
                      action='extend',
                      nargs='+',
                      default=[],
                      type=str,
                      help='Annotations to insert, in the form key=value.')

    args = argp.parse_args()

    keys = DEFAULT_KEYS.copy()
    keys.update(loadtest_config.parse_key_value_args(args.keys))
    validate_keys(keys)

    annotations = loadtest_config.parse_key_value_args(args.annotations)

    metadata = {'name': args.name}
    if annotations:
        metadata['annotations'] = annotations

    template = loadtest_template(args.inputs, keys, metadata)

    with open(args.output, 'w') if args.output else sys.stdout as f:
        yaml.dump(template, stream=f)


if __name__ == '__main__':
    main()

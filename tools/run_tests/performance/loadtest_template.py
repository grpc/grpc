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
import yaml

from typing import Any, Dict, Iterable, List


def loadtest_template(input_file_names: Iterable[str]) -> Iterable[Dict[str, Any]]:
    clients = list()
    servers = list()
    client_languages = set()
    server_languages = set()
    for input_file_name in input_file_names:
        with open(input_file_name) as f:
            input_config = yaml.safe_load(f.read())

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

    clients.sort(key=lambda x: x['language'])
    servers.sort(key=lambda x: x['language'])

    return {
        'metadata': dict(),
        'spec': {
            'clients': clients,
            'servers': servers,
        }
    }


def main() -> None:
    argp = argparse.ArgumentParser(
        description='Creates a load test config generator template rom source configurations.')
    argp.add_argument('-i',
                      '--inputs',
                      action='extend',
                      nargs='+',
                      type=str,
                      required=True,
                      help='Input files.')
    argp.add_argument(
        '-o',
        '--output',
        type=str,
        help='Output file. Outputs to stdout if not set.')
    args = argp.parse_args()

    template = loadtest_template(args.inputs)

    with open(args.output, 'w') if args.output else sys.stdout as f:
        yaml.dump(template, stream=f)


if __name__ == '__main__':
    main()

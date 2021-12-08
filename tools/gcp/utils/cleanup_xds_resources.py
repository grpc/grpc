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

import argparse
import logging
import json
import sys
import re
import os
import functools
import subprocess
import datetime
from dataclasses import dataclass
from typing import List, Any

# Parses commandline arguments
parser = argparse.ArgumentParser()
parser.add_argument('--dry_run', action='store_true')
args = parser.parse_args()

# Type alias
Json = Any

# Configures this script
KEEP_PERIOD = datetime.timedelta(days=14)
GCLOUD = os.environ.get('GCLOUD', 'gcloud')
GCLOUD_CMD_TIMEOUT_S = datetime.timedelta(seconds=5).total_seconds()
ZONE = 'us-central1-a'
SECONDARY_ZONE = 'us-west1-b'
PROJECT = 'grpc-testing'


@functools.lru_cache()
def get_expire_timestamp():
    return (datetime.datetime.now() - KEEP_PERIOD).isoformat()


def exec_gcloud(*cmds: List[str]) -> Json:
    cmds = [GCLOUD, '--project', PROJECT, '--quiet'] + list(cmds)
    if 'list' in cmds:
        # Add arguments to shape the list output
        cmds.extend([
            '--format', 'json', '--filter',
            f'creationTimestamp <= {get_expire_timestamp()}'
        ])
    if args.dry_run and 'delete' in cmds:
        # Skip deletion for dry-runs
        logging.debug('> Skipped: %s', " ".join(cmds))
        return None
    # Executing the gcloud command
    logging.debug('Executing: %s', " ".join(cmds))
    proc = subprocess.Popen(cmds,
                            stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE,
                            text=True)
    returncode = proc.wait(timeout=GCLOUD_CMD_TIMEOUT_S)
    if returncode:
        logging.error('> Failed to execute cmd [%s], returned %d, stderr: %s',
                      " ".join(cmds), returncode, proc.stderr.read())
        return None
    stdout = proc.stdout.read()
    if stdout:
        return json.loads(stdout)
    return None


def remove_relative_resources_run_xds_tests(suffix: str):
    """Removing GCP resources created by run_xds_tests.py."""
    logging.info('Removing run_xds_tests.py resources with suffix [%s]', suffix)
    exec_gcloud('compute', 'forwarding-rules', 'delete',
                f'test-forwarding-rule{suffix}', '--global')
    exec_gcloud('compute', 'target-http-proxies', 'delete',
                f'test-target-proxy{suffix}')
    exec_gcloud('alpha', 'compute', 'target-grpc-proxies', 'delete',
                f'test-target-proxy{suffix}')
    exec_gcloud('compute', 'url-maps', 'delete', f'test-map{suffix}')
    exec_gcloud('compute', 'backend-services', 'delete',
                f'test-backend-service{suffix}', '--global')
    exec_gcloud('compute', 'backend-services', 'delete',
                f'test-backend-service-alternate{suffix}', '--global')
    exec_gcloud('compute', 'backend-services', 'delete',
                f'test-backend-service-extra{suffix}', '--global')
    exec_gcloud('compute', 'backend-services', 'delete',
                f'test-backend-service-more-extra{suffix}', '--global')
    exec_gcloud('compute', 'firewall-rules', 'delete', f'test-fw-rule{suffix}')
    exec_gcloud('compute', 'health-checks', 'delete', f'test-hc{suffix}')
    exec_gcloud('compute', 'instance-groups', 'managed', 'delete',
                f'test-ig{suffix}', '--zone', ZONE)
    exec_gcloud('compute', 'instance-groups', 'managed', 'delete',
                f'test-ig-same-zone{suffix}', '--zone', ZONE)
    exec_gcloud('compute', 'instance-groups', 'managed', 'delete',
                f'test-ig-secondary-zone{suffix}', '--zone', SECONDARY_ZONE)
    exec_gcloud('compute', 'instance-templates', 'delete',
                f'test-template{suffix}')


def remove_relative_resources_psm_sec(prefix: str):
    """Removing GCP resources created by PSM Sec framework."""
    logging.info('Removing PSM Security resources with prefix [%s]', prefix)
    exec_gcloud('compute', 'forwarding-rules', 'delete',
                f'{prefix}-forwarding-rule', '--global')
    exec_gcloud('alpha', 'compute', 'target-grpc-proxies', 'delete',
                f'{prefix}-target-proxy')
    exec_gcloud('compute', 'url-maps', 'delete', f'{prefix}-url-map')
    exec_gcloud('compute', 'backend-services', 'delete',
                f'{prefix}-backend-service', '--global')
    exec_gcloud('compute', 'health-checks', 'delete', f'{prefix}-health-check')
    exec_gcloud('compute', 'firewall-rules', 'delete',
                f'{prefix}-allow-health-checks')
    exec_gcloud('alpha', 'network-security', 'server-tls-policies', 'delete',
                f'{prefix}-server-tls-policy')
    exec_gcloud('alpha', 'network-security', 'client-tls-policies', 'delete',
                f'{prefix}-client-tls-policy')


def check_one_type_of_gcp_resources(list_cmd: List[str],
                                    suffix_search: str = '',
                                    prefix_search: str = ''):
    logging.info('Checking GCP resources with %s or %s', suffix_search,
                 prefix_search)
    for resource in exec_gcloud(*list_cmd):
        if resource['name'].startswith('interop-psm-url-map'):
            logging.info('Skipping url-map test resource: %s', resource['name'])
            continue
        if suffix_search:
            result = re.search(suffix_search, resource['name'])
            if result is not None:
                remove_relative_resources_run_xds_tests(result.group(1))
                continue

        if prefix_search:
            result = re.search(prefix_search, resource['name'])
            if result is not None:
                remove_relative_resources_psm_sec(result.group(1))
                continue


def check_costly_gcp_resources() -> None:
    check_one_type_of_gcp_resources(['compute', 'forwarding-rules', 'list'],
                                    suffix_search=r'test-forwarding-rule(.*)',
                                    prefix_search=r'(.+?)-forwarding-rule')
    check_one_type_of_gcp_resources(['compute', 'target-http-proxies', 'list'],
                                    suffix_search=r'test-target-proxy(.*)')
    check_one_type_of_gcp_resources(['compute', 'target-grpc-proxies', 'list'],
                                    suffix_search=r'test-target-proxy(.*)',
                                    prefix_search=r'(.+?)-target-proxy')
    check_one_type_of_gcp_resources(['compute', 'url-maps', 'list'],
                                    suffix_search=r'test-map(.*)',
                                    prefix_search=r'(.+?)-url-map')
    check_one_type_of_gcp_resources(['compute', 'backend-services', 'list'],
                                    suffix_search=r'test-backend-service(.*)',
                                    prefix_search=r'(.+?)-backend-service')


def main():
    logging.info('Cleaning up costly resources created before %s',
                 get_expire_timestamp())
    check_costly_gcp_resources()


if __name__ == "__main__":
    logging.basicConfig(level=logging.DEBUG)
    main()

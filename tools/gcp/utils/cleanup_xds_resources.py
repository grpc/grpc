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
from dataclasses import dataclass
import datetime
import functools
import json
import logging
import os
import re
import subprocess
import sys
from typing import Any, List

# Parses commandline arguments
parser = argparse.ArgumentParser()
parser.add_argument('--dry_run',
                    action='store_true',
                    help='print the deletion command without execution')
parser.add_argument('--clean_psm_sec',
                    action='store_true',
                    help='whether to enable PSM Security resource cleaning')
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
PSM_SECURITY_PREFIX = 'xds-k8s-security'

# Global variables
KEEP_CONFIG = None


def load_keep_config() -> None:
    global KEEP_CONFIG
    json_path = os.path.realpath(
        os.path.join(os.path.dirname(os.path.abspath(__file__)),
                     'keep_xds_interop_resources.json'))
    with open(json_path, 'r') as f:
        KEEP_CONFIG = json.load(f)
        logging.debug('Resource keep config loaded: %s',
                      json.dumps(KEEP_CONFIG, indent=2))


def is_marked_as_keep_gce(suffix: str) -> bool:
    return suffix in KEEP_CONFIG["gce_framework"]["suffix"]


@functools.lru_cache()
def get_expire_timestamp() -> str:
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
        logging.debug('> Skipped[Dry Run]: %s', " ".join(cmds))
        return None
    # Executing the gcloud command
    logging.debug('Executing: %s', " ".join(cmds))
    proc = subprocess.Popen(cmds,
                            stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE,
                            text=True)
    # NOTE(lidiz) the gcloud subprocess won't return unless its output is read
    stdout = proc.stdout.read()
    stderr = proc.stderr.read()
    try:
        returncode = proc.wait(timeout=GCLOUD_CMD_TIMEOUT_S)
    except subprocess.TimeoutExpired:
        logging.error('> Timeout executing cmd [%s]', " ".join(cmds))
        return None
    if returncode:
        logging.error('> Failed to execute cmd [%s], returned %d, stderr: %s',
                      " ".join(cmds), returncode, stderr)
        return None
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


def remove_relative_resources_psm_sec(suffix: str):
    """Removing GCP resources created by PSM Sec framework."""
    logging.info('Removing PSM Security resources with suffix [%s]', suffix)
    exec_gcloud('compute', 'forwarding-rules', 'delete',
                f'{PSM_SECURITY_PREFIX}-forwarding-rule{suffix}', '--global')
    exec_gcloud('alpha', 'compute', 'target-grpc-proxies', 'delete',
                f'{PSM_SECURITY_PREFIX}-target-proxy{suffix}')
    exec_gcloud('compute', 'url-maps', 'delete',
                f'{PSM_SECURITY_PREFIX}-url-map{suffix}')
    exec_gcloud('compute', 'backend-services', 'delete',
                f'{PSM_SECURITY_PREFIX}-backend-service{suffix}', '--global')
    exec_gcloud('compute', 'health-checks', 'delete',
                f'{PSM_SECURITY_PREFIX}-health-check{suffix}')
    exec_gcloud('compute', 'firewall-rules', 'delete',
                f'{PSM_SECURITY_PREFIX}-allow-health-checks{suffix}')
    exec_gcloud('alpha', 'network-security', 'server-tls-policies', 'delete',
                f'{PSM_SECURITY_PREFIX}-server-tls-policy{suffix}',
                '--location=global')
    exec_gcloud('alpha', 'network-security', 'client-tls-policies', 'delete',
                f'{PSM_SECURITY_PREFIX}-client-tls-policy{suffix}',
                '--location=global')


def check_one_type_of_gcp_resources(list_cmd: List[str],
                                    gce_resource_matcher: str = '',
                                    gke_resource_matcher: str = ''):
    logging.info('Checking GCP resources with %s or %s', gce_resource_matcher,
                 gke_resource_matcher)
    for resource in exec_gcloud(*list_cmd):
        if gce_resource_matcher:
            result = re.search(gce_resource_matcher, resource['name'])
            if result is not None:
                if is_marked_as_keep_gce(result.group(1)):
                    logging.info(
                        'Skip: GCE resource suffix [%s] is marked as keep',
                        result.group(1))
                    continue
                remove_relative_resources_run_xds_tests(result.group(1))
                continue

        if gke_resource_matcher and args.clean_psm_sec:
            result = re.search(gke_resource_matcher, resource['name'])
            if result is not None:
                remove_relative_resources_psm_sec(result.group(1))
                continue


def check_costly_gcp_resources() -> None:
    check_one_type_of_gcp_resources(
        ['compute', 'health-checks', 'list'],
        gce_resource_matcher=r'test-hc(.*)',
        gke_resource_matcher=f'{PSM_SECURITY_PREFIX}-health-check(.*)')
    check_one_type_of_gcp_resources(['compute', 'instance-templates', 'list'],
                                    gce_resource_matcher=r'test-template(.*)')


def main():
    load_keep_config()
    logging.info('Cleaning up xDS interop resources created before %s',
                 get_expire_timestamp())
    check_costly_gcp_resources()


if __name__ == "__main__":
    logging.basicConfig(level=logging.DEBUG)
    main()

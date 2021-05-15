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
import datetime
import functools
import json
import logging
import os
import re
import subprocess
import sys
from dataclasses import dataclass
from typing import Any, List, Optional

# Parses commandline arguments
parser = argparse.ArgumentParser()
parser.add_argument('--dry_run', action='store_true')
args = parser.parse_args()

# Type alias
Json = Any

# Configures this script
KEEP_PERIOD = datetime.timedelta(hours=24)
COMMAND_TIMEOUT_S = datetime.timedelta(seconds=180).total_seconds()
GCLOUD = os.environ.get('GCLOUD', 'gcloud')
ZONE = 'us-central1-a'
SECONDARY_ZONE = 'us-west1-b'
PROJECT = 'grpc-testing'

ROOT = os.path.abspath(os.path.join(os.path.dirname(sys.argv[0]), '../../..'))
RUN_XDS_TESTS = os.path.join(ROOT, 'tools', 'run_tests', 'run_xds_tests.py')


@functools.lru_cache()
def get_expire_timestamp():
    return (datetime.datetime.now() - KEEP_PERIOD).isoformat()


def exec_command(*cmds: List[str], cwd: Optional[str] = None) -> Optional[str]:
    logging.debug('Executing: %s', " ".join(cmds))
    proc = subprocess.Popen(cmds,
                            stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE,
                            text=True,
                            cwd=cwd)
    # Overcome the potential buffer deadlocking
    # https://docs.python.org/3/library/subprocess.html#subprocess.Popen.communicate
    try:
        outs, errs = proc.communicate(timeout=COMMAND_TIMEOUT_S)
    except subprocess.TimeoutExpired:
        proc.kill()
        outs, errs = proc.communicate()
    if proc.returncode:
        logging.error('> Failed to execute cmd [%s], returned %d, stderr: %s',
                      " ".join(cmds), proc.returncode, errs)
        return None
    return outs


def exec_gcloud(*cmds: List[str]) -> Json:
    cmds = [GCLOUD, '--project', PROJECT] + list(cmds)
    if 'list' in cmds:
        # Add arguments to shape the list output
        cmds.extend([
            '--format', 'json', '--filter',
            f'creationTimestamp<={get_expire_timestamp()}'
        ])
    # Executing the gcloud command
    return json.loads(exec_command(*cmds))


def remove_relative_resources_run_xds_tests(suffix: str):
    """Removing GCP resources created by run_xds_tests.py."""
    logging.info('Removing run_xds_tests.py resources with suffix [%s]', suffix)
    cmds = [
        sys.executable, RUN_XDS_TESTS, '--clean_only', '--gcp_suffix', suffix
    ]
    if args.dry_run:
        logging.debug('> Skipped: %s', " ".join(cmds))
        return
    exec_command(cmds, cwd=ROOT)


def remove_relative_resources_psm_sec(prefix: str):
    """Removing GCP resources created by PSM Sec framework."""
    logging.info('Removing PSM Security resources with prefix [%s]', prefix)
    cmds = [
        sys.executable,
        '-m',
        'bin.run_td_setup',
        '--cmd',
        'cleanup',
        '--flagfile',
        'config/grpc-testing.cfg',
        '--namespace',
        prefix,
        # Following arguments doesn't matter
        '--kube_context',
        'DUMMY_DOES_MATTER',
        '--server_image',
        'DUMMY_DOES_MATTER',
        '--client_image',
        'DUMMY_DOES_MATTER'
    ]
    if args.dry_run:
        logging.debug('> Skipped: %s', " ".join(cmds))
        return
    exec_command(cmds,
                 cwd=os.path.join(ROOT, 'tools', 'run_tests',
                                  'xds_k8s_test_driver'))


def check_one_type_of_gcp_resources(list_cmd: List[str],
                                    old_framework_residual_search: str = '',
                                    new_framework_residual_search: str = ''):
    logging.info('Checking GCP resources with %s or %s',
                 old_framework_residual_search, new_framework_residual_search)
    for resource in exec_gcloud(*list_cmd):
        if old_framework_residual_search:
            result = re.search(old_framework_residual_search, resource['name'])
            if result is not None:
                remove_relative_resources_run_xds_tests(result.group(1))
                continue

        if new_framework_residual_search:
            result = re.search(new_framework_residual_search, resource['name'])
            if result is not None:
                remove_relative_resources_psm_sec(result.group(1))
                continue


def check_costly_gcp_resources() -> None:
    check_one_type_of_gcp_resources(
        ['compute', 'forwarding-rules', 'list'],
        old_framework_residual_search=r'^test-forwarding-rule(.*)$',
        new_framework_residual_search=r'^(.+?)-forwarding-rule$')
    check_one_type_of_gcp_resources(
        ['compute', 'target-http-proxies', 'list'],
        old_framework_residual_search=r'^test-target-proxy(.*)$')
    check_one_type_of_gcp_resources(
        ['compute', 'target-grpc-proxies', 'list'],
        old_framework_residual_search=r'^test-target-proxy(.*)$',
        new_framework_residual_search=r'^(.+?)-target-proxy$')
    check_one_type_of_gcp_resources(
        ['compute', 'url-maps', 'list'],
        old_framework_residual_search=r'^test-map(.*)$',
        new_framework_residual_search=r'^(.+?)-url-map$')
    check_one_type_of_gcp_resources(
        ['compute', 'backend-services', 'list'],
        old_framework_residual_search=r'^test-backend-service(.*)$',
        new_framework_residual_search=r'^(.+?)-backend-service$')


def main():
    logging.info('Cleaning up costly resources created before %s',
                 get_expire_timestamp())
    check_costly_gcp_resources()


if __name__ == "__main__":
    logging.basicConfig(level=logging.DEBUG)
    main()

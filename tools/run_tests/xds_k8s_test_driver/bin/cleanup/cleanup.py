# Copyright 2021 gRPC authors.
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
"""Clean up resources created by the tests.

This is intended as a tool to delete leaked resources from old tests.

Typical usage examples:

python3 tools/run_tests/xds_k8s_test_driver/bin/cleanup/cleanup.py\
    --project=grpc-testing\
    --network=default-vpc\
    --kube_context=gke_grpc-testing_us-central1-a_interop-test-psm-sec-v2-us-central1-a\
    --resource_prefix='required-but-does-not-matter'\
    --td_bootstrap_image='required-but-does-not-matter' --server_image='required-but-does-not-matter' --client_image='required-but-does-not-matter'
"""
import datetime
import functools
import logging
import json
import os
import pytz
import re
import subprocess
from typing import Any, List

from absl import app
from absl import flags

from framework import xds_flags
from framework import xds_k8s_flags
from framework.infrastructure import gcp
from framework.infrastructure import k8s
from framework.infrastructure import traffic_director
from framework.test_app import client_app
from framework.test_app import server_app

logger = logging.getLogger(__name__)
Json = Any
KubernetesClientRunner = client_app.KubernetesClientRunner
KubernetesServerRunner = server_app.KubernetesServerRunner
utc = pytz.UTC

KEEP_PERIOD = datetime.timedelta(days=85)
GCLOUD = os.environ.get('GCLOUD', 'gcloud')
GCLOUD_CMD_TIMEOUT_S = datetime.timedelta(seconds=5).total_seconds()
ZONE = 'us-central1-a'
SECONDARY_ZONE = 'us-west1-b'

PSM_SECURITY_PREFIX = 'xds-k8s-security'  # Prefix for gke resources to delete.
URL_MAP_TEST_PREFIX = 'interop-psm-url-map'  # Prefix for url-map test resources to delete.

DRY_RUN = flags.DEFINE_bool(
    "dry_run",
    default=False,
    help="dry run, print resources but do not perform deletion")


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


def is_marked_as_keep_gke(suffix: str) -> bool:
    return suffix in KEEP_CONFIG["gke_framework"]["suffix"]


@functools.lru_cache()
def get_expire_timestamp() -> str:
    return datetime.datetime.now() - KEEP_PERIOD


def exec_gcloud(project: str, *cmds: List[str]) -> Json:
    cmds = [GCLOUD, '--project', project, '--quiet'] + list(cmds)
    if 'list' in cmds:
        # Add arguments to shape the list output
        cmds.extend([
            '--format', 'json', '--filter',
            f'creationTimestamp <= {get_expire_timestamp().isoformat()}'
        ])
    # Executing the gcloud command
    logging.debug('Executing: %s', " ".join(cmds))
    proc = subprocess.Popen(cmds,
                            stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE)
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


def remove_relative_resources_run_xds_tests(project: str, suffix: str):
    """Removing GCP resources created by run_xds_tests.py."""
    logging.info('----- Removing run_xds_tests.py resources with suffix [%s]',
                 suffix)
    exec_gcloud(project, 'compute', 'forwarding-rules', 'delete',
                f'test-forwarding-rule{suffix}', '--global')
    exec_gcloud(project, 'compute', 'target-http-proxies', 'delete',
                f'test-target-proxy{suffix}')
    exec_gcloud(project, 'alpha', 'compute', 'target-grpc-proxies', 'delete',
                f'test-target-proxy{suffix}')
    exec_gcloud(project, 'compute', 'url-maps', 'delete', f'test-map{suffix}')
    exec_gcloud(project, 'compute', 'backend-services', 'delete',
                f'test-backend-service{suffix}', '--global')
    exec_gcloud(project, 'compute', 'backend-services', 'delete',
                f'test-backend-service-alternate{suffix}', '--global')
    exec_gcloud(project, 'compute', 'backend-services', 'delete',
                f'test-backend-service-extra{suffix}', '--global')
    exec_gcloud(project, 'compute', 'backend-services', 'delete',
                f'test-backend-service-more-extra{suffix}', '--global')
    exec_gcloud(project, 'compute', 'firewall-rules', 'delete',
                f'test-fw-rule{suffix}')
    exec_gcloud(project, 'compute', 'health-checks', 'delete',
                f'test-hc{suffix}')
    exec_gcloud(project, 'compute', 'instance-groups', 'managed', 'delete',
                f'test-ig{suffix}', '--zone', ZONE)
    exec_gcloud(project, 'compute', 'instance-groups', 'managed', 'delete',
                f'test-ig-same-zone{suffix}', '--zone', ZONE)
    exec_gcloud(project, 'compute', 'instance-groups', 'managed', 'delete',
                f'test-ig-secondary-zone{suffix}', '--zone', SECONDARY_ZONE)
    exec_gcloud(project, 'compute', 'instance-templates', 'delete',
                f'test-template{suffix}')


# cleanup_td creates TrafficDirectorManager (and its varients for security and
# AppNet), and then calls the cleanup() methods.
#
# Note that the varients are all based on the basic TrafficDirectorManager, so
# their `cleanup()` might do duplicate work. But deleting an non-exist resource
# returns 404, and is OK.
def cleanup_td_for_gke(project, network, resource_prefix, resource_suffix):
    gcp_api_manager = gcp.api.GcpApiManager()
    plain_td = traffic_director.TrafficDirectorManager(
        gcp_api_manager,
        project=project,
        network=network,
        resource_prefix=resource_prefix,
        resource_suffix=resource_suffix)
    security_td = traffic_director.TrafficDirectorSecureManager(
        gcp_api_manager,
        project=project,
        network=network,
        resource_prefix=resource_prefix,
        resource_suffix=resource_suffix)
    # TODO: cleanup appnet resources.
    # appnet_td = traffic_director.TrafficDirectorAppNetManager(
    #     gcp_api_manager,
    #     project=project,
    #     network=network,
    #     resource_prefix=resource_prefix,
    #     resource_suffix=resource_suffix)

    logger.info('----- Removing traffic director for gke, prefix %s, suffix %s',
                resource_prefix, resource_suffix)
    security_td.cleanup(force=True)
    # appnet_td.cleanup(force=True)
    plain_td.cleanup(force=True)


# cleanup_client creates a client runner, and calls its cleanup() method.
def cleanup_client(project, network, k8s_api_manager, resource_prefix,
                   resource_suffix, gcp_service_account):
    runner_kwargs = dict(
        deployment_name=xds_flags.CLIENT_NAME.value,
        image_name=xds_k8s_flags.CLIENT_IMAGE.value,
        td_bootstrap_image=xds_k8s_flags.TD_BOOTSTRAP_IMAGE.value,
        gcp_project=project,
        gcp_api_manager=gcp.api.GcpApiManager(),
        gcp_service_account=gcp_service_account,
        xds_server_uri=xds_flags.XDS_SERVER_URI.value,
        network=network,
        stats_port=xds_flags.CLIENT_PORT.value)

    client_namespace = KubernetesClientRunner.make_namespace_name(
        resource_prefix, resource_suffix)
    client_runner = KubernetesClientRunner(
        k8s.KubernetesNamespace(k8s_api_manager, client_namespace),
        **runner_kwargs)

    logger.info('Cleanup client')
    client_runner.cleanup(force=True, force_namespace=True)


# cleanup_server creates a server runner, and calls its cleanup() method.
def cleanup_server(project, network, k8s_api_manager, resource_prefix,
                   resource_suffix, gcp_service_account):
    runner_kwargs = dict(
        deployment_name=xds_flags.SERVER_NAME.value,
        image_name=xds_k8s_flags.SERVER_IMAGE.value,
        td_bootstrap_image=xds_k8s_flags.TD_BOOTSTRAP_IMAGE.value,
        gcp_project=project,
        gcp_api_manager=gcp.api.GcpApiManager(),
        gcp_service_account=gcp_service_account,
        network=network)

    server_namespace = KubernetesServerRunner.make_namespace_name(
        resource_prefix, resource_suffix)
    server_runner = KubernetesServerRunner(
        k8s.KubernetesNamespace(k8s_api_manager, server_namespace),
        **runner_kwargs)

    logger.info('Cleanup server')
    server_runner.cleanup(force=True, force_namespace=True)


def main(argv):
    if len(argv) > 1:
        raise app.UsageError('Too many command-line arguments.')
    load_keep_config()

    project: str = xds_flags.PROJECT.value
    network: str = xds_flags.NETWORK.value
    gcp_service_account: str = xds_k8s_flags.GCP_SERVICE_ACCOUNT.value
    dry_run: bool = DRY_RUN.value

    # List resources older than KEEP_PERIOD. We only list health-checks and
    # instance templates because these are leaves in the resource dependency tree.
    #
    # E.g. forwarding-rule depends on the target-proxy. So leaked
    # forwarding-rule indicates there's a leaked target-proxy (because this
    # target proxy cannot deleted unless the forwarding rule is deleted). The
    # leaked target-proxy is guaranteed to be a super set of leaked
    # forwarding-rule.
    leakedHealthChecks = exec_gcloud(project, 'compute', 'health-checks',
                                     'list')
    for resource in leakedHealthChecks:
        logger.info('-----')
        logger.info('----- Cleaning up health check %s', resource['name'])
        if dry_run:
            # Skip deletion for dry-runs
            logging.info('----- Skipped [Dry Run]: %s', resource['name'])
            continue

        # Cleanup resources from the gce framewok.
        result = re.search(r'test-hc(.*)', resource['name'])
        if result is not None:
            if is_marked_as_keep_gce(result.group(1)):
                logging.info(
                    'Skipped [keep]: GCE resource suffix [%s] is marked as keep',
                    result.group(1))
                continue
            remove_relative_resources_run_xds_tests(project, result.group(1))
            continue

        # Cleanup resources from the gke framework.
        result = re.search(f'{PSM_SECURITY_PREFIX}-health-check-(.*)',
                           resource['name'])
        if result is not None:
            if is_marked_as_keep_gke(result.group(1)):
                logging.info(
                    'Skipped [keep]: GKE resource suffix [%s] is marked as keep',
                    result.group(1))
                continue
            cleanup_td_for_gke(project, network, PSM_SECURITY_PREFIX,
                               result.group(1))
            continue

        logging.info(
            '----- Skipped [does not matching resource name templates]')

    # Delete leaked instance templates, those usually mean there are leaked VMs
    # from the gce framework. Also note that this is only needed for the gce
    # resources.
    leakedInstanceTemplates = exec_gcloud(project, 'compute',
                                          'instance-templates', 'list')
    for resource in leakedInstanceTemplates:
        logger.info('-----')
        logger.info('----- Cleaning up instance template %s', resource['name'])
        if dry_run:
            # Skip deletion for dry-runs
            logging.info('----- Skipped [Dry Run]: %s', resource['name'])
            continue

        # Cleanup resources from the gce framewok.
        result = re.search(r'test-template(.*)', resource['name'])
        if result is not None:
            if is_marked_as_keep_gce(result.group(1)):
                logging.info(
                    'Skipped [keep]: GCE resource suffix [%s] is marked as keep',
                    result.group(1))
                continue
            remove_relative_resources_run_xds_tests(project, result.group(1))
            continue

        logging.info(
            '----- Skipped [does not matching resource name templates]')

    # Delete leaked k8s namespaces, those usually mean there are leaked testing
    # client/servers from the gke framework.
    k8s_api_manager = k8s.KubernetesApiManager(xds_k8s_flags.KUBE_CONTEXT.value)
    nss = k8s_api_manager.core.list_namespace()
    for ns in nss.items:
        logger.info('-----')
        logger.info('----- Cleaning up k8s namespaces %s', ns.metadata.name)
        if ns.metadata.creation_timestamp <= utc.localize(
                get_expire_timestamp()):
            if dry_run:
                # Skip deletion for dry-runs
                logging.info('----- Skipped [Dry Run]: %s', ns.metadata.name)
                continue

            result = re.search(f'{PSM_SECURITY_PREFIX}-server-(.*)',
                               ns.metadata.name)
            if result is not None:
                print(ns.metadata.name, ns.metadata.creation_timestamp,
                      result.group(1))
                cleanup_server(project, network,
                               k8s_api_manager, PSM_SECURITY_PREFIX,
                               result.group(1), gcp_service_account)
                continue

            result = re.search(f'{PSM_SECURITY_PREFIX}-client-(.*)',
                               ns.metadata.name)
            if result is not None:
                print(ns.metadata.name, ns.metadata.creation_timestamp,
                      result.group(1))
                cleanup_client(project, network,
                               k8s_api_manager, PSM_SECURITY_PREFIX,
                               result.group(1), gcp_service_account)
                continue

            # Special handling for url-map test clients. url-map test servers
            # are shared, so there's no need to delete them.
            result = re.search(f'{URL_MAP_TEST_PREFIX}-client-(.*)',
                               ns.metadata.name)
            if result is not None:
                print(ns.metadata.name, ns.metadata.creation_timestamp,
                      result.group(1))
                cleanup_client(project, network,
                               k8s_api_manager, URL_MAP_TEST_PREFIX,
                               result.group(1), gcp_service_account)
                continue
        logging.info(
            '----- Skipped [does not matching resource name templates]')


if __name__ == '__main__':
    app.run(main)

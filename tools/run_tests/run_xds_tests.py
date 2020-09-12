#!/usr/bin/env python
# Copyright 2020 gRPC authors.
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
"""Run xDS integration tests on GCP using Traffic Director."""

import argparse
import googleapiclient.discovery
import grpc
import json
import logging
import os
import random
import shlex
import socket
import subprocess
import sys
import tempfile
import time

from oauth2client.client import GoogleCredentials

import python_utils.jobset as jobset
import python_utils.report_utils as report_utils

from src.proto.grpc.health.v1 import health_pb2
from src.proto.grpc.health.v1 import health_pb2_grpc
from src.proto.grpc.testing import empty_pb2
from src.proto.grpc.testing import messages_pb2
from src.proto.grpc.testing import test_pb2_grpc

logger = logging.getLogger()
console_handler = logging.StreamHandler()
formatter = logging.Formatter(fmt='%(asctime)s: %(levelname)-8s %(message)s')
console_handler.setFormatter(formatter)
logger.handlers = []
logger.addHandler(console_handler)
logger.setLevel(logging.WARNING)

_TEST_CASES = [
    'backends_restart',
    'change_backend_service',
    'gentle_failover',
    'ping_pong',
    'remove_instance_group',
    'round_robin',
    'secondary_locality_gets_no_requests_on_partial_primary_failure',
    'secondary_locality_gets_requests_on_primary_failure',
    'traffic_splitting',
]
# Valid test cases, but not in all. So the tests can only run manually, and
# aren't enabled automatically for all languages.
#
# TODO: Move them into _TEST_CASES when support is ready in all languages.
_ADDITIONAL_TEST_CASES = ['path_matching', 'header_matching']


def parse_test_cases(arg):
    if arg == '':
        return []
    arg_split = arg.split(',')
    test_cases = set()
    all_test_cases = _TEST_CASES + _ADDITIONAL_TEST_CASES
    for arg in arg_split:
        if arg == "all":
            test_cases = test_cases.union(_TEST_CASES)
        else:
            test_cases = test_cases.union([arg])
    if not all([test_case in all_test_cases for test_case in test_cases]):
        raise Exception('Failed to parse test cases %s' % arg)
    # Perserve order.
    return [x for x in all_test_cases if x in test_cases]


def parse_port_range(port_arg):
    try:
        port = int(port_arg)
        return range(port, port + 1)
    except:
        port_min, port_max = port_arg.split(':')
        return range(int(port_min), int(port_max) + 1)


argp = argparse.ArgumentParser(description='Run xDS interop tests on GCP')
argp.add_argument('--project_id', help='GCP project id')
argp.add_argument(
    '--gcp_suffix',
    default='',
    help='Optional suffix for all generated GCP resource names. Useful to '
    'ensure distinct names across test runs.')
argp.add_argument(
    '--test_case',
    default='ping_pong',
    type=parse_test_cases,
    help='Comma-separated list of test cases to run. Available tests: %s, '
    '(or \'all\' to run every test). '
    'Alternative tests not included in \'all\': %s' %
    (','.join(_TEST_CASES), ','.join(_ADDITIONAL_TEST_CASES)))
argp.add_argument(
    '--bootstrap_file',
    default='',
    help='File to reference via GRPC_XDS_BOOTSTRAP. Disables built-in '
    'bootstrap generation')
argp.add_argument(
    '--xds_v3_support',
    default=False,
    action='store_true',
    help='Support xDS v3 via GRPC_XDS_EXPERIMENTAL_V3_SUPPORT. '
    'If a pre-created bootstrap file is provided via the --bootstrap_file '
    'parameter, it should include xds_v3 in its server_features field.')
argp.add_argument(
    '--client_cmd',
    default=None,
    help='Command to launch xDS test client. {server_uri}, {stats_port} and '
    '{qps} references will be replaced using str.format(). GRPC_XDS_BOOTSTRAP '
    'will be set for the command')
argp.add_argument('--zone', default='us-central1-a')
argp.add_argument('--secondary_zone',
                  default='us-west1-b',
                  help='Zone to use for secondary TD locality tests')
argp.add_argument('--qps', default=100, type=int, help='Client QPS')
argp.add_argument(
    '--wait_for_backend_sec',
    default=1200,
    type=int,
    help='Time limit for waiting for created backend services to report '
    'healthy when launching or updated GCP resources')
argp.add_argument(
    '--use_existing_gcp_resources',
    default=False,
    action='store_true',
    help=
    'If set, find and use already created GCP resources instead of creating new'
    ' ones.')
argp.add_argument(
    '--keep_gcp_resources',
    default=False,
    action='store_true',
    help=
    'Leave GCP VMs and configuration running after test. Default behavior is '
    'to delete when tests complete.')
argp.add_argument(
    '--compute_discovery_document',
    default=None,
    type=str,
    help=
    'If provided, uses this file instead of retrieving via the GCP discovery '
    'API')
argp.add_argument(
    '--alpha_compute_discovery_document',
    default=None,
    type=str,
    help='If provided, uses this file instead of retrieving via the alpha GCP '
    'discovery API')
argp.add_argument('--network',
                  default='global/networks/default',
                  help='GCP network to use')
argp.add_argument('--service_port_range',
                  default='8080:8110',
                  type=parse_port_range,
                  help='Listening port for created gRPC backends. Specified as '
                  'either a single int or as a range in the format min:max, in '
                  'which case an available port p will be chosen s.t. min <= p '
                  '<= max')
argp.add_argument(
    '--stats_port',
    default=8079,
    type=int,
    help='Local port for the client process to expose the LB stats service')
argp.add_argument('--xds_server',
                  default='trafficdirector.googleapis.com:443',
                  help='xDS server')
argp.add_argument('--source_image',
                  default='projects/debian-cloud/global/images/family/debian-9',
                  help='Source image for VMs created during the test')
argp.add_argument('--path_to_server_binary',
                  default=None,
                  type=str,
                  help='If set, the server binary must already be pre-built on '
                  'the specified source image')
argp.add_argument('--machine_type',
                  default='e2-standard-2',
                  help='Machine type for VMs created during the test')
argp.add_argument(
    '--instance_group_size',
    default=2,
    type=int,
    help='Number of VMs to create per instance group. Certain test cases (e.g., '
    'round_robin) may not give meaningful results if this is set to a value '
    'less than 2.')
argp.add_argument('--verbose',
                  help='verbose log output',
                  default=False,
                  action='store_true')
# TODO(ericgribkoff) Remove this param once the sponge-formatted log files are
# visible in all test environments.
argp.add_argument('--log_client_output',
                  help='Log captured client output',
                  default=False,
                  action='store_true')
# TODO(ericgribkoff) Remove this flag once all test environments are verified to
# have access to the alpha compute APIs.
argp.add_argument('--only_stable_gcp_apis',
                  help='Do not use alpha compute APIs. Some tests may be '
                  'incompatible with this option (gRPC health checks are '
                  'currently alpha and required for simulating server failure',
                  default=False,
                  action='store_true')
args = argp.parse_args()

if args.verbose:
    logger.setLevel(logging.DEBUG)

_DEFAULT_SERVICE_PORT = 80
_WAIT_FOR_BACKEND_SEC = args.wait_for_backend_sec
_WAIT_FOR_OPERATION_SEC = 1200
_INSTANCE_GROUP_SIZE = args.instance_group_size
_NUM_TEST_RPCS = 10 * args.qps
_WAIT_FOR_STATS_SEC = 360
_WAIT_FOR_VALID_CONFIG_SEC = 60
_WAIT_FOR_URL_MAP_PATCH_SEC = 300
_CONNECTION_TIMEOUT_SEC = 60
_GCP_API_RETRIES = 5
_BOOTSTRAP_TEMPLATE = """
{{
  "node": {{
    "id": "{node_id}",
    "metadata": {{
      "TRAFFICDIRECTOR_NETWORK_NAME": "%s"
    }},
    "locality": {{
      "zone": "%s"
    }}
  }},
  "xds_servers": [{{
    "server_uri": "%s",
    "channel_creds": [
      {{
        "type": "google_default",
        "config": {{}}
      }}
    ],
    "server_features": {server_features}
  }}]
}}""" % (args.network.split('/')[-1], args.zone, args.xds_server)

# TODO(ericgribkoff) Add change_backend_service to this list once TD no longer
# sends an update with no localities when adding the MIG to the backend service
# can race with the URL map patch.
_TESTS_TO_FAIL_ON_RPC_FAILURE = ['ping_pong', 'round_robin']
# Tests that run UnaryCall and EmptyCall.
_TESTS_TO_RUN_MULTIPLE_RPCS = ['path_matching', 'header_matching']
# Tests that make UnaryCall with test metadata.
_TESTS_TO_SEND_METADATA = ['header_matching']
_TEST_METADATA_KEY = 'xds_md'
_TEST_METADATA_VALUE = 'exact_match'
_PATH_MATCHER_NAME = 'path-matcher'
_BASE_TEMPLATE_NAME = 'test-template'
_BASE_INSTANCE_GROUP_NAME = 'test-ig'
_BASE_HEALTH_CHECK_NAME = 'test-hc'
_BASE_FIREWALL_RULE_NAME = 'test-fw-rule'
_BASE_BACKEND_SERVICE_NAME = 'test-backend-service'
_BASE_URL_MAP_NAME = 'test-map'
_BASE_SERVICE_HOST = 'grpc-test'
_BASE_TARGET_PROXY_NAME = 'test-target-proxy'
_BASE_FORWARDING_RULE_NAME = 'test-forwarding-rule'
_TEST_LOG_BASE_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                  '../../reports')
_SPONGE_LOG_NAME = 'sponge_log.log'
_SPONGE_XML_NAME = 'sponge_log.xml'


def get_client_stats(num_rpcs, timeout_sec):
    with grpc.insecure_channel('localhost:%d' % args.stats_port) as channel:
        stub = test_pb2_grpc.LoadBalancerStatsServiceStub(channel)
        request = messages_pb2.LoadBalancerStatsRequest()
        request.num_rpcs = num_rpcs
        request.timeout_sec = timeout_sec
        rpc_timeout = timeout_sec + _CONNECTION_TIMEOUT_SEC
        response = stub.GetClientStats(request,
                                       wait_for_ready=True,
                                       timeout=rpc_timeout)
        logger.debug('Invoked GetClientStats RPC: %s', response)
        return response


class RpcDistributionError(Exception):
    pass


def _verify_rpcs_to_given_backends(backends, timeout_sec, num_rpcs,
                                   allow_failures):
    start_time = time.time()
    error_msg = None
    logger.debug('Waiting for %d sec until backends %s receive load' %
                 (timeout_sec, backends))
    while time.time() - start_time <= timeout_sec:
        error_msg = None
        stats = get_client_stats(num_rpcs, timeout_sec)
        rpcs_by_peer = stats.rpcs_by_peer
        for backend in backends:
            if backend not in rpcs_by_peer:
                error_msg = 'Backend %s did not receive load' % backend
                break
        if not error_msg and len(rpcs_by_peer) > len(backends):
            error_msg = 'Unexpected backend received load: %s' % rpcs_by_peer
        if not allow_failures and stats.num_failures > 0:
            error_msg = '%d RPCs failed' % stats.num_failures
        if not error_msg:
            return
    raise RpcDistributionError(error_msg)


def wait_until_all_rpcs_go_to_given_backends_or_fail(backends,
                                                     timeout_sec,
                                                     num_rpcs=_NUM_TEST_RPCS):
    _verify_rpcs_to_given_backends(backends,
                                   timeout_sec,
                                   num_rpcs,
                                   allow_failures=True)


def wait_until_all_rpcs_go_to_given_backends(backends,
                                             timeout_sec,
                                             num_rpcs=_NUM_TEST_RPCS):
    _verify_rpcs_to_given_backends(backends,
                                   timeout_sec,
                                   num_rpcs,
                                   allow_failures=False)


def compare_distributions(actual_distribution, expected_distribution,
                          threshold):
    """Compare if two distributions are similar.

    Args:
      actual_distribution: A list of floats, contains the actual distribution.
      expected_distribution: A list of floats, contains the expected distribution.
      threshold: Number within [0,100], the threshold percentage by which the
        actual distribution can differ from the expected distribution.

    Returns:
      The similarity between the distributions as a boolean. Returns true if the
      actual distribution lies within the threshold of the expected
      distribution, false otherwise.
    
    Raises:
      ValueError: if threshold is not with in [0,100].
      Exception: containing detailed error messages.
    """
    if len(expected_distribution) != len(actual_distribution):
        raise Exception(
            'Error: expected and actual distributions have different size (%d vs %d)'
            % (len(expected_distribution), len(actual_distribution)))
    if threshold < 0 or threshold > 100:
        raise ValueError('Value error: Threshold should be between 0 to 100')
    threshold_fraction = threshold / 100.0
    for expected, actual in zip(expected_distribution, actual_distribution):
        if actual < (expected * (1 - threshold_fraction)):
            raise Exception("actual(%f) < expected(%f-%d%%)" %
                            (actual, expected, threshold))
        if actual > (expected * (1 + threshold_fraction)):
            raise Exception("actual(%f) > expected(%f+%d%%)" %
                            (actual, expected, threshold))
    return True


def compare_expected_instances(stats, expected_instances):
    """Compare if stats have expected instances for each type of RPC.

    Args:
      stats: LoadBalancerStatsResponse reported by interop client.
      expected_instances: a dict with key as the RPC type (string), value as
        the expected backend instances (list of strings).

    Returns:
      Returns true if the instances are expected. False if not.
    """
    for rpc_type, expected_peers in expected_instances.items():
        rpcs_by_peer_for_type = stats.rpcs_by_method[rpc_type]
        rpcs_by_peer = rpcs_by_peer_for_type.rpcs_by_peer if rpcs_by_peer_for_type else None
        logger.debug('rpc: %s, by_peer: %s', rpc_type, rpcs_by_peer)
        peers = list(rpcs_by_peer.keys())
        if set(peers) != set(expected_peers):
            logger.info('unexpected peers for %s, got %s, want %s', rpc_type,
                        peers, expected_peers)
            return False
    return True


def test_backends_restart(gcp, backend_service, instance_group):
    logger.info('Running test_backends_restart')
    instance_names = get_instance_names(gcp, instance_group)
    num_instances = len(instance_names)
    start_time = time.time()
    wait_until_all_rpcs_go_to_given_backends(instance_names,
                                             _WAIT_FOR_STATS_SEC)
    try:
        resize_instance_group(gcp, instance_group, 0)
        wait_until_all_rpcs_go_to_given_backends_or_fail([],
                                                         _WAIT_FOR_BACKEND_SEC)
    finally:
        resize_instance_group(gcp, instance_group, num_instances)
    wait_for_healthy_backends(gcp, backend_service, instance_group)
    new_instance_names = get_instance_names(gcp, instance_group)
    wait_until_all_rpcs_go_to_given_backends(new_instance_names,
                                             _WAIT_FOR_BACKEND_SEC)


def test_change_backend_service(gcp, original_backend_service, instance_group,
                                alternate_backend_service,
                                same_zone_instance_group):
    logger.info('Running test_change_backend_service')
    original_backend_instances = get_instance_names(gcp, instance_group)
    alternate_backend_instances = get_instance_names(gcp,
                                                     same_zone_instance_group)
    patch_backend_instances(gcp, alternate_backend_service,
                            [same_zone_instance_group])
    wait_for_healthy_backends(gcp, original_backend_service, instance_group)
    wait_for_healthy_backends(gcp, alternate_backend_service,
                              same_zone_instance_group)
    wait_until_all_rpcs_go_to_given_backends(original_backend_instances,
                                             _WAIT_FOR_STATS_SEC)
    try:
        patch_url_map_backend_service(gcp, alternate_backend_service)
        wait_until_all_rpcs_go_to_given_backends(alternate_backend_instances,
                                                 _WAIT_FOR_URL_MAP_PATCH_SEC)
    finally:
        patch_url_map_backend_service(gcp, original_backend_service)
        patch_backend_instances(gcp, alternate_backend_service, [])


def test_gentle_failover(gcp,
                         backend_service,
                         primary_instance_group,
                         secondary_instance_group,
                         swapped_primary_and_secondary=False):
    logger.info('Running test_gentle_failover')
    num_primary_instances = len(get_instance_names(gcp, primary_instance_group))
    min_instances_for_gentle_failover = 3  # Need >50% failure to start failover
    try:
        if num_primary_instances < min_instances_for_gentle_failover:
            resize_instance_group(gcp, primary_instance_group,
                                  min_instances_for_gentle_failover)
        patch_backend_instances(
            gcp, backend_service,
            [primary_instance_group, secondary_instance_group])
        primary_instance_names = get_instance_names(gcp, primary_instance_group)
        secondary_instance_names = get_instance_names(gcp,
                                                      secondary_instance_group)
        wait_for_healthy_backends(gcp, backend_service, primary_instance_group)
        wait_for_healthy_backends(gcp, backend_service,
                                  secondary_instance_group)
        wait_until_all_rpcs_go_to_given_backends(primary_instance_names,
                                                 _WAIT_FOR_STATS_SEC)
        instances_to_stop = primary_instance_names[:-1]
        remaining_instances = primary_instance_names[-1:]
        try:
            set_serving_status(instances_to_stop,
                               gcp.service_port,
                               serving=False)
            wait_until_all_rpcs_go_to_given_backends(
                remaining_instances + secondary_instance_names,
                _WAIT_FOR_BACKEND_SEC)
        finally:
            set_serving_status(primary_instance_names,
                               gcp.service_port,
                               serving=True)
    except RpcDistributionError as e:
        if not swapped_primary_and_secondary and is_primary_instance_group(
                gcp, secondary_instance_group):
            # Swap expectation of primary and secondary instance groups.
            test_gentle_failover(gcp,
                                 backend_service,
                                 secondary_instance_group,
                                 primary_instance_group,
                                 swapped_primary_and_secondary=True)
        else:
            raise e
    finally:
        patch_backend_instances(gcp, backend_service, [primary_instance_group])
        resize_instance_group(gcp, primary_instance_group,
                              num_primary_instances)
        instance_names = get_instance_names(gcp, primary_instance_group)
        wait_until_all_rpcs_go_to_given_backends(instance_names,
                                                 _WAIT_FOR_BACKEND_SEC)


def test_ping_pong(gcp, backend_service, instance_group):
    logger.info('Running test_ping_pong')
    wait_for_healthy_backends(gcp, backend_service, instance_group)
    instance_names = get_instance_names(gcp, instance_group)
    wait_until_all_rpcs_go_to_given_backends(instance_names,
                                             _WAIT_FOR_STATS_SEC)


def test_remove_instance_group(gcp, backend_service, instance_group,
                               same_zone_instance_group):
    logger.info('Running test_remove_instance_group')
    try:
        patch_backend_instances(gcp,
                                backend_service,
                                [instance_group, same_zone_instance_group],
                                balancing_mode='RATE')
        wait_for_healthy_backends(gcp, backend_service, instance_group)
        wait_for_healthy_backends(gcp, backend_service,
                                  same_zone_instance_group)
        instance_names = get_instance_names(gcp, instance_group)
        same_zone_instance_names = get_instance_names(gcp,
                                                      same_zone_instance_group)
        try:
            wait_until_all_rpcs_go_to_given_backends(
                instance_names + same_zone_instance_names,
                _WAIT_FOR_OPERATION_SEC)
            remaining_instance_group = same_zone_instance_group
            remaining_instance_names = same_zone_instance_names
        except RpcDistributionError as e:
            # If connected to TD in a different zone, we may route traffic to
            # only one instance group. Determine which group that is to continue
            # with the remainder of the test case.
            try:
                wait_until_all_rpcs_go_to_given_backends(
                    instance_names, _WAIT_FOR_STATS_SEC)
                remaining_instance_group = same_zone_instance_group
                remaining_instance_names = same_zone_instance_names
            except RpcDistributionError as e:
                wait_until_all_rpcs_go_to_given_backends(
                    same_zone_instance_names, _WAIT_FOR_STATS_SEC)
                remaining_instance_group = instance_group
                remaining_instance_names = instance_names
        patch_backend_instances(gcp,
                                backend_service, [remaining_instance_group],
                                balancing_mode='RATE')
        wait_until_all_rpcs_go_to_given_backends(remaining_instance_names,
                                                 _WAIT_FOR_BACKEND_SEC)
    finally:
        patch_backend_instances(gcp, backend_service, [instance_group])
        wait_until_all_rpcs_go_to_given_backends(instance_names,
                                                 _WAIT_FOR_BACKEND_SEC)


def test_round_robin(gcp, backend_service, instance_group):
    logger.info('Running test_round_robin')
    wait_for_healthy_backends(gcp, backend_service, instance_group)
    instance_names = get_instance_names(gcp, instance_group)
    threshold = 1
    wait_until_all_rpcs_go_to_given_backends(instance_names,
                                             _WAIT_FOR_STATS_SEC)
    # TODO(ericgribkoff) Delayed config propagation from earlier tests
    # may result in briefly receiving an empty EDS update, resulting in failed
    # RPCs. Retry distribution validation if this occurs; long-term fix is
    # creating new backend resources for each individual test case.
    # Each attempt takes 10 seconds. Config propagation can take several
    # minutes.
    max_attempts = 40
    for i in range(max_attempts):
        stats = get_client_stats(_NUM_TEST_RPCS, _WAIT_FOR_STATS_SEC)
        requests_received = [stats.rpcs_by_peer[x] for x in stats.rpcs_by_peer]
        total_requests_received = sum(requests_received)
        if total_requests_received != _NUM_TEST_RPCS:
            logger.info('Unexpected RPC failures, retrying: %s', stats)
            continue
        expected_requests = total_requests_received / len(instance_names)
        for instance in instance_names:
            if abs(stats.rpcs_by_peer[instance] -
                   expected_requests) > threshold:
                raise Exception(
                    'RPC peer distribution differs from expected by more than %d '
                    'for instance %s (%s)' % (threshold, instance, stats))
        return
    raise Exception('RPC failures persisted through %d retries' % max_attempts)


def test_secondary_locality_gets_no_requests_on_partial_primary_failure(
        gcp,
        backend_service,
        primary_instance_group,
        secondary_instance_group,
        swapped_primary_and_secondary=False):
    logger.info(
        'Running secondary_locality_gets_no_requests_on_partial_primary_failure'
    )
    try:
        patch_backend_instances(
            gcp, backend_service,
            [primary_instance_group, secondary_instance_group])
        wait_for_healthy_backends(gcp, backend_service, primary_instance_group)
        wait_for_healthy_backends(gcp, backend_service,
                                  secondary_instance_group)
        primary_instance_names = get_instance_names(gcp, primary_instance_group)
        wait_until_all_rpcs_go_to_given_backends(primary_instance_names,
                                                 _WAIT_FOR_STATS_SEC)
        instances_to_stop = primary_instance_names[:1]
        remaining_instances = primary_instance_names[1:]
        try:
            set_serving_status(instances_to_stop,
                               gcp.service_port,
                               serving=False)
            wait_until_all_rpcs_go_to_given_backends(remaining_instances,
                                                     _WAIT_FOR_BACKEND_SEC)
        finally:
            set_serving_status(primary_instance_names,
                               gcp.service_port,
                               serving=True)
    except RpcDistributionError as e:
        if not swapped_primary_and_secondary and is_primary_instance_group(
                gcp, secondary_instance_group):
            # Swap expectation of primary and secondary instance groups.
            test_secondary_locality_gets_no_requests_on_partial_primary_failure(
                gcp,
                backend_service,
                secondary_instance_group,
                primary_instance_group,
                swapped_primary_and_secondary=True)
        else:
            raise e
    finally:
        patch_backend_instances(gcp, backend_service, [primary_instance_group])


def test_secondary_locality_gets_requests_on_primary_failure(
        gcp,
        backend_service,
        primary_instance_group,
        secondary_instance_group,
        swapped_primary_and_secondary=False):
    logger.info('Running secondary_locality_gets_requests_on_primary_failure')
    try:
        patch_backend_instances(
            gcp, backend_service,
            [primary_instance_group, secondary_instance_group])
        wait_for_healthy_backends(gcp, backend_service, primary_instance_group)
        wait_for_healthy_backends(gcp, backend_service,
                                  secondary_instance_group)
        primary_instance_names = get_instance_names(gcp, primary_instance_group)
        secondary_instance_names = get_instance_names(gcp,
                                                      secondary_instance_group)
        wait_until_all_rpcs_go_to_given_backends(primary_instance_names,
                                                 _WAIT_FOR_STATS_SEC)
        try:
            set_serving_status(primary_instance_names,
                               gcp.service_port,
                               serving=False)
            wait_until_all_rpcs_go_to_given_backends(secondary_instance_names,
                                                     _WAIT_FOR_BACKEND_SEC)
        finally:
            set_serving_status(primary_instance_names,
                               gcp.service_port,
                               serving=True)
    except RpcDistributionError as e:
        if not swapped_primary_and_secondary and is_primary_instance_group(
                gcp, secondary_instance_group):
            # Swap expectation of primary and secondary instance groups.
            test_secondary_locality_gets_requests_on_primary_failure(
                gcp,
                backend_service,
                secondary_instance_group,
                primary_instance_group,
                swapped_primary_and_secondary=True)
        else:
            raise e
    finally:
        patch_backend_instances(gcp, backend_service, [primary_instance_group])


def prepare_services_for_urlmap_tests(gcp, original_backend_service,
                                      instance_group, alternate_backend_service,
                                      same_zone_instance_group):
    '''
    This function prepares the services to be ready for tests that modifies
    urlmaps.

    Returns:
      Returns original and alternate backend names as lists of strings.
    '''
    logger.info('waiting for original backends to become healthy')
    wait_for_healthy_backends(gcp, original_backend_service, instance_group)

    patch_backend_instances(gcp, alternate_backend_service,
                            [same_zone_instance_group])
    logger.info('waiting for alternate to become healthy')
    wait_for_healthy_backends(gcp, alternate_backend_service,
                              same_zone_instance_group)

    original_backend_instances = get_instance_names(gcp, instance_group)
    logger.info('original backends instances: %s', original_backend_instances)

    alternate_backend_instances = get_instance_names(gcp,
                                                     same_zone_instance_group)
    logger.info('alternate backends instances: %s', alternate_backend_instances)

    # Start with all traffic going to original_backend_service.
    logger.info('waiting for traffic to all go to original backends')
    wait_until_all_rpcs_go_to_given_backends(original_backend_instances,
                                             _WAIT_FOR_STATS_SEC)
    return original_backend_instances, alternate_backend_instances


def test_traffic_splitting(gcp, original_backend_service, instance_group,
                           alternate_backend_service, same_zone_instance_group):
    # This test start with all traffic going to original_backend_service. Then
    # it updates URL-map to set default action to traffic splitting between
    # original and alternate. It waits for all backends in both services to
    # receive traffic, then verifies that weights are expected.
    logger.info('Running test_traffic_splitting')

    original_backend_instances, alternate_backend_instances = prepare_services_for_urlmap_tests(
        gcp, original_backend_service, instance_group,
        alternate_backend_service, same_zone_instance_group)

    try:
        # Patch urlmap, change route action to traffic splitting between
        # original and alternate.
        logger.info('patching url map with traffic splitting')
        original_service_percentage, alternate_service_percentage = 20, 80
        patch_url_map_backend_service(
            gcp,
            services_with_weights={
                original_backend_service: original_service_percentage,
                alternate_backend_service: alternate_service_percentage,
            })
        # Split percentage between instances: [20,80] -> [10,10,40,40].
        expected_instance_percentage = [
            original_service_percentage * 1.0 / len(original_backend_instances)
        ] * len(original_backend_instances) + [
            alternate_service_percentage * 1.0 /
            len(alternate_backend_instances)
        ] * len(alternate_backend_instances)

        # Wait for traffic to go to both services.
        logger.info(
            'waiting for traffic to go to all backends (including alternate)')
        wait_until_all_rpcs_go_to_given_backends(
            original_backend_instances + alternate_backend_instances,
            _WAIT_FOR_STATS_SEC)

        # Verify that weights between two services are expected.
        retry_count = 10
        # Each attempt takes about 10 seconds, 10 retries is equivalent to 100
        # seconds timeout.
        for i in range(retry_count):
            stats = get_client_stats(_NUM_TEST_RPCS, _WAIT_FOR_STATS_SEC)
            got_instance_count = [
                stats.rpcs_by_peer[i] for i in original_backend_instances
            ] + [stats.rpcs_by_peer[i] for i in alternate_backend_instances]
            total_count = sum(got_instance_count)
            got_instance_percentage = [
                x * 100.0 / total_count for x in got_instance_count
            ]

            try:
                compare_distributions(got_instance_percentage,
                                      expected_instance_percentage, 5)
            except Exception as e:
                logger.info('attempt %d', i)
                logger.info('got percentage: %s', got_instance_percentage)
                logger.info('expected percentage: %s',
                            expected_instance_percentage)
                logger.info(e)
                if i == retry_count - 1:
                    raise Exception(
                        'RPC distribution (%s) differs from expected (%s)' %
                        (got_instance_percentage, expected_instance_percentage))
            else:
                logger.info("success")
                break
    finally:
        patch_url_map_backend_service(gcp, original_backend_service)
        patch_backend_instances(gcp, alternate_backend_service, [])


def test_path_matching(gcp, original_backend_service, instance_group,
                       alternate_backend_service, same_zone_instance_group):
    # This test start with all traffic (UnaryCall and EmptyCall) going to
    # original_backend_service.
    #
    # Then it updates URL-map to add routes, to make UnaryCall and EmptyCall to
    # go different backends. It waits for all backends in both services to
    # receive traffic, then verifies that traffic goes to the expected
    # backends.
    logger.info('Running test_path_matching')

    original_backend_instances, alternate_backend_instances = prepare_services_for_urlmap_tests(
        gcp, original_backend_service, instance_group,
        alternate_backend_service, same_zone_instance_group)

    try:
        # A list of tuples (route_rules, expected_instances).
        test_cases = [
            (
                [{
                    'priority': 0,
                    # FullPath EmptyCall -> alternate_backend_service.
                    'matchRules': [{
                        'fullPathMatch': '/grpc.testing.TestService/EmptyCall'
                    }],
                    'service': alternate_backend_service.url
                }],
                {
                    "EmptyCall": alternate_backend_instances,
                    "UnaryCall": original_backend_instances
                }),
            (
                [{
                    'priority': 0,
                    # Prefix UnaryCall -> alternate_backend_service.
                    'matchRules': [{
                        'prefixMatch': '/grpc.testing.TestService/Unary'
                    }],
                    'service': alternate_backend_service.url
                }],
                {
                    "UnaryCall": alternate_backend_instances,
                    "EmptyCall": original_backend_instances
                }),
            (
                # This test case is similar to the one above (but with route
                # services swapped). This test has two routes (full_path and
                # the default) to match EmptyCall, and both routes set
                # alternative_backend_service as the action. This forces the
                # client to handle duplicate Clusters in the RDS response.
                [
                    {
                        'priority': 0,
                        # Prefix UnaryCall -> original_backend_service.
                        'matchRules': [{
                            'prefixMatch': '/grpc.testing.TestService/Unary'
                        }],
                        'service': original_backend_service.url
                    },
                    {
                        'priority': 1,
                        # FullPath EmptyCall -> alternate_backend_service.
                        'matchRules': [{
                            'fullPathMatch':
                                '/grpc.testing.TestService/EmptyCall'
                        }],
                        'service': alternate_backend_service.url
                    }
                ],
                {
                    "UnaryCall": original_backend_instances,
                    "EmptyCall": alternate_backend_instances
                })
        ]

        for (route_rules, expected_instances) in test_cases:
            logger.info('patching url map with %s', route_rules)
            patch_url_map_backend_service(gcp,
                                          original_backend_service,
                                          route_rules=route_rules)

            # Wait for traffic to go to both services.
            logger.info(
                'waiting for traffic to go to all backends (including alternate)'
            )
            wait_until_all_rpcs_go_to_given_backends(
                original_backend_instances + alternate_backend_instances,
                _WAIT_FOR_STATS_SEC)

            retry_count = 20
            # Each attempt takes about 10 seconds, 20 retries is equivalent to 200
            # seconds timeout.
            for i in range(retry_count):
                stats = get_client_stats(_NUM_TEST_RPCS, _WAIT_FOR_STATS_SEC)
                if not stats.rpcs_by_method:
                    raise ValueError(
                        'stats.rpcs_by_method is None, the interop client stats service does not support this test case'
                    )
                logger.info('attempt %d', i)
                if compare_expected_instances(stats, expected_instances):
                    logger.info("success")
                    break
    finally:
        patch_url_map_backend_service(gcp, original_backend_service)
        patch_backend_instances(gcp, alternate_backend_service, [])


def test_header_matching(gcp, original_backend_service, instance_group,
                         alternate_backend_service, same_zone_instance_group):
    # This test start with all traffic (UnaryCall and EmptyCall) going to
    # original_backend_service.
    #
    # Then it updates URL-map to add routes, to make RPCs with test headers to
    # go to different backends. It waits for all backends in both services to
    # receive traffic, then verifies that traffic goes to the expected
    # backends.
    logger.info('Running test_header_matching')

    original_backend_instances, alternate_backend_instances = prepare_services_for_urlmap_tests(
        gcp, original_backend_service, instance_group,
        alternate_backend_service, same_zone_instance_group)

    try:
        # A list of tuples (route_rules, expected_instances).
        test_cases = [(
            [{
                'priority': 0,
                # Header ExactMatch -> alternate_backend_service.
                # EmptyCall is sent with the metadata.
                'matchRules': [{
                    'prefixMatch':
                        '/',
                    'headerMatches': [{
                        'headerName': _TEST_METADATA_KEY,
                        'exactMatch': _TEST_METADATA_VALUE
                    }]
                }],
                'service': alternate_backend_service.url
            }],
            {
                "EmptyCall": alternate_backend_instances,
                "UnaryCall": original_backend_instances
            })]

        for (route_rules, expected_instances) in test_cases:
            logger.info('patching url map with %s -> alternative',
                        route_rules[0]['matchRules'])
            patch_url_map_backend_service(gcp,
                                          original_backend_service,
                                          route_rules=route_rules)

            # Wait for traffic to go to both services.
            logger.info(
                'waiting for traffic to go to all backends (including alternate)'
            )
            wait_until_all_rpcs_go_to_given_backends(
                original_backend_instances + alternate_backend_instances,
                _WAIT_FOR_STATS_SEC)

            retry_count = 10
            # Each attempt takes about 10 seconds, 10 retries is equivalent to 100
            # seconds timeout.
            for i in range(retry_count):
                stats = get_client_stats(_NUM_TEST_RPCS, _WAIT_FOR_STATS_SEC)
                if not stats.rpcs_by_method:
                    raise ValueError(
                        'stats.rpcs_by_method is None, the interop client stats service does not support this test case'
                    )
                logger.info('attempt %d', i)
                if compare_expected_instances(stats, expected_instances):
                    logger.info("success")
                    break
    finally:
        patch_url_map_backend_service(gcp, original_backend_service)
        patch_backend_instances(gcp, alternate_backend_service, [])


def get_serving_status(instance, service_port):
    with grpc.insecure_channel('%s:%d' % (instance, service_port)) as channel:
        health_stub = health_pb2_grpc.HealthStub(channel)
        return health_stub.Check(health_pb2.HealthCheckRequest())


def set_serving_status(instances, service_port, serving):
    logger.info('setting %s serving status to %s', instances, serving)
    for instance in instances:
        with grpc.insecure_channel('%s:%d' %
                                   (instance, service_port)) as channel:
            logger.info('setting %s serving status to %s', instance, serving)
            stub = test_pb2_grpc.XdsUpdateHealthServiceStub(channel)
            retry_count = 5
            for i in range(5):
                if serving:
                    stub.SetServing(empty_pb2.Empty())
                else:
                    stub.SetNotServing(empty_pb2.Empty())
                serving_status = get_serving_status(instance, service_port)
                logger.info('got instance service status %s', serving_status)
                want_status = health_pb2.HealthCheckResponse.SERVING if serving else health_pb2.HealthCheckResponse.NOT_SERVING
                if serving_status.status == want_status:
                    break
                if i == retry_count - 1:
                    raise Exception(
                        'failed to set instance service status after %d retries'
                        % retry_count)


def is_primary_instance_group(gcp, instance_group):
    # Clients may connect to a TD instance in a different region than the
    # client, in which case primary/secondary assignments may not be based on
    # the client's actual locality.
    instance_names = get_instance_names(gcp, instance_group)
    stats = get_client_stats(_NUM_TEST_RPCS, _WAIT_FOR_STATS_SEC)
    return all(peer in instance_names for peer in stats.rpcs_by_peer.keys())


def get_startup_script(path_to_server_binary, service_port):
    if path_to_server_binary:
        return "nohup %s --port=%d 1>/dev/null &" % (path_to_server_binary,
                                                     service_port)
    else:
        return """#!/bin/bash
sudo apt update
sudo apt install -y git default-jdk
mkdir java_server
pushd java_server
git clone https://github.com/grpc/grpc-java.git
pushd grpc-java
pushd interop-testing
../gradlew installDist -x test -PskipCodegen=true -PskipAndroid=true

nohup build/install/grpc-interop-testing/bin/xds-test-server \
    --port=%d 1>/dev/null &""" % service_port


def create_instance_template(gcp, name, network, source_image, machine_type,
                             startup_script):
    config = {
        'name': name,
        'properties': {
            'tags': {
                'items': ['allow-health-checks']
            },
            'machineType': machine_type,
            'serviceAccounts': [{
                'email': 'default',
                'scopes': ['https://www.googleapis.com/auth/cloud-platform',]
            }],
            'networkInterfaces': [{
                'accessConfigs': [{
                    'type': 'ONE_TO_ONE_NAT'
                }],
                'network': network
            }],
            'disks': [{
                'boot': True,
                'initializeParams': {
                    'sourceImage': source_image
                }
            }],
            'metadata': {
                'items': [{
                    'key': 'startup-script',
                    'value': startup_script
                }]
            }
        }
    }

    logger.debug('Sending GCP request with body=%s', config)
    result = gcp.compute.instanceTemplates().insert(
        project=gcp.project, body=config).execute(num_retries=_GCP_API_RETRIES)
    wait_for_global_operation(gcp, result['name'])
    gcp.instance_template = GcpResource(config['name'], result['targetLink'])


def add_instance_group(gcp, zone, name, size):
    config = {
        'name': name,
        'instanceTemplate': gcp.instance_template.url,
        'targetSize': size,
        'namedPorts': [{
            'name': 'grpc',
            'port': gcp.service_port
        }]
    }

    logger.debug('Sending GCP request with body=%s', config)
    result = gcp.compute.instanceGroupManagers().insert(
        project=gcp.project, zone=zone,
        body=config).execute(num_retries=_GCP_API_RETRIES)
    wait_for_zone_operation(gcp, zone, result['name'])
    result = gcp.compute.instanceGroupManagers().get(
        project=gcp.project, zone=zone,
        instanceGroupManager=config['name']).execute(
            num_retries=_GCP_API_RETRIES)
    instance_group = InstanceGroup(config['name'], result['instanceGroup'],
                                   zone)
    gcp.instance_groups.append(instance_group)
    wait_for_instance_group_to_reach_expected_size(gcp, instance_group, size,
                                                   _WAIT_FOR_OPERATION_SEC)
    return instance_group


def create_health_check(gcp, name):
    if gcp.alpha_compute:
        config = {
            'name': name,
            'type': 'GRPC',
            'grpcHealthCheck': {
                'portSpecification': 'USE_SERVING_PORT'
            }
        }
        compute_to_use = gcp.alpha_compute
    else:
        config = {
            'name': name,
            'type': 'TCP',
            'tcpHealthCheck': {
                'portName': 'grpc'
            }
        }
        compute_to_use = gcp.compute
    logger.debug('Sending GCP request with body=%s', config)
    result = compute_to_use.healthChecks().insert(
        project=gcp.project, body=config).execute(num_retries=_GCP_API_RETRIES)
    wait_for_global_operation(gcp, result['name'])
    gcp.health_check = GcpResource(config['name'], result['targetLink'])


def create_health_check_firewall_rule(gcp, name):
    config = {
        'name': name,
        'direction': 'INGRESS',
        'allowed': [{
            'IPProtocol': 'tcp'
        }],
        'sourceRanges': ['35.191.0.0/16', '130.211.0.0/22'],
        'targetTags': ['allow-health-checks'],
    }
    logger.debug('Sending GCP request with body=%s', config)
    result = gcp.compute.firewalls().insert(
        project=gcp.project, body=config).execute(num_retries=_GCP_API_RETRIES)
    wait_for_global_operation(gcp, result['name'])
    gcp.health_check_firewall_rule = GcpResource(config['name'],
                                                 result['targetLink'])


def add_backend_service(gcp, name):
    if gcp.alpha_compute:
        protocol = 'GRPC'
        compute_to_use = gcp.alpha_compute
    else:
        protocol = 'HTTP2'
        compute_to_use = gcp.compute
    config = {
        'name': name,
        'loadBalancingScheme': 'INTERNAL_SELF_MANAGED',
        'healthChecks': [gcp.health_check.url],
        'portName': 'grpc',
        'protocol': protocol
    }
    logger.debug('Sending GCP request with body=%s', config)
    result = compute_to_use.backendServices().insert(
        project=gcp.project, body=config).execute(num_retries=_GCP_API_RETRIES)
    wait_for_global_operation(gcp, result['name'])
    backend_service = GcpResource(config['name'], result['targetLink'])
    gcp.backend_services.append(backend_service)
    return backend_service


def create_url_map(gcp, name, backend_service, host_name):
    config = {
        'name': name,
        'defaultService': backend_service.url,
        'pathMatchers': [{
            'name': _PATH_MATCHER_NAME,
            'defaultService': backend_service.url,
        }],
        'hostRules': [{
            'hosts': [host_name],
            'pathMatcher': _PATH_MATCHER_NAME
        }]
    }
    logger.debug('Sending GCP request with body=%s', config)
    result = gcp.compute.urlMaps().insert(
        project=gcp.project, body=config).execute(num_retries=_GCP_API_RETRIES)
    wait_for_global_operation(gcp, result['name'])
    gcp.url_map = GcpResource(config['name'], result['targetLink'])


def patch_url_map_host_rule_with_port(gcp, name, backend_service, host_name):
    config = {
        'hostRules': [{
            'hosts': ['%s:%d' % (host_name, gcp.service_port)],
            'pathMatcher': _PATH_MATCHER_NAME
        }]
    }
    logger.debug('Sending GCP request with body=%s', config)
    result = gcp.compute.urlMaps().patch(
        project=gcp.project, urlMap=name,
        body=config).execute(num_retries=_GCP_API_RETRIES)
    wait_for_global_operation(gcp, result['name'])


def create_target_proxy(gcp, name):
    if gcp.alpha_compute:
        config = {
            'name': name,
            'url_map': gcp.url_map.url,
            'validate_for_proxyless': True,
        }
        logger.debug('Sending GCP request with body=%s', config)
        result = gcp.alpha_compute.targetGrpcProxies().insert(
            project=gcp.project,
            body=config).execute(num_retries=_GCP_API_RETRIES)
    else:
        config = {
            'name': name,
            'url_map': gcp.url_map.url,
        }
        logger.debug('Sending GCP request with body=%s', config)
        result = gcp.compute.targetHttpProxies().insert(
            project=gcp.project,
            body=config).execute(num_retries=_GCP_API_RETRIES)
    wait_for_global_operation(gcp, result['name'])
    gcp.target_proxy = GcpResource(config['name'], result['targetLink'])


def create_global_forwarding_rule(gcp, name, potential_ports):
    if gcp.alpha_compute:
        compute_to_use = gcp.alpha_compute
    else:
        compute_to_use = gcp.compute
    for port in potential_ports:
        try:
            config = {
                'name': name,
                'loadBalancingScheme': 'INTERNAL_SELF_MANAGED',
                'portRange': str(port),
                'IPAddress': '0.0.0.0',
                'network': args.network,
                'target': gcp.target_proxy.url,
            }
            logger.debug('Sending GCP request with body=%s', config)
            result = compute_to_use.globalForwardingRules().insert(
                project=gcp.project,
                body=config).execute(num_retries=_GCP_API_RETRIES)
            wait_for_global_operation(gcp, result['name'])
            gcp.global_forwarding_rule = GcpResource(config['name'],
                                                     result['targetLink'])
            gcp.service_port = port
            return
        except googleapiclient.errors.HttpError as http_error:
            logger.warning(
                'Got error %s when attempting to create forwarding rule to '
                '0.0.0.0:%d. Retrying with another port.' % (http_error, port))


def get_health_check(gcp, health_check_name):
    result = gcp.compute.healthChecks().get(
        project=gcp.project, healthCheck=health_check_name).execute()
    gcp.health_check = GcpResource(health_check_name, result['selfLink'])


def get_health_check_firewall_rule(gcp, firewall_name):
    result = gcp.compute.firewalls().get(project=gcp.project,
                                         firewall=firewall_name).execute()
    gcp.health_check_firewall_rule = GcpResource(firewall_name,
                                                 result['selfLink'])


def get_backend_service(gcp, backend_service_name):
    result = gcp.compute.backendServices().get(
        project=gcp.project, backendService=backend_service_name).execute()
    backend_service = GcpResource(backend_service_name, result['selfLink'])
    gcp.backend_services.append(backend_service)
    return backend_service


def get_url_map(gcp, url_map_name):
    result = gcp.compute.urlMaps().get(project=gcp.project,
                                       urlMap=url_map_name).execute()
    gcp.url_map = GcpResource(url_map_name, result['selfLink'])


def get_target_proxy(gcp, target_proxy_name):
    if gcp.alpha_compute:
        result = gcp.alpha_compute.targetGrpcProxies().get(
            project=gcp.project, targetGrpcProxy=target_proxy_name).execute()
    else:
        result = gcp.compute.targetHttpProxies().get(
            project=gcp.project, targetHttpProxy=target_proxy_name).execute()
    gcp.target_proxy = GcpResource(target_proxy_name, result['selfLink'])


def get_global_forwarding_rule(gcp, forwarding_rule_name):
    result = gcp.compute.globalForwardingRules().get(
        project=gcp.project, forwardingRule=forwarding_rule_name).execute()
    gcp.global_forwarding_rule = GcpResource(forwarding_rule_name,
                                             result['selfLink'])


def get_instance_template(gcp, template_name):
    result = gcp.compute.instanceTemplates().get(
        project=gcp.project, instanceTemplate=template_name).execute()
    gcp.instance_template = GcpResource(template_name, result['selfLink'])


def get_instance_group(gcp, zone, instance_group_name):
    result = gcp.compute.instanceGroups().get(
        project=gcp.project, zone=zone,
        instanceGroup=instance_group_name).execute()
    gcp.service_port = result['namedPorts'][0]['port']
    instance_group = InstanceGroup(instance_group_name, result['selfLink'],
                                   zone)
    gcp.instance_groups.append(instance_group)
    return instance_group


def delete_global_forwarding_rule(gcp):
    try:
        result = gcp.compute.globalForwardingRules().delete(
            project=gcp.project,
            forwardingRule=gcp.global_forwarding_rule.name).execute(
                num_retries=_GCP_API_RETRIES)
        wait_for_global_operation(gcp, result['name'])
    except googleapiclient.errors.HttpError as http_error:
        logger.info('Delete failed: %s', http_error)


def delete_target_proxy(gcp):
    try:
        if gcp.alpha_compute:
            result = gcp.alpha_compute.targetGrpcProxies().delete(
                project=gcp.project,
                targetGrpcProxy=gcp.target_proxy.name).execute(
                    num_retries=_GCP_API_RETRIES)
        else:
            result = gcp.compute.targetHttpProxies().delete(
                project=gcp.project,
                targetHttpProxy=gcp.target_proxy.name).execute(
                    num_retries=_GCP_API_RETRIES)
        wait_for_global_operation(gcp, result['name'])
    except googleapiclient.errors.HttpError as http_error:
        logger.info('Delete failed: %s', http_error)


def delete_url_map(gcp):
    try:
        result = gcp.compute.urlMaps().delete(
            project=gcp.project,
            urlMap=gcp.url_map.name).execute(num_retries=_GCP_API_RETRIES)
        wait_for_global_operation(gcp, result['name'])
    except googleapiclient.errors.HttpError as http_error:
        logger.info('Delete failed: %s', http_error)


def delete_backend_services(gcp):
    for backend_service in gcp.backend_services:
        try:
            result = gcp.compute.backendServices().delete(
                project=gcp.project,
                backendService=backend_service.name).execute(
                    num_retries=_GCP_API_RETRIES)
            wait_for_global_operation(gcp, result['name'])
        except googleapiclient.errors.HttpError as http_error:
            logger.info('Delete failed: %s', http_error)


def delete_firewall(gcp):
    try:
        result = gcp.compute.firewalls().delete(
            project=gcp.project,
            firewall=gcp.health_check_firewall_rule.name).execute(
                num_retries=_GCP_API_RETRIES)
        wait_for_global_operation(gcp, result['name'])
    except googleapiclient.errors.HttpError as http_error:
        logger.info('Delete failed: %s', http_error)


def delete_health_check(gcp):
    try:
        result = gcp.compute.healthChecks().delete(
            project=gcp.project, healthCheck=gcp.health_check.name).execute(
                num_retries=_GCP_API_RETRIES)
        wait_for_global_operation(gcp, result['name'])
    except googleapiclient.errors.HttpError as http_error:
        logger.info('Delete failed: %s', http_error)


def delete_instance_groups(gcp):
    for instance_group in gcp.instance_groups:
        try:
            result = gcp.compute.instanceGroupManagers().delete(
                project=gcp.project,
                zone=instance_group.zone,
                instanceGroupManager=instance_group.name).execute(
                    num_retries=_GCP_API_RETRIES)
            wait_for_zone_operation(gcp,
                                    instance_group.zone,
                                    result['name'],
                                    timeout_sec=_WAIT_FOR_BACKEND_SEC)
        except googleapiclient.errors.HttpError as http_error:
            logger.info('Delete failed: %s', http_error)


def delete_instance_template(gcp):
    try:
        result = gcp.compute.instanceTemplates().delete(
            project=gcp.project,
            instanceTemplate=gcp.instance_template.name).execute(
                num_retries=_GCP_API_RETRIES)
        wait_for_global_operation(gcp, result['name'])
    except googleapiclient.errors.HttpError as http_error:
        logger.info('Delete failed: %s', http_error)


def patch_backend_instances(gcp,
                            backend_service,
                            instance_groups,
                            balancing_mode='UTILIZATION'):
    if gcp.alpha_compute:
        compute_to_use = gcp.alpha_compute
    else:
        compute_to_use = gcp.compute
    config = {
        'backends': [{
            'group': instance_group.url,
            'balancingMode': balancing_mode,
            'maxRate': 1 if balancing_mode == 'RATE' else None
        } for instance_group in instance_groups],
    }
    logger.debug('Sending GCP request with body=%s', config)
    result = compute_to_use.backendServices().patch(
        project=gcp.project, backendService=backend_service.name,
        body=config).execute(num_retries=_GCP_API_RETRIES)
    wait_for_global_operation(gcp,
                              result['name'],
                              timeout_sec=_WAIT_FOR_BACKEND_SEC)


def resize_instance_group(gcp,
                          instance_group,
                          new_size,
                          timeout_sec=_WAIT_FOR_OPERATION_SEC):
    result = gcp.compute.instanceGroupManagers().resize(
        project=gcp.project,
        zone=instance_group.zone,
        instanceGroupManager=instance_group.name,
        size=new_size).execute(num_retries=_GCP_API_RETRIES)
    wait_for_zone_operation(gcp,
                            instance_group.zone,
                            result['name'],
                            timeout_sec=360)
    wait_for_instance_group_to_reach_expected_size(gcp, instance_group,
                                                   new_size, timeout_sec)


def patch_url_map_backend_service(gcp,
                                  backend_service=None,
                                  services_with_weights=None,
                                  route_rules=None):
    '''change url_map's backend service

    Only one of backend_service and service_with_weights can be not None.
    '''
    if backend_service and services_with_weights:
        raise ValueError(
            'both backend_service and service_with_weights are not None.')

    default_service = backend_service.url if backend_service else None
    default_route_action = {
        'weightedBackendServices': [{
            'backendService': service.url,
            'weight': w,
        } for service, w in services_with_weights.items()]
    } if services_with_weights else None

    config = {
        'pathMatchers': [{
            'name': _PATH_MATCHER_NAME,
            'defaultService': default_service,
            'defaultRouteAction': default_route_action,
            'routeRules': route_rules,
        }]
    }
    logger.debug('Sending GCP request with body=%s', config)
    result = gcp.compute.urlMaps().patch(
        project=gcp.project, urlMap=gcp.url_map.name,
        body=config).execute(num_retries=_GCP_API_RETRIES)
    wait_for_global_operation(gcp, result['name'])


def wait_for_instance_group_to_reach_expected_size(gcp, instance_group,
                                                   expected_size, timeout_sec):
    start_time = time.time()
    while True:
        current_size = len(get_instance_names(gcp, instance_group))
        if current_size == expected_size:
            break
        if time.time() - start_time > timeout_sec:
            raise Exception(
                'Instance group had expected size %d but actual size %d' %
                (expected_size, current_size))
        time.sleep(2)


def wait_for_global_operation(gcp,
                              operation,
                              timeout_sec=_WAIT_FOR_OPERATION_SEC):
    start_time = time.time()
    while time.time() - start_time <= timeout_sec:
        result = gcp.compute.globalOperations().get(
            project=gcp.project,
            operation=operation).execute(num_retries=_GCP_API_RETRIES)
        if result['status'] == 'DONE':
            if 'error' in result:
                raise Exception(result['error'])
            return
        time.sleep(2)
    raise Exception('Operation %s did not complete within %d' %
                    (operation, timeout_sec))


def wait_for_zone_operation(gcp,
                            zone,
                            operation,
                            timeout_sec=_WAIT_FOR_OPERATION_SEC):
    start_time = time.time()
    while time.time() - start_time <= timeout_sec:
        result = gcp.compute.zoneOperations().get(
            project=gcp.project, zone=zone,
            operation=operation).execute(num_retries=_GCP_API_RETRIES)
        if result['status'] == 'DONE':
            if 'error' in result:
                raise Exception(result['error'])
            return
        time.sleep(2)
    raise Exception('Operation %s did not complete within %d' %
                    (operation, timeout_sec))


def wait_for_healthy_backends(gcp,
                              backend_service,
                              instance_group,
                              timeout_sec=_WAIT_FOR_BACKEND_SEC):
    start_time = time.time()
    config = {'group': instance_group.url}
    instance_names = get_instance_names(gcp, instance_group)
    expected_size = len(instance_names)
    while time.time() - start_time <= timeout_sec:
        for instance_name in instance_names:
            try:
                status = get_serving_status(instance_name, gcp.service_port)
                logger.info('serving status response from %s: %s',
                            instance_name, status)
            except grpc.RpcError as rpc_error:
                logger.info('checking serving status of %s failed: %s',
                            instance_name, rpc_error)
        result = gcp.compute.backendServices().getHealth(
            project=gcp.project,
            backendService=backend_service.name,
            body=config).execute(num_retries=_GCP_API_RETRIES)
        if 'healthStatus' in result:
            logger.info('received GCP healthStatus: %s', result['healthStatus'])
            healthy = True
            for instance in result['healthStatus']:
                if instance['healthState'] != 'HEALTHY':
                    healthy = False
                    break
            if healthy and expected_size == len(result['healthStatus']):
                return
        else:
            logger.info('no healthStatus received from GCP')
        time.sleep(5)
    raise Exception('Not all backends became healthy within %d seconds: %s' %
                    (timeout_sec, result))


def get_instance_names(gcp, instance_group):
    instance_names = []
    result = gcp.compute.instanceGroups().listInstances(
        project=gcp.project,
        zone=instance_group.zone,
        instanceGroup=instance_group.name,
        body={
            'instanceState': 'ALL'
        }).execute(num_retries=_GCP_API_RETRIES)
    if 'items' not in result:
        return []
    for item in result['items']:
        # listInstances() returns the full URL of the instance, which ends with
        # the instance name. compute.instances().get() requires using the
        # instance name (not the full URL) to look up instance details, so we
        # just extract the name manually.
        instance_name = item['instance'].split('/')[-1]
        instance_names.append(instance_name)
    logger.info('retrieved instance names: %s', instance_names)
    return instance_names


def clean_up(gcp):
    if gcp.global_forwarding_rule:
        delete_global_forwarding_rule(gcp)
    if gcp.target_proxy:
        delete_target_proxy(gcp)
    if gcp.url_map:
        delete_url_map(gcp)
    delete_backend_services(gcp)
    if gcp.health_check_firewall_rule:
        delete_firewall(gcp)
    if gcp.health_check:
        delete_health_check(gcp)
    delete_instance_groups(gcp)
    if gcp.instance_template:
        delete_instance_template(gcp)


class InstanceGroup(object):

    def __init__(self, name, url, zone):
        self.name = name
        self.url = url
        self.zone = zone


class GcpResource(object):

    def __init__(self, name, url):
        self.name = name
        self.url = url


class GcpState(object):

    def __init__(self, compute, alpha_compute, project):
        self.compute = compute
        self.alpha_compute = alpha_compute
        self.project = project
        self.health_check = None
        self.health_check_firewall_rule = None
        self.backend_services = []
        self.url_map = None
        self.target_proxy = None
        self.global_forwarding_rule = None
        self.service_port = None
        self.instance_template = None
        self.instance_groups = []


alpha_compute = None
if args.compute_discovery_document:
    with open(args.compute_discovery_document, 'r') as discovery_doc:
        compute = googleapiclient.discovery.build_from_document(
            discovery_doc.read())
    if not args.only_stable_gcp_apis and args.alpha_compute_discovery_document:
        with open(args.alpha_compute_discovery_document, 'r') as discovery_doc:
            alpha_compute = googleapiclient.discovery.build_from_document(
                discovery_doc.read())
else:
    compute = googleapiclient.discovery.build('compute', 'v1')
    if not args.only_stable_gcp_apis:
        alpha_compute = googleapiclient.discovery.build('compute', 'alpha')

try:
    gcp = GcpState(compute, alpha_compute, args.project_id)
    gcp_suffix = args.gcp_suffix
    health_check_name = _BASE_HEALTH_CHECK_NAME + gcp_suffix
    if not args.use_existing_gcp_resources:
        num_attempts = 5
        for i in range(num_attempts):
            try:
                logger.info('Using GCP suffix %s', gcp_suffix)
                create_health_check(gcp, health_check_name)
                break
            except googleapiclient.errors.HttpError as http_error:
                gcp_suffix = '%s-%04d' % (gcp_suffix, random.randint(0, 9999))
                health_check_name = _BASE_HEALTH_CHECK_NAME + gcp_suffix
                logger.exception('HttpError when creating health check')
        if gcp.health_check is None:
            raise Exception('Failed to create health check name after %d '
                            'attempts' % num_attempts)
    firewall_name = _BASE_FIREWALL_RULE_NAME + gcp_suffix
    backend_service_name = _BASE_BACKEND_SERVICE_NAME + gcp_suffix
    alternate_backend_service_name = _BASE_BACKEND_SERVICE_NAME + '-alternate' + gcp_suffix
    url_map_name = _BASE_URL_MAP_NAME + gcp_suffix
    service_host_name = _BASE_SERVICE_HOST + gcp_suffix
    target_proxy_name = _BASE_TARGET_PROXY_NAME + gcp_suffix
    forwarding_rule_name = _BASE_FORWARDING_RULE_NAME + gcp_suffix
    template_name = _BASE_TEMPLATE_NAME + gcp_suffix
    instance_group_name = _BASE_INSTANCE_GROUP_NAME + gcp_suffix
    same_zone_instance_group_name = _BASE_INSTANCE_GROUP_NAME + '-same-zone' + gcp_suffix
    secondary_zone_instance_group_name = _BASE_INSTANCE_GROUP_NAME + '-secondary-zone' + gcp_suffix
    if args.use_existing_gcp_resources:
        logger.info('Reusing existing GCP resources')
        get_health_check(gcp, health_check_name)
        try:
            get_health_check_firewall_rule(gcp, firewall_name)
        except googleapiclient.errors.HttpError as http_error:
            # Firewall rule may be auto-deleted periodically depending on GCP
            # project settings.
            logger.exception('Failed to find firewall rule, recreating')
            create_health_check_firewall_rule(gcp, firewall_name)
        backend_service = get_backend_service(gcp, backend_service_name)
        alternate_backend_service = get_backend_service(
            gcp, alternate_backend_service_name)
        get_url_map(gcp, url_map_name)
        get_target_proxy(gcp, target_proxy_name)
        get_global_forwarding_rule(gcp, forwarding_rule_name)
        get_instance_template(gcp, template_name)
        instance_group = get_instance_group(gcp, args.zone, instance_group_name)
        same_zone_instance_group = get_instance_group(
            gcp, args.zone, same_zone_instance_group_name)
        secondary_zone_instance_group = get_instance_group(
            gcp, args.secondary_zone, secondary_zone_instance_group_name)
    else:
        create_health_check_firewall_rule(gcp, firewall_name)
        backend_service = add_backend_service(gcp, backend_service_name)
        alternate_backend_service = add_backend_service(
            gcp, alternate_backend_service_name)
        create_url_map(gcp, url_map_name, backend_service, service_host_name)
        create_target_proxy(gcp, target_proxy_name)
        potential_service_ports = list(args.service_port_range)
        random.shuffle(potential_service_ports)
        create_global_forwarding_rule(gcp, forwarding_rule_name,
                                      potential_service_ports)
        if not gcp.service_port:
            raise Exception(
                'Failed to find a valid ip:port for the forwarding rule')
        if gcp.service_port != _DEFAULT_SERVICE_PORT:
            patch_url_map_host_rule_with_port(gcp, url_map_name,
                                              backend_service,
                                              service_host_name)
        startup_script = get_startup_script(args.path_to_server_binary,
                                            gcp.service_port)
        create_instance_template(gcp, template_name, args.network,
                                 args.source_image, args.machine_type,
                                 startup_script)
        instance_group = add_instance_group(gcp, args.zone, instance_group_name,
                                            _INSTANCE_GROUP_SIZE)
        patch_backend_instances(gcp, backend_service, [instance_group])
        same_zone_instance_group = add_instance_group(
            gcp, args.zone, same_zone_instance_group_name, _INSTANCE_GROUP_SIZE)
        secondary_zone_instance_group = add_instance_group(
            gcp, args.secondary_zone, secondary_zone_instance_group_name,
            _INSTANCE_GROUP_SIZE)

    wait_for_healthy_backends(gcp, backend_service, instance_group)

    if args.test_case:
        client_env = dict(os.environ)
        bootstrap_server_features = []
        if gcp.service_port == _DEFAULT_SERVICE_PORT:
            server_uri = service_host_name
        else:
            server_uri = service_host_name + ':' + str(gcp.service_port)
        if args.xds_v3_support:
            client_env['GRPC_XDS_EXPERIMENTAL_V3_SUPPORT'] = 'true'
            bootstrap_server_features.append('xds_v3')
        if args.bootstrap_file:
            bootstrap_path = os.path.abspath(args.bootstrap_file)
        else:
            with tempfile.NamedTemporaryFile(delete=False) as bootstrap_file:
                bootstrap_file.write(
                    _BOOTSTRAP_TEMPLATE.format(
                        node_id=socket.gethostname(),
                        server_features=json.dumps(
                            bootstrap_server_features)).encode('utf-8'))
                bootstrap_path = bootstrap_file.name
        client_env['GRPC_XDS_BOOTSTRAP'] = bootstrap_path
        test_results = {}
        failed_tests = []
        for test_case in args.test_case:
            result = jobset.JobResult()
            log_dir = os.path.join(_TEST_LOG_BASE_DIR, test_case)
            if not os.path.exists(log_dir):
                os.makedirs(log_dir)
            test_log_filename = os.path.join(log_dir, _SPONGE_LOG_NAME)
            test_log_file = open(test_log_filename, 'w+')
            client_process = None

            if test_case in _TESTS_TO_RUN_MULTIPLE_RPCS:
                rpcs_to_send = '--rpc="UnaryCall,EmptyCall"'
            else:
                rpcs_to_send = '--rpc="UnaryCall"'

            if test_case in _TESTS_TO_SEND_METADATA:
                metadata_to_send = '--metadata="EmptyCall:{key}:{value}"'.format(
                    key=_TEST_METADATA_KEY, value=_TEST_METADATA_VALUE)
            else:
                # Setting the arg explicitly to empty with '--metadata=""'
                # makes C# client fail
                # (see https://github.com/commandlineparser/commandline/issues/412),
                # so instead we just rely on clients using the default when
                # metadata arg is not specified.
                metadata_to_send = ''

            # TODO(ericgribkoff) Temporarily disable fail_on_failed_rpc checks
            # in the client. This means we will ignore intermittent RPC
            # failures (but this framework still checks that the final result
            # is as expected).
            #
            # Reason for disabling this is, the resources are shared by
            # multiple tests, and a change in previous test could be delayed
            # until the second test starts. The second test may see
            # intermittent failures because of that.
            #
            # A fix is to not share resources between tests (though that does
            # mean the tests will be significantly slower due to creating new
            # resources).
            fail_on_failed_rpc = ''

            client_cmd_formatted = args.client_cmd.format(
                server_uri=server_uri,
                stats_port=args.stats_port,
                qps=args.qps,
                fail_on_failed_rpc=fail_on_failed_rpc,
                rpcs_to_send=rpcs_to_send,
                metadata_to_send=metadata_to_send)
            logger.debug('running client: %s', client_cmd_formatted)
            client_cmd = shlex.split(client_cmd_formatted)
            try:
                client_process = subprocess.Popen(client_cmd,
                                                  env=client_env,
                                                  stderr=subprocess.STDOUT,
                                                  stdout=test_log_file)
                if test_case == 'backends_restart':
                    test_backends_restart(gcp, backend_service, instance_group)
                elif test_case == 'change_backend_service':
                    test_change_backend_service(gcp, backend_service,
                                                instance_group,
                                                alternate_backend_service,
                                                same_zone_instance_group)
                elif test_case == 'gentle_failover':
                    test_gentle_failover(gcp, backend_service, instance_group,
                                         secondary_zone_instance_group)
                elif test_case == 'ping_pong':
                    test_ping_pong(gcp, backend_service, instance_group)
                elif test_case == 'remove_instance_group':
                    test_remove_instance_group(gcp, backend_service,
                                               instance_group,
                                               same_zone_instance_group)
                elif test_case == 'round_robin':
                    test_round_robin(gcp, backend_service, instance_group)
                elif test_case == 'secondary_locality_gets_no_requests_on_partial_primary_failure':
                    test_secondary_locality_gets_no_requests_on_partial_primary_failure(
                        gcp, backend_service, instance_group,
                        secondary_zone_instance_group)
                elif test_case == 'secondary_locality_gets_requests_on_primary_failure':
                    test_secondary_locality_gets_requests_on_primary_failure(
                        gcp, backend_service, instance_group,
                        secondary_zone_instance_group)
                elif test_case == 'traffic_splitting':
                    test_traffic_splitting(gcp, backend_service, instance_group,
                                           alternate_backend_service,
                                           same_zone_instance_group)
                elif test_case == 'path_matching':
                    test_path_matching(gcp, backend_service, instance_group,
                                       alternate_backend_service,
                                       same_zone_instance_group)
                elif test_case == 'header_matching':
                    test_header_matching(gcp, backend_service, instance_group,
                                         alternate_backend_service,
                                         same_zone_instance_group)
                else:
                    logger.error('Unknown test case: %s', test_case)
                    sys.exit(1)
                if client_process.poll() is not None:
                    raise Exception(
                        'Client process exited prematurely with exit code %d' %
                        client_process.returncode)
                result.state = 'PASSED'
                result.returncode = 0
            except Exception as e:
                logger.exception('Test case %s failed', test_case)
                failed_tests.append(test_case)
                result.state = 'FAILED'
                result.message = str(e)
            finally:
                if client_process and not client_process.returncode:
                    client_process.terminate()
                test_log_file.close()
                # Workaround for Python 3, as report_utils will invoke decode() on
                # result.message, which has a default value of ''.
                result.message = result.message.encode('UTF-8')
                test_results[test_case] = [result]
                if args.log_client_output:
                    logger.info('Client output:')
                    with open(test_log_filename, 'r') as client_output:
                        logger.info(client_output.read())
        if not os.path.exists(_TEST_LOG_BASE_DIR):
            os.makedirs(_TEST_LOG_BASE_DIR)
        report_utils.render_junit_xml_report(test_results,
                                             os.path.join(
                                                 _TEST_LOG_BASE_DIR,
                                                 _SPONGE_XML_NAME),
                                             suite_name='xds_tests',
                                             multi_target=True)
        if failed_tests:
            logger.error('Test case(s) %s failed', failed_tests)
            sys.exit(1)
finally:
    if not args.keep_gcp_resources:
        logger.info('Cleaning up GCP resources. This may take some time.')
        clean_up(gcp)

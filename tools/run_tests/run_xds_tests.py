#!/usr/bin/env python3
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
import datetime
import json
import logging
import os
import random
import re
import shlex
import socket
import subprocess
import sys
import tempfile
import time
import uuid

from google.protobuf import json_format
import googleapiclient.discovery
import grpc
from oauth2client.client import GoogleCredentials

import python_utils.jobset as jobset
import python_utils.report_utils as report_utils
from src.proto.grpc.health.v1 import health_pb2
from src.proto.grpc.health.v1 import health_pb2_grpc
from src.proto.grpc.testing import empty_pb2
from src.proto.grpc.testing import messages_pb2
from src.proto.grpc.testing import test_pb2_grpc

# Envoy protos provided by PyPI package xds-protos
# Needs to import the generated Python file to load descriptors
try:
    from envoy.extensions.filters.common.fault.v3 import fault_pb2
    from envoy.extensions.filters.http.fault.v3 import fault_pb2
    from envoy.extensions.filters.http.router.v3 import router_pb2
    from envoy.extensions.filters.network.http_connection_manager.v3 import (
        http_connection_manager_pb2,
    )
    from envoy.service.status.v3 import csds_pb2
    from envoy.service.status.v3 import csds_pb2_grpc
except ImportError:
    # These protos are required by CSDS test. We should not fail the entire
    # script for one test case.
    pass

logger = logging.getLogger()
console_handler = logging.StreamHandler()
formatter = logging.Formatter(fmt="%(asctime)s: %(levelname)-8s %(message)s")
console_handler.setFormatter(formatter)
logger.handlers = []
logger.addHandler(console_handler)
logger.setLevel(logging.WARNING)

# Suppress excessive logs for gRPC Python
original_grpc_trace = os.environ.pop("GRPC_TRACE", None)
original_grpc_verbosity = os.environ.pop("GRPC_VERBOSITY", None)
# Suppress not-essential logs for GCP clients
logging.getLogger("google_auth_httplib2").setLevel(logging.WARNING)
logging.getLogger("googleapiclient.discovery").setLevel(logging.WARNING)

_TEST_CASES = [
    "backends_restart",
    "change_backend_service",
    "gentle_failover",
    "load_report_based_failover",
    "ping_pong",
    "remove_instance_group",
    "round_robin",
    "secondary_locality_gets_no_requests_on_partial_primary_failure",
    "secondary_locality_gets_requests_on_primary_failure",
    "traffic_splitting",
    "path_matching",
    "header_matching",
    "api_listener",
    "forwarding_rule_port_match",
    "forwarding_rule_default_port",
    "metadata_filter",
]

# Valid test cases, but not in all. So the tests can only run manually, and
# aren't enabled automatically for all languages.
#
# TODO: Move them into _TEST_CASES when support is ready in all languages.
_ADDITIONAL_TEST_CASES = [
    "circuit_breaking",
    "timeout",
    "fault_injection",
    "csds",
]

# Test cases that require the V3 API.  Skipped in older runs.
_V3_TEST_CASES = frozenset(["timeout", "fault_injection", "csds"])

# Test cases that require the alpha API.  Skipped for stable API runs.
_ALPHA_TEST_CASES = frozenset(["timeout"])


def parse_test_cases(arg):
    if arg == "":
        return []
    arg_split = arg.split(",")
    test_cases = set()
    all_test_cases = _TEST_CASES + _ADDITIONAL_TEST_CASES
    for arg in arg_split:
        if arg == "all":
            test_cases = test_cases.union(_TEST_CASES)
        else:
            test_cases = test_cases.union([arg])
    if not all([test_case in all_test_cases for test_case in test_cases]):
        raise Exception("Failed to parse test cases %s" % arg)
    # Perserve order.
    return [x for x in all_test_cases if x in test_cases]


def parse_port_range(port_arg):
    try:
        port = int(port_arg)
        return list(range(port, port + 1))
    except:
        port_min, port_max = port_arg.split(":")
        return list(range(int(port_min), int(port_max) + 1))


argp = argparse.ArgumentParser(description="Run xDS interop tests on GCP")
# TODO(zdapeng): remove default value of project_id and project_num
argp.add_argument("--project_id", default="grpc-testing", help="GCP project id")
argp.add_argument(
    "--project_num", default="830293263384", help="GCP project number"
)
argp.add_argument(
    "--gcp_suffix",
    default="",
    help=(
        "Optional suffix for all generated GCP resource names. Useful to "
        "ensure distinct names across test runs."
    ),
)
argp.add_argument(
    "--test_case",
    default="ping_pong",
    type=parse_test_cases,
    help=(
        "Comma-separated list of test cases to run. Available tests: %s, "
        "(or 'all' to run every test). "
        "Alternative tests not included in 'all': %s"
    )
    % (",".join(_TEST_CASES), ",".join(_ADDITIONAL_TEST_CASES)),
)
argp.add_argument(
    "--bootstrap_file",
    default="",
    help=(
        "File to reference via GRPC_XDS_BOOTSTRAP. Disables built-in "
        "bootstrap generation"
    ),
)
argp.add_argument(
    "--xds_v3_support",
    default=False,
    action="store_true",
    help=(
        "Support xDS v3 via GRPC_XDS_EXPERIMENTAL_V3_SUPPORT. "
        "If a pre-created bootstrap file is provided via the --bootstrap_file "
        "parameter, it should include xds_v3 in its server_features field."
    ),
)
argp.add_argument(
    "--client_cmd",
    default=None,
    help=(
        "Command to launch xDS test client. {server_uri}, {stats_port} and"
        " {qps} references will be replaced using str.format()."
        " GRPC_XDS_BOOTSTRAP will be set for the command"
    ),
)
argp.add_argument(
    "--client_hosts",
    default=None,
    help=(
        "Comma-separated list of hosts running client processes. If set,"
        " --client_cmd is ignored and client processes are assumed to be"
        " running on the specified hosts."
    ),
)
argp.add_argument("--zone", default="us-central1-a")
argp.add_argument(
    "--secondary_zone",
    default="us-west1-b",
    help="Zone to use for secondary TD locality tests",
)
argp.add_argument("--qps", default=100, type=int, help="Client QPS")
argp.add_argument(
    "--wait_for_backend_sec",
    default=1200,
    type=int,
    help=(
        "Time limit for waiting for created backend services to report "
        "healthy when launching or updated GCP resources"
    ),
)
argp.add_argument(
    "--use_existing_gcp_resources",
    default=False,
    action="store_true",
    help=(
        "If set, find and use already created GCP resources instead of creating"
        " new ones."
    ),
)
argp.add_argument(
    "--keep_gcp_resources",
    default=False,
    action="store_true",
    help=(
        "Leave GCP VMs and configuration running after test. Default behavior"
        " is to delete when tests complete."
    ),
)
argp.add_argument(
    "--halt_after_fail",
    action="store_true",
    help="Halt and save the resources when test failed.",
)
argp.add_argument(
    "--compute_discovery_document",
    default=None,
    type=str,
    help=(
        "If provided, uses this file instead of retrieving via the GCP"
        " discovery API"
    ),
)
argp.add_argument(
    "--alpha_compute_discovery_document",
    default=None,
    type=str,
    help=(
        "If provided, uses this file instead of retrieving via the alpha GCP "
        "discovery API"
    ),
)
argp.add_argument(
    "--network", default="global/networks/default", help="GCP network to use"
)
_DEFAULT_PORT_RANGE = "8080:8280"
argp.add_argument(
    "--service_port_range",
    default=_DEFAULT_PORT_RANGE,
    type=parse_port_range,
    help=(
        "Listening port for created gRPC backends. Specified as "
        "either a single int or as a range in the format min:max, in "
        "which case an available port p will be chosen s.t. min <= p "
        "<= max"
    ),
)
argp.add_argument(
    "--stats_port",
    default=8079,
    type=int,
    help="Local port for the client process to expose the LB stats service",
)
argp.add_argument(
    "--xds_server",
    default="trafficdirector.googleapis.com:443",
    help="xDS server",
)
argp.add_argument(
    "--source_image",
    default="projects/debian-cloud/global/images/family/debian-9",
    help="Source image for VMs created during the test",
)
argp.add_argument(
    "--path_to_server_binary",
    default=None,
    type=str,
    help=(
        "If set, the server binary must already be pre-built on "
        "the specified source image"
    ),
)
argp.add_argument(
    "--machine_type",
    default="e2-standard-2",
    help="Machine type for VMs created during the test",
)
argp.add_argument(
    "--instance_group_size",
    default=2,
    type=int,
    help=(
        "Number of VMs to create per instance group. Certain test cases (e.g.,"
        " round_robin) may not give meaningful results if this is set to a"
        " value less than 2."
    ),
)
argp.add_argument(
    "--verbose", help="verbose log output", default=False, action="store_true"
)
# TODO(ericgribkoff) Remove this param once the sponge-formatted log files are
# visible in all test environments.
argp.add_argument(
    "--log_client_output",
    help="Log captured client output",
    default=False,
    action="store_true",
)
# TODO(ericgribkoff) Remove this flag once all test environments are verified to
# have access to the alpha compute APIs.
argp.add_argument(
    "--only_stable_gcp_apis",
    help=(
        "Do not use alpha compute APIs. Some tests may be "
        "incompatible with this option (gRPC health checks are "
        "currently alpha and required for simulating server failure"
    ),
    default=False,
    action="store_true",
)
args = argp.parse_args()

if args.verbose:
    logger.setLevel(logging.DEBUG)

# In grpc-testing, use non-legacy network.
if (
    args.project_id == "grpc-testing"
    and args.network
    and args.network == argp.get_default("network")
):
    args.network += "-vpc"

CLIENT_HOSTS = []
if args.client_hosts:
    CLIENT_HOSTS = args.client_hosts.split(",")

# Each of the config propagation in the control plane should finish within 600s.
# Otherwise, it indicates a bug in the control plane. The config propagation
# includes all kinds of traffic config update, like updating urlMap, creating
# the resources for the first time, updating BackendService, and changing the
# status of endpoints in BackendService.
_WAIT_FOR_URL_MAP_PATCH_SEC = 600
# In general, fetching load balancing stats only takes ~10s. However, slow
# config update could lead to empty EDS or similar symptoms causing the
# connection to hang for a long period of time. So, we want to extend the stats
# wait time to be the same as urlMap patch time.
_WAIT_FOR_STATS_SEC = _WAIT_FOR_URL_MAP_PATCH_SEC

_DEFAULT_SERVICE_PORT = 80
_WAIT_FOR_BACKEND_SEC = args.wait_for_backend_sec
_WAIT_FOR_OPERATION_SEC = 1200
_INSTANCE_GROUP_SIZE = args.instance_group_size
_NUM_TEST_RPCS = 10 * args.qps
_CONNECTION_TIMEOUT_SEC = 60
_GCP_API_RETRIES = 5
_BOOTSTRAP_TEMPLATE = """
{{
  "node": {{
    "id": "{node_id}",
    "metadata": {{
      "TRAFFICDIRECTOR_NETWORK_NAME": "%s",
      "com.googleapis.trafficdirector.config_time_trace": "TRUE"
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
}}""" % (
    args.network.split("/")[-1],
    args.zone,
    args.xds_server,
)

# TODO(ericgribkoff) Add change_backend_service to this list once TD no longer
# sends an update with no localities when adding the MIG to the backend service
# can race with the URL map patch.
_TESTS_TO_FAIL_ON_RPC_FAILURE = ["ping_pong", "round_robin"]
# Tests that run UnaryCall and EmptyCall.
_TESTS_TO_RUN_MULTIPLE_RPCS = ["path_matching", "header_matching"]
# Tests that make UnaryCall with test metadata.
_TESTS_TO_SEND_METADATA = ["header_matching"]
_TEST_METADATA_KEY = "xds_md"
_TEST_METADATA_VALUE_UNARY = "unary_yranu"
_TEST_METADATA_VALUE_EMPTY = "empty_ytpme"
# Extra RPC metadata whose value is a number, sent with UnaryCall only.
_TEST_METADATA_NUMERIC_KEY = "xds_md_numeric"
_TEST_METADATA_NUMERIC_VALUE = "159"
_PATH_MATCHER_NAME = "path-matcher"
_BASE_TEMPLATE_NAME = "test-template"
_BASE_INSTANCE_GROUP_NAME = "test-ig"
_BASE_HEALTH_CHECK_NAME = "test-hc"
_BASE_FIREWALL_RULE_NAME = "test-fw-rule"
_BASE_BACKEND_SERVICE_NAME = "test-backend-service"
_BASE_URL_MAP_NAME = "test-map"
_BASE_SERVICE_HOST = "grpc-test"
_BASE_TARGET_PROXY_NAME = "test-target-proxy"
_BASE_FORWARDING_RULE_NAME = "test-forwarding-rule"
_TEST_LOG_BASE_DIR = os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "../../reports"
)
_SPONGE_LOG_NAME = "sponge_log.log"
_SPONGE_XML_NAME = "sponge_log.xml"


def get_client_stats(num_rpcs, timeout_sec, metadata):
    if CLIENT_HOSTS:
        hosts = CLIENT_HOSTS
    else:
        hosts = ["localhost"]
    for host in hosts:
        with grpc.insecure_channel(
            "%s:%d" % (host, args.stats_port)
        ) as channel:
            stub = test_pb2_grpc.LoadBalancerStatsServiceStub(channel)
            request = messages_pb2.LoadBalancerStatsRequest()
            request.num_rpcs = num_rpcs
            request.timeout_sec = timeout_sec
            request.metadata = metadata
            rpc_timeout = timeout_sec + _CONNECTION_TIMEOUT_SEC
            logger.debug(
                "Invoking GetClientStats RPC to %s:%d:", host, args.stats_port
            )
            response = stub.GetClientStats(
                request, wait_for_ready=True, timeout=rpc_timeout
            )
            logger.debug(
                "Invoked GetClientStats RPC to %s: %s",
                host,
                json_format.MessageToJson(response),
            )
            return response


def get_client_accumulated_stats():
    if CLIENT_HOSTS:
        hosts = CLIENT_HOSTS
    else:
        hosts = ["localhost"]
    for host in hosts:
        with grpc.insecure_channel(
            "%s:%d" % (host, args.stats_port)
        ) as channel:
            stub = test_pb2_grpc.LoadBalancerStatsServiceStub(channel)
            request = messages_pb2.LoadBalancerAccumulatedStatsRequest()
            logger.debug(
                "Invoking GetClientAccumulatedStats RPC to %s:%d:",
                host,
                args.stats_port,
            )
            response = stub.GetClientAccumulatedStats(
                request, wait_for_ready=True, timeout=_CONNECTION_TIMEOUT_SEC
            )
            logger.debug(
                "Invoked GetClientAccumulatedStats RPC to %s: %s",
                host,
                response,
            )
            return response


def get_client_xds_config_dump():
    if CLIENT_HOSTS:
        hosts = CLIENT_HOSTS
    else:
        hosts = ["localhost"]
    for host in hosts:
        server_address = "%s:%d" % (host, args.stats_port)
        with grpc.insecure_channel(server_address) as channel:
            stub = csds_pb2_grpc.ClientStatusDiscoveryServiceStub(channel)
            logger.debug("Fetching xDS config dump from %s", server_address)
            response = stub.FetchClientStatus(
                csds_pb2.ClientStatusRequest(),
                wait_for_ready=True,
                timeout=_CONNECTION_TIMEOUT_SEC,
            )
            logger.debug("Fetched xDS config dump from %s", server_address)
            if len(response.config) != 1:
                logger.error(
                    "Unexpected number of ClientConfigs %d: %s",
                    len(response.config),
                    response,
                )
                return None
            else:
                # Converting the ClientStatusResponse into JSON, because many
                # fields are packed in google.protobuf.Any. It will require many
                # duplicated code to unpack proto message and inspect values.
                return json_format.MessageToDict(
                    response.config[0], preserving_proto_field_name=True
                )


def configure_client(rpc_types, metadata=[], timeout_sec=None):
    if CLIENT_HOSTS:
        hosts = CLIENT_HOSTS
    else:
        hosts = ["localhost"]
    for host in hosts:
        with grpc.insecure_channel(
            "%s:%d" % (host, args.stats_port)
        ) as channel:
            stub = test_pb2_grpc.XdsUpdateClientConfigureServiceStub(channel)
            request = messages_pb2.ClientConfigureRequest()
            request.types.extend(rpc_types)
            for rpc_type, md_key, md_value in metadata:
                md = request.metadata.add()
                md.type = rpc_type
                md.key = md_key
                md.value = md_value
            if timeout_sec:
                request.timeout_sec = timeout_sec
            logger.debug(
                "Invoking XdsUpdateClientConfigureService RPC to %s:%d: %s",
                host,
                args.stats_port,
                request,
            )
            stub.Configure(
                request, wait_for_ready=True, timeout=_CONNECTION_TIMEOUT_SEC
            )
            logger.debug(
                "Invoked XdsUpdateClientConfigureService RPC to %s", host
            )


class RpcDistributionError(Exception):
    pass


def _verify_rpcs_to_given_backends(
    backends, timeout_sec, num_rpcs, allow_failures
):
    start_time = time.time()
    error_msg = None
    logger.debug(
        "Waiting for %d sec until backends %s receive load"
        % (timeout_sec, backends)
    )
    while time.time() - start_time <= timeout_sec:
        error_msg = None
        stats = get_client_stats(num_rpcs, timeout_sec)
        rpcs_by_peer = stats.rpcs_by_peer
        for backend in backends:
            if backend not in rpcs_by_peer:
                error_msg = "Backend %s did not receive load" % backend
                break
        if not error_msg and len(rpcs_by_peer) > len(backends):
            error_msg = "Unexpected backend received load: %s" % rpcs_by_peer
        if not allow_failures and stats.num_failures > 0:
            error_msg = "%d RPCs failed" % stats.num_failures
        if not error_msg:
            return
    raise RpcDistributionError(error_msg)


def wait_until_all_rpcs_go_to_given_backends_or_fail(
    backends, timeout_sec, num_rpcs=_NUM_TEST_RPCS
):
    _verify_rpcs_to_given_backends(
        backends, timeout_sec, num_rpcs, allow_failures=True
    )


def wait_until_all_rpcs_go_to_given_backends(
    backends, timeout_sec, num_rpcs=_NUM_TEST_RPCS
):
    _verify_rpcs_to_given_backends(
        backends, timeout_sec, num_rpcs, allow_failures=False
    )


def wait_until_no_rpcs_go_to_given_backends(backends, timeout_sec):
    start_time = time.time()
    while time.time() - start_time <= timeout_sec:
        stats = get_client_stats(_NUM_TEST_RPCS, timeout_sec)
        error_msg = None
        rpcs_by_peer = stats.rpcs_by_peer
        for backend in backends:
            if backend in rpcs_by_peer:
                error_msg = "Unexpected backend %s receives load" % backend
                break
        if not error_msg:
            return
    raise Exception("Unexpected RPCs going to given backends")


def wait_until_rpcs_in_flight(rpc_type, timeout_sec, num_rpcs, threshold):
    """Block until the test client reaches the state with the given number
    of RPCs being outstanding stably.

    Args:
      rpc_type: A string indicating the RPC method to check for. Either
        'UnaryCall' or 'EmptyCall'.
      timeout_sec: Maximum number of seconds to wait until the desired state
        is reached.
      num_rpcs: Expected number of RPCs to be in-flight.
      threshold: Number within [0,100], the tolerable percentage by which
        the actual number of RPCs in-flight can differ from the expected number.
    """
    if threshold < 0 or threshold > 100:
        raise ValueError("Value error: Threshold should be between 0 to 100")
    threshold_fraction = threshold / 100.0
    start_time = time.time()
    error_msg = None
    logger.debug(
        "Waiting for %d sec until %d %s RPCs (with %d%% tolerance) in-flight"
        % (timeout_sec, num_rpcs, rpc_type, threshold)
    )
    while time.time() - start_time <= timeout_sec:
        error_msg = _check_rpcs_in_flight(
            rpc_type, num_rpcs, threshold, threshold_fraction
        )
        if error_msg:
            logger.debug("Progress: %s", error_msg)
            time.sleep(2)
        else:
            break
    # Ensure the number of outstanding RPCs is stable.
    if not error_msg:
        time.sleep(5)
        error_msg = _check_rpcs_in_flight(
            rpc_type, num_rpcs, threshold, threshold_fraction
        )
    if error_msg:
        raise Exception(
            "Wrong number of %s RPCs in-flight: %s" % (rpc_type, error_msg)
        )


def _check_rpcs_in_flight(rpc_type, num_rpcs, threshold, threshold_fraction):
    error_msg = None
    stats = get_client_accumulated_stats()
    rpcs_started = stats.num_rpcs_started_by_method[rpc_type]
    rpcs_succeeded = stats.num_rpcs_succeeded_by_method[rpc_type]
    rpcs_failed = stats.num_rpcs_failed_by_method[rpc_type]
    rpcs_in_flight = rpcs_started - rpcs_succeeded - rpcs_failed
    if rpcs_in_flight < (num_rpcs * (1 - threshold_fraction)):
        error_msg = "actual(%d) < expected(%d - %d%%)" % (
            rpcs_in_flight,
            num_rpcs,
            threshold,
        )
    elif rpcs_in_flight > (num_rpcs * (1 + threshold_fraction)):
        error_msg = "actual(%d) > expected(%d + %d%%)" % (
            rpcs_in_flight,
            num_rpcs,
            threshold,
        )
    return error_msg


def compare_distributions(
    actual_distribution, expected_distribution, threshold
):
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
            "Error: expected and actual distributions have different size (%d"
            " vs %d)" % (len(expected_distribution), len(actual_distribution))
        )
    if threshold < 0 or threshold > 100:
        raise ValueError("Value error: Threshold should be between 0 to 100")
    threshold_fraction = threshold / 100.0
    for expected, actual in zip(expected_distribution, actual_distribution):
        if actual < (expected * (1 - threshold_fraction)):
            raise Exception(
                "actual(%f) < expected(%f-%d%%)" % (actual, expected, threshold)
            )
        if actual > (expected * (1 + threshold_fraction)):
            raise Exception(
                "actual(%f) > expected(%f+%d%%)" % (actual, expected, threshold)
            )
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
    for rpc_type, expected_peers in list(expected_instances.items()):
        rpcs_by_peer_for_type = stats.rpcs_by_method[rpc_type]
        rpcs_by_peer = (
            rpcs_by_peer_for_type.rpcs_by_peer
            if rpcs_by_peer_for_type
            else None
        )
        logger.debug("rpc: %s, by_peer: %s", rpc_type, rpcs_by_peer)
        peers = list(rpcs_by_peer.keys())
        if set(peers) != set(expected_peers):
            logger.info(
                "unexpected peers for %s, got %s, want %s",
                rpc_type,
                peers,
                expected_peers,
            )
            return False
    return True


def test_backends_restart(gcp, backend_service, instance_group):
    logger.info("Running test_backends_restart")
    instance_names = get_instance_names(gcp, instance_group)
    num_instances = len(instance_names)
    start_time = time.time()
    wait_until_all_rpcs_go_to_given_backends(
        instance_names, _WAIT_FOR_STATS_SEC
    )
    try:
        resize_instance_group(gcp, instance_group, 0)
        wait_until_all_rpcs_go_to_given_backends_or_fail(
            [], _WAIT_FOR_BACKEND_SEC
        )
    finally:
        resize_instance_group(gcp, instance_group, num_instances)
    wait_for_healthy_backends(gcp, backend_service, instance_group)
    new_instance_names = get_instance_names(gcp, instance_group)
    wait_until_all_rpcs_go_to_given_backends(
        new_instance_names, _WAIT_FOR_BACKEND_SEC
    )


def test_change_backend_service(
    gcp,
    original_backend_service,
    instance_group,
    alternate_backend_service,
    same_zone_instance_group,
):
    logger.info("Running test_change_backend_service")
    original_backend_instances = get_instance_names(gcp, instance_group)
    alternate_backend_instances = get_instance_names(
        gcp, same_zone_instance_group
    )
    patch_backend_service(
        gcp, alternate_backend_service, [same_zone_instance_group]
    )
    wait_for_healthy_backends(gcp, original_backend_service, instance_group)
    wait_for_healthy_backends(
        gcp, alternate_backend_service, same_zone_instance_group
    )
    wait_until_all_rpcs_go_to_given_backends(
        original_backend_instances, _WAIT_FOR_STATS_SEC
    )
    passed = True
    try:
        patch_url_map_backend_service(gcp, alternate_backend_service)
        wait_until_all_rpcs_go_to_given_backends(
            alternate_backend_instances, _WAIT_FOR_URL_MAP_PATCH_SEC
        )
    except Exception:
        passed = False
        raise
    finally:
        if passed or not args.halt_after_fail:
            patch_url_map_backend_service(gcp, original_backend_service)
            patch_backend_service(gcp, alternate_backend_service, [])


def test_gentle_failover(
    gcp,
    backend_service,
    primary_instance_group,
    secondary_instance_group,
    swapped_primary_and_secondary=False,
):
    logger.info("Running test_gentle_failover")
    num_primary_instances = len(get_instance_names(gcp, primary_instance_group))
    min_instances_for_gentle_failover = 3  # Need >50% failure to start failover
    passed = True
    try:
        if num_primary_instances < min_instances_for_gentle_failover:
            resize_instance_group(
                gcp, primary_instance_group, min_instances_for_gentle_failover
            )
        patch_backend_service(
            gcp,
            backend_service,
            [primary_instance_group, secondary_instance_group],
        )
        primary_instance_names = get_instance_names(gcp, primary_instance_group)
        secondary_instance_names = get_instance_names(
            gcp, secondary_instance_group
        )
        wait_for_healthy_backends(gcp, backend_service, primary_instance_group)
        wait_for_healthy_backends(
            gcp, backend_service, secondary_instance_group
        )
        wait_until_all_rpcs_go_to_given_backends(
            primary_instance_names, _WAIT_FOR_STATS_SEC
        )
        instances_to_stop = primary_instance_names[:-1]
        remaining_instances = primary_instance_names[-1:]
        try:
            set_serving_status(
                instances_to_stop, gcp.service_port, serving=False
            )
            wait_until_all_rpcs_go_to_given_backends(
                remaining_instances + secondary_instance_names,
                _WAIT_FOR_BACKEND_SEC,
            )
        finally:
            set_serving_status(
                primary_instance_names, gcp.service_port, serving=True
            )
    except RpcDistributionError as e:
        if not swapped_primary_and_secondary and is_primary_instance_group(
            gcp, secondary_instance_group
        ):
            # Swap expectation of primary and secondary instance groups.
            test_gentle_failover(
                gcp,
                backend_service,
                secondary_instance_group,
                primary_instance_group,
                swapped_primary_and_secondary=True,
            )
        else:
            passed = False
            raise e
    except Exception:
        passed = False
        raise
    finally:
        if passed or not args.halt_after_fail:
            patch_backend_service(
                gcp, backend_service, [primary_instance_group]
            )
            resize_instance_group(
                gcp, primary_instance_group, num_primary_instances
            )
            instance_names = get_instance_names(gcp, primary_instance_group)
            wait_until_all_rpcs_go_to_given_backends(
                instance_names, _WAIT_FOR_BACKEND_SEC
            )


def test_load_report_based_failover(
    gcp, backend_service, primary_instance_group, secondary_instance_group
):
    logger.info("Running test_load_report_based_failover")
    passed = True
    try:
        patch_backend_service(
            gcp,
            backend_service,
            [primary_instance_group, secondary_instance_group],
        )
        primary_instance_names = get_instance_names(gcp, primary_instance_group)
        secondary_instance_names = get_instance_names(
            gcp, secondary_instance_group
        )
        wait_for_healthy_backends(gcp, backend_service, primary_instance_group)
        wait_for_healthy_backends(
            gcp, backend_service, secondary_instance_group
        )
        wait_until_all_rpcs_go_to_given_backends(
            primary_instance_names, _WAIT_FOR_STATS_SEC
        )
        # Set primary locality's balance mode to RATE, and RPS to 20% of the
        # client's QPS. The secondary locality will be used.
        max_rate = int(args.qps * 1 / 5)
        logger.info(
            "Patching backend service to RATE with %d max_rate", max_rate
        )
        patch_backend_service(
            gcp,
            backend_service,
            [primary_instance_group, secondary_instance_group],
            balancing_mode="RATE",
            max_rate=max_rate,
        )
        wait_until_all_rpcs_go_to_given_backends(
            primary_instance_names + secondary_instance_names,
            _WAIT_FOR_BACKEND_SEC,
        )

        # Set primary locality's balance mode to RATE, and RPS to 120% of the
        # client's QPS. Only the primary locality will be used.
        max_rate = int(args.qps * 6 / 5)
        logger.info(
            "Patching backend service to RATE with %d max_rate", max_rate
        )
        patch_backend_service(
            gcp,
            backend_service,
            [primary_instance_group, secondary_instance_group],
            balancing_mode="RATE",
            max_rate=max_rate,
        )
        wait_until_all_rpcs_go_to_given_backends(
            primary_instance_names, _WAIT_FOR_BACKEND_SEC
        )
        logger.info("success")
    except Exception:
        passed = False
        raise
    finally:
        if passed or not args.halt_after_fail:
            patch_backend_service(
                gcp, backend_service, [primary_instance_group]
            )
            instance_names = get_instance_names(gcp, primary_instance_group)
            wait_until_all_rpcs_go_to_given_backends(
                instance_names, _WAIT_FOR_BACKEND_SEC
            )


def test_ping_pong(gcp, backend_service, instance_group):
    logger.info("Running test_ping_pong")
    wait_for_healthy_backends(gcp, backend_service, instance_group)
    instance_names = get_instance_names(gcp, instance_group)
    wait_until_all_rpcs_go_to_given_backends(
        instance_names, _WAIT_FOR_STATS_SEC
    )


def test_remove_instance_group(
    gcp, backend_service, instance_group, same_zone_instance_group
):
    logger.info("Running test_remove_instance_group")
    passed = True
    try:
        patch_backend_service(
            gcp,
            backend_service,
            [instance_group, same_zone_instance_group],
            balancing_mode="RATE",
        )
        wait_for_healthy_backends(gcp, backend_service, instance_group)
        wait_for_healthy_backends(
            gcp, backend_service, same_zone_instance_group
        )
        instance_names = get_instance_names(gcp, instance_group)
        same_zone_instance_names = get_instance_names(
            gcp, same_zone_instance_group
        )
        try:
            wait_until_all_rpcs_go_to_given_backends(
                instance_names + same_zone_instance_names,
                _WAIT_FOR_OPERATION_SEC,
            )
            remaining_instance_group = same_zone_instance_group
            remaining_instance_names = same_zone_instance_names
        except RpcDistributionError as e:
            # If connected to TD in a different zone, we may route traffic to
            # only one instance group. Determine which group that is to continue
            # with the remainder of the test case.
            try:
                wait_until_all_rpcs_go_to_given_backends(
                    instance_names, _WAIT_FOR_STATS_SEC
                )
                remaining_instance_group = same_zone_instance_group
                remaining_instance_names = same_zone_instance_names
            except RpcDistributionError as e:
                wait_until_all_rpcs_go_to_given_backends(
                    same_zone_instance_names, _WAIT_FOR_STATS_SEC
                )
                remaining_instance_group = instance_group
                remaining_instance_names = instance_names
        patch_backend_service(
            gcp,
            backend_service,
            [remaining_instance_group],
            balancing_mode="RATE",
        )
        wait_until_all_rpcs_go_to_given_backends(
            remaining_instance_names, _WAIT_FOR_BACKEND_SEC
        )
    except Exception:
        passed = False
        raise
    finally:
        if passed or not args.halt_after_fail:
            patch_backend_service(gcp, backend_service, [instance_group])
            wait_until_all_rpcs_go_to_given_backends(
                instance_names, _WAIT_FOR_BACKEND_SEC
            )


def test_round_robin(gcp, backend_service, instance_group):
    logger.info("Running test_round_robin")
    wait_for_healthy_backends(gcp, backend_service, instance_group)
    instance_names = get_instance_names(gcp, instance_group)
    threshold = 1
    wait_until_all_rpcs_go_to_given_backends(
        instance_names, _WAIT_FOR_STATS_SEC
    )
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
            logger.info("Unexpected RPC failures, retrying: %s", stats)
            continue
        expected_requests = total_requests_received / len(instance_names)
        for instance in instance_names:
            if (
                abs(stats.rpcs_by_peer[instance] - expected_requests)
                > threshold
            ):
                raise Exception(
                    "RPC peer distribution differs from expected by more than"
                    " %d for instance %s (%s)" % (threshold, instance, stats)
                )
        return
    raise Exception("RPC failures persisted through %d retries" % max_attempts)


def test_secondary_locality_gets_no_requests_on_partial_primary_failure(
    gcp,
    backend_service,
    primary_instance_group,
    secondary_instance_group,
    swapped_primary_and_secondary=False,
):
    logger.info(
        "Running secondary_locality_gets_no_requests_on_partial_primary_failure"
    )
    passed = True
    try:
        patch_backend_service(
            gcp,
            backend_service,
            [primary_instance_group, secondary_instance_group],
        )
        wait_for_healthy_backends(gcp, backend_service, primary_instance_group)
        wait_for_healthy_backends(
            gcp, backend_service, secondary_instance_group
        )
        primary_instance_names = get_instance_names(gcp, primary_instance_group)
        wait_until_all_rpcs_go_to_given_backends(
            primary_instance_names, _WAIT_FOR_STATS_SEC
        )
        instances_to_stop = primary_instance_names[:1]
        remaining_instances = primary_instance_names[1:]
        try:
            set_serving_status(
                instances_to_stop, gcp.service_port, serving=False
            )
            wait_until_all_rpcs_go_to_given_backends(
                remaining_instances, _WAIT_FOR_BACKEND_SEC
            )
        finally:
            set_serving_status(
                primary_instance_names, gcp.service_port, serving=True
            )
    except RpcDistributionError as e:
        if not swapped_primary_and_secondary and is_primary_instance_group(
            gcp, secondary_instance_group
        ):
            # Swap expectation of primary and secondary instance groups.
            test_secondary_locality_gets_no_requests_on_partial_primary_failure(
                gcp,
                backend_service,
                secondary_instance_group,
                primary_instance_group,
                swapped_primary_and_secondary=True,
            )
        else:
            passed = False
            raise e
    finally:
        if passed or not args.halt_after_fail:
            patch_backend_service(
                gcp, backend_service, [primary_instance_group]
            )


def test_secondary_locality_gets_requests_on_primary_failure(
    gcp,
    backend_service,
    primary_instance_group,
    secondary_instance_group,
    swapped_primary_and_secondary=False,
):
    logger.info("Running secondary_locality_gets_requests_on_primary_failure")
    passed = True
    try:
        patch_backend_service(
            gcp,
            backend_service,
            [primary_instance_group, secondary_instance_group],
        )
        wait_for_healthy_backends(gcp, backend_service, primary_instance_group)
        wait_for_healthy_backends(
            gcp, backend_service, secondary_instance_group
        )
        primary_instance_names = get_instance_names(gcp, primary_instance_group)
        secondary_instance_names = get_instance_names(
            gcp, secondary_instance_group
        )
        wait_until_all_rpcs_go_to_given_backends(
            primary_instance_names, _WAIT_FOR_STATS_SEC
        )
        try:
            set_serving_status(
                primary_instance_names, gcp.service_port, serving=False
            )
            wait_until_all_rpcs_go_to_given_backends(
                secondary_instance_names, _WAIT_FOR_BACKEND_SEC
            )
        finally:
            set_serving_status(
                primary_instance_names, gcp.service_port, serving=True
            )
    except RpcDistributionError as e:
        if not swapped_primary_and_secondary and is_primary_instance_group(
            gcp, secondary_instance_group
        ):
            # Swap expectation of primary and secondary instance groups.
            test_secondary_locality_gets_requests_on_primary_failure(
                gcp,
                backend_service,
                secondary_instance_group,
                primary_instance_group,
                swapped_primary_and_secondary=True,
            )
        else:
            passed = False
            raise e
    finally:
        if passed or not args.halt_after_fail:
            patch_backend_service(
                gcp, backend_service, [primary_instance_group]
            )


def prepare_services_for_urlmap_tests(
    gcp,
    original_backend_service,
    instance_group,
    alternate_backend_service,
    same_zone_instance_group,
):
    """
    This function prepares the services to be ready for tests that modifies
    urlmaps.

    Returns:
      Returns original and alternate backend names as lists of strings.
    """
    logger.info("waiting for original backends to become healthy")
    wait_for_healthy_backends(gcp, original_backend_service, instance_group)

    patch_backend_service(
        gcp, alternate_backend_service, [same_zone_instance_group]
    )
    logger.info("waiting for alternate to become healthy")
    wait_for_healthy_backends(
        gcp, alternate_backend_service, same_zone_instance_group
    )

    original_backend_instances = get_instance_names(gcp, instance_group)
    logger.info("original backends instances: %s", original_backend_instances)

    alternate_backend_instances = get_instance_names(
        gcp, same_zone_instance_group
    )
    logger.info("alternate backends instances: %s", alternate_backend_instances)

    # Start with all traffic going to original_backend_service.
    logger.info("waiting for traffic to all go to original backends")
    wait_until_all_rpcs_go_to_given_backends(
        original_backend_instances, _WAIT_FOR_STATS_SEC
    )
    return original_backend_instances, alternate_backend_instances


def test_metadata_filter(
    gcp,
    original_backend_service,
    instance_group,
    alternate_backend_service,
    same_zone_instance_group,
):
    logger.info("Running test_metadata_filter")
    wait_for_healthy_backends(gcp, original_backend_service, instance_group)
    original_backend_instances = get_instance_names(gcp, instance_group)
    alternate_backend_instances = get_instance_names(
        gcp, same_zone_instance_group
    )
    patch_backend_service(
        gcp, alternate_backend_service, [same_zone_instance_group]
    )
    wait_for_healthy_backends(
        gcp, alternate_backend_service, same_zone_instance_group
    )
    passed = True
    try:
        with open(bootstrap_path) as f:
            md = json.load(f)["node"]["metadata"]
            match_labels = []
            for k, v in list(md.items()):
                match_labels.append({"name": k, "value": v})

        not_match_labels = [{"name": "fake", "value": "fail"}]
        test_route_rules = [
            # test MATCH_ALL
            [
                {
                    "priority": 0,
                    "matchRules": [
                        {
                            "prefixMatch": "/",
                            "metadataFilters": [
                                {
                                    "filterMatchCriteria": "MATCH_ALL",
                                    "filterLabels": not_match_labels,
                                }
                            ],
                        }
                    ],
                    "service": original_backend_service.url,
                },
                {
                    "priority": 1,
                    "matchRules": [
                        {
                            "prefixMatch": "/",
                            "metadataFilters": [
                                {
                                    "filterMatchCriteria": "MATCH_ALL",
                                    "filterLabels": match_labels,
                                }
                            ],
                        }
                    ],
                    "service": alternate_backend_service.url,
                },
            ],
            # test mixing MATCH_ALL and MATCH_ANY
            # test MATCH_ALL: super set labels won't match
            [
                {
                    "priority": 0,
                    "matchRules": [
                        {
                            "prefixMatch": "/",
                            "metadataFilters": [
                                {
                                    "filterMatchCriteria": "MATCH_ALL",
                                    "filterLabels": not_match_labels
                                    + match_labels,
                                }
                            ],
                        }
                    ],
                    "service": original_backend_service.url,
                },
                {
                    "priority": 1,
                    "matchRules": [
                        {
                            "prefixMatch": "/",
                            "metadataFilters": [
                                {
                                    "filterMatchCriteria": "MATCH_ANY",
                                    "filterLabels": not_match_labels
                                    + match_labels,
                                }
                            ],
                        }
                    ],
                    "service": alternate_backend_service.url,
                },
            ],
            # test MATCH_ANY
            [
                {
                    "priority": 0,
                    "matchRules": [
                        {
                            "prefixMatch": "/",
                            "metadataFilters": [
                                {
                                    "filterMatchCriteria": "MATCH_ANY",
                                    "filterLabels": not_match_labels,
                                }
                            ],
                        }
                    ],
                    "service": original_backend_service.url,
                },
                {
                    "priority": 1,
                    "matchRules": [
                        {
                            "prefixMatch": "/",
                            "metadataFilters": [
                                {
                                    "filterMatchCriteria": "MATCH_ANY",
                                    "filterLabels": not_match_labels
                                    + match_labels,
                                }
                            ],
                        }
                    ],
                    "service": alternate_backend_service.url,
                },
            ],
            # test match multiple route rules
            [
                {
                    "priority": 0,
                    "matchRules": [
                        {
                            "prefixMatch": "/",
                            "metadataFilters": [
                                {
                                    "filterMatchCriteria": "MATCH_ANY",
                                    "filterLabels": match_labels,
                                }
                            ],
                        }
                    ],
                    "service": alternate_backend_service.url,
                },
                {
                    "priority": 1,
                    "matchRules": [
                        {
                            "prefixMatch": "/",
                            "metadataFilters": [
                                {
                                    "filterMatchCriteria": "MATCH_ALL",
                                    "filterLabels": match_labels,
                                }
                            ],
                        }
                    ],
                    "service": original_backend_service.url,
                },
            ],
        ]

        for route_rules in test_route_rules:
            wait_until_all_rpcs_go_to_given_backends(
                original_backend_instances, _WAIT_FOR_STATS_SEC
            )
            patch_url_map_backend_service(
                gcp, original_backend_service, route_rules=route_rules
            )
            wait_until_no_rpcs_go_to_given_backends(
                original_backend_instances, _WAIT_FOR_STATS_SEC
            )
            wait_until_all_rpcs_go_to_given_backends(
                alternate_backend_instances, _WAIT_FOR_STATS_SEC
            )
            patch_url_map_backend_service(gcp, original_backend_service)
    except Exception:
        passed = False
        raise
    finally:
        if passed or not args.halt_after_fail:
            patch_backend_service(gcp, alternate_backend_service, [])


def test_api_listener(
    gcp, backend_service, instance_group, alternate_backend_service
):
    logger.info("Running api_listener")
    passed = True
    try:
        wait_for_healthy_backends(gcp, backend_service, instance_group)
        backend_instances = get_instance_names(gcp, instance_group)
        wait_until_all_rpcs_go_to_given_backends(
            backend_instances, _WAIT_FOR_STATS_SEC
        )
        # create a second suite of map+tp+fr with the same host name in host rule
        # and we have to disable proxyless validation because it needs `0.0.0.0`
        # ip address in fr for proxyless and also we violate ip:port uniqueness
        # for test purpose. See https://github.com/grpc/grpc-java/issues/8009
        new_config_suffix = "2"
        url_map_2 = create_url_map(
            gcp,
            url_map_name + new_config_suffix,
            backend_service,
            service_host_name,
        )
        target_proxy_2 = create_target_proxy(
            gcp, target_proxy_name + new_config_suffix, False, url_map_2
        )
        if not gcp.service_port:
            raise Exception(
                "Faied to find a valid port for the forwarding rule"
            )
        potential_ip_addresses = []
        max_attempts = 10
        for i in range(max_attempts):
            potential_ip_addresses.append(
                "10.10.10.%d" % (random.randint(0, 255))
            )
        create_global_forwarding_rule(
            gcp,
            forwarding_rule_name + new_config_suffix,
            [gcp.service_port],
            potential_ip_addresses,
            target_proxy_2,
        )
        if gcp.service_port != _DEFAULT_SERVICE_PORT:
            patch_url_map_host_rule_with_port(
                gcp,
                url_map_name + new_config_suffix,
                backend_service,
                service_host_name,
            )
        wait_until_all_rpcs_go_to_given_backends(
            backend_instances, _WAIT_FOR_STATS_SEC
        )

        delete_global_forwarding_rule(gcp, gcp.global_forwarding_rules[0])
        delete_target_proxy(gcp, gcp.target_proxies[0])
        delete_url_map(gcp, gcp.url_maps[0])
        verify_attempts = int(
            _WAIT_FOR_URL_MAP_PATCH_SEC / _NUM_TEST_RPCS * args.qps
        )
        for i in range(verify_attempts):
            wait_until_all_rpcs_go_to_given_backends(
                backend_instances, _WAIT_FOR_STATS_SEC
            )
        # delete host rule for the original host name
        patch_url_map_backend_service(gcp, alternate_backend_service)
        wait_until_no_rpcs_go_to_given_backends(
            backend_instances, _WAIT_FOR_STATS_SEC
        )

    except Exception:
        passed = False
        raise
    finally:
        if passed or not args.halt_after_fail:
            delete_global_forwarding_rules(gcp)
            delete_target_proxies(gcp)
            delete_url_maps(gcp)
            create_url_map(
                gcp, url_map_name, backend_service, service_host_name
            )
            create_target_proxy(gcp, target_proxy_name)
            create_global_forwarding_rule(
                gcp, forwarding_rule_name, potential_service_ports
            )
            if gcp.service_port != _DEFAULT_SERVICE_PORT:
                patch_url_map_host_rule_with_port(
                    gcp, url_map_name, backend_service, service_host_name
                )
                server_uri = service_host_name + ":" + str(gcp.service_port)
            else:
                server_uri = service_host_name
            return server_uri


def test_forwarding_rule_port_match(gcp, backend_service, instance_group):
    logger.info("Running test_forwarding_rule_port_match")
    passed = True
    try:
        wait_for_healthy_backends(gcp, backend_service, instance_group)
        backend_instances = get_instance_names(gcp, instance_group)
        wait_until_all_rpcs_go_to_given_backends(
            backend_instances, _WAIT_FOR_STATS_SEC
        )
        delete_global_forwarding_rules(gcp)
        create_global_forwarding_rule(
            gcp,
            forwarding_rule_name,
            [
                x
                for x in parse_port_range(_DEFAULT_PORT_RANGE)
                if x != gcp.service_port
            ],
        )
        wait_until_no_rpcs_go_to_given_backends(
            backend_instances, _WAIT_FOR_STATS_SEC
        )
    except Exception:
        passed = False
        raise
    finally:
        if passed or not args.halt_after_fail:
            delete_global_forwarding_rules(gcp)
            create_global_forwarding_rule(
                gcp, forwarding_rule_name, potential_service_ports
            )
            if gcp.service_port != _DEFAULT_SERVICE_PORT:
                patch_url_map_host_rule_with_port(
                    gcp, url_map_name, backend_service, service_host_name
                )
                server_uri = service_host_name + ":" + str(gcp.service_port)
            else:
                server_uri = service_host_name
            return server_uri


def test_forwarding_rule_default_port(gcp, backend_service, instance_group):
    logger.info("Running test_forwarding_rule_default_port")
    passed = True
    try:
        wait_for_healthy_backends(gcp, backend_service, instance_group)
        backend_instances = get_instance_names(gcp, instance_group)
        if gcp.service_port == _DEFAULT_SERVICE_PORT:
            wait_until_all_rpcs_go_to_given_backends(
                backend_instances, _WAIT_FOR_STATS_SEC
            )
            delete_global_forwarding_rules(gcp)
            create_global_forwarding_rule(
                gcp, forwarding_rule_name, parse_port_range(_DEFAULT_PORT_RANGE)
            )
            patch_url_map_host_rule_with_port(
                gcp, url_map_name, backend_service, service_host_name
            )
        wait_until_no_rpcs_go_to_given_backends(
            backend_instances, _WAIT_FOR_STATS_SEC
        )
        # expect success when no port in client request service uri, and no port in url-map
        delete_global_forwarding_rule(gcp, gcp.global_forwarding_rules[0])
        delete_target_proxy(gcp, gcp.target_proxies[0])
        delete_url_map(gcp, gcp.url_maps[0])
        create_url_map(gcp, url_map_name, backend_service, service_host_name)
        create_target_proxy(gcp, target_proxy_name, False)
        potential_ip_addresses = []
        max_attempts = 10
        for i in range(max_attempts):
            potential_ip_addresses.append(
                "10.10.10.%d" % (random.randint(0, 255))
            )
        create_global_forwarding_rule(
            gcp, forwarding_rule_name, [80], potential_ip_addresses
        )
        wait_until_all_rpcs_go_to_given_backends(
            backend_instances, _WAIT_FOR_STATS_SEC
        )

        # expect failure when no port in client request uri, but specify port in url-map
        patch_url_map_host_rule_with_port(
            gcp, url_map_name, backend_service, service_host_name
        )
        wait_until_no_rpcs_go_to_given_backends(
            backend_instances, _WAIT_FOR_STATS_SEC
        )
    except Exception:
        passed = False
        raise
    finally:
        if passed or not args.halt_after_fail:
            delete_global_forwarding_rules(gcp)
            delete_target_proxies(gcp)
            delete_url_maps(gcp)
            create_url_map(
                gcp, url_map_name, backend_service, service_host_name
            )
            create_target_proxy(gcp, target_proxy_name)
            create_global_forwarding_rule(
                gcp, forwarding_rule_name, potential_service_ports
            )
            if gcp.service_port != _DEFAULT_SERVICE_PORT:
                patch_url_map_host_rule_with_port(
                    gcp, url_map_name, backend_service, service_host_name
                )
                server_uri = service_host_name + ":" + str(gcp.service_port)
            else:
                server_uri = service_host_name
            return server_uri


def test_traffic_splitting(
    gcp,
    original_backend_service,
    instance_group,
    alternate_backend_service,
    same_zone_instance_group,
):
    # This test start with all traffic going to original_backend_service. Then
    # it updates URL-map to set default action to traffic splitting between
    # original and alternate. It waits for all backends in both services to
    # receive traffic, then verifies that weights are expected.
    logger.info("Running test_traffic_splitting")

    (
        original_backend_instances,
        alternate_backend_instances,
    ) = prepare_services_for_urlmap_tests(
        gcp,
        original_backend_service,
        instance_group,
        alternate_backend_service,
        same_zone_instance_group,
    )

    passed = True
    try:
        # Patch urlmap, change route action to traffic splitting between
        # original and alternate.
        logger.info("patching url map with traffic splitting")
        original_service_percentage, alternate_service_percentage = 20, 80
        patch_url_map_backend_service(
            gcp,
            services_with_weights={
                original_backend_service: original_service_percentage,
                alternate_backend_service: alternate_service_percentage,
            },
        )
        # Split percentage between instances: [20,80] -> [10,10,40,40].
        expected_instance_percentage = [
            original_service_percentage * 1.0 / len(original_backend_instances)
        ] * len(original_backend_instances) + [
            alternate_service_percentage
            * 1.0
            / len(alternate_backend_instances)
        ] * len(
            alternate_backend_instances
        )

        # Wait for traffic to go to both services.
        logger.info(
            "waiting for traffic to go to all backends (including alternate)"
        )
        wait_until_all_rpcs_go_to_given_backends(
            original_backend_instances + alternate_backend_instances,
            _WAIT_FOR_STATS_SEC,
        )

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
                compare_distributions(
                    got_instance_percentage, expected_instance_percentage, 5
                )
            except Exception as e:
                logger.info("attempt %d", i)
                logger.info("got percentage: %s", got_instance_percentage)
                logger.info(
                    "expected percentage: %s", expected_instance_percentage
                )
                logger.info(e)
                if i == retry_count - 1:
                    raise Exception(
                        "RPC distribution (%s) differs from expected (%s)"
                        % (
                            got_instance_percentage,
                            expected_instance_percentage,
                        )
                    )
            else:
                logger.info("success")
                break
    except Exception:
        passed = False
        raise
    finally:
        if passed or not args.halt_after_fail:
            patch_url_map_backend_service(gcp, original_backend_service)
            patch_backend_service(gcp, alternate_backend_service, [])


def test_path_matching(
    gcp,
    original_backend_service,
    instance_group,
    alternate_backend_service,
    same_zone_instance_group,
):
    # This test start with all traffic (UnaryCall and EmptyCall) going to
    # original_backend_service.
    #
    # Then it updates URL-map to add routes, to make UnaryCall and EmptyCall to
    # go different backends. It waits for all backends in both services to
    # receive traffic, then verifies that traffic goes to the expected
    # backends.
    logger.info("Running test_path_matching")

    (
        original_backend_instances,
        alternate_backend_instances,
    ) = prepare_services_for_urlmap_tests(
        gcp,
        original_backend_service,
        instance_group,
        alternate_backend_service,
        same_zone_instance_group,
    )

    passed = True
    try:
        # A list of tuples (route_rules, expected_instances).
        test_cases = [
            (
                [
                    {
                        "priority": 0,
                        # FullPath EmptyCall -> alternate_backend_service.
                        "matchRules": [
                            {
                                "fullPathMatch": (
                                    "/grpc.testing.TestService/EmptyCall"
                                )
                            }
                        ],
                        "service": alternate_backend_service.url,
                    }
                ],
                {
                    "EmptyCall": alternate_backend_instances,
                    "UnaryCall": original_backend_instances,
                },
            ),
            (
                [
                    {
                        "priority": 0,
                        # Prefix UnaryCall -> alternate_backend_service.
                        "matchRules": [
                            {"prefixMatch": "/grpc.testing.TestService/Unary"}
                        ],
                        "service": alternate_backend_service.url,
                    }
                ],
                {
                    "UnaryCall": alternate_backend_instances,
                    "EmptyCall": original_backend_instances,
                },
            ),
            (
                # This test case is similar to the one above (but with route
                # services swapped). This test has two routes (full_path and
                # the default) to match EmptyCall, and both routes set
                # alternative_backend_service as the action. This forces the
                # client to handle duplicate Clusters in the RDS response.
                [
                    {
                        "priority": 0,
                        # Prefix UnaryCall -> original_backend_service.
                        "matchRules": [
                            {"prefixMatch": "/grpc.testing.TestService/Unary"}
                        ],
                        "service": original_backend_service.url,
                    },
                    {
                        "priority": 1,
                        # FullPath EmptyCall -> alternate_backend_service.
                        "matchRules": [
                            {
                                "fullPathMatch": (
                                    "/grpc.testing.TestService/EmptyCall"
                                )
                            }
                        ],
                        "service": alternate_backend_service.url,
                    },
                ],
                {
                    "UnaryCall": original_backend_instances,
                    "EmptyCall": alternate_backend_instances,
                },
            ),
            (
                [
                    {
                        "priority": 0,
                        # Regex UnaryCall -> alternate_backend_service.
                        "matchRules": [
                            {
                                "regexMatch": (  # Unary methods with any services.
                                    "^\/.*\/UnaryCall$"
                                )
                            }
                        ],
                        "service": alternate_backend_service.url,
                    }
                ],
                {
                    "UnaryCall": alternate_backend_instances,
                    "EmptyCall": original_backend_instances,
                },
            ),
            (
                [
                    {
                        "priority": 0,
                        # ignoreCase EmptyCall -> alternate_backend_service.
                        "matchRules": [
                            {
                                # Case insensitive matching.
                                "fullPathMatch": (
                                    "/gRpC.tEsTinG.tEstseRvice/empTycaLl"
                                ),
                                "ignoreCase": True,
                            }
                        ],
                        "service": alternate_backend_service.url,
                    }
                ],
                {
                    "UnaryCall": original_backend_instances,
                    "EmptyCall": alternate_backend_instances,
                },
            ),
        ]

        for route_rules, expected_instances in test_cases:
            logger.info("patching url map with %s", route_rules)
            patch_url_map_backend_service(
                gcp, original_backend_service, route_rules=route_rules
            )

            # Wait for traffic to go to both services.
            logger.info(
                "waiting for traffic to go to all backends (including"
                " alternate)"
            )
            wait_until_all_rpcs_go_to_given_backends(
                original_backend_instances + alternate_backend_instances,
                _WAIT_FOR_STATS_SEC,
            )

            retry_count = 80
            # Each attempt takes about 5 seconds, 80 retries is equivalent to 400
            # seconds timeout.
            for i in range(retry_count):
                stats = get_client_stats(_NUM_TEST_RPCS, _WAIT_FOR_STATS_SEC)
                if not stats.rpcs_by_method:
                    raise ValueError(
                        "stats.rpcs_by_method is None, the interop client stats"
                        " service does not support this test case"
                    )
                logger.info("attempt %d", i)
                if compare_expected_instances(stats, expected_instances):
                    logger.info("success")
                    break
                elif i == retry_count - 1:
                    raise Exception(
                        "timeout waiting for RPCs to the expected instances: %s"
                        % expected_instances
                    )
    except Exception:
        passed = False
        raise
    finally:
        if passed or not args.halt_after_fail:
            patch_url_map_backend_service(gcp, original_backend_service)
            patch_backend_service(gcp, alternate_backend_service, [])


def test_header_matching(
    gcp,
    original_backend_service,
    instance_group,
    alternate_backend_service,
    same_zone_instance_group,
):
    # This test start with all traffic (UnaryCall and EmptyCall) going to
    # original_backend_service.
    #
    # Then it updates URL-map to add routes, to make RPCs with test headers to
    # go to different backends. It waits for all backends in both services to
    # receive traffic, then verifies that traffic goes to the expected
    # backends.
    logger.info("Running test_header_matching")

    (
        original_backend_instances,
        alternate_backend_instances,
    ) = prepare_services_for_urlmap_tests(
        gcp,
        original_backend_service,
        instance_group,
        alternate_backend_service,
        same_zone_instance_group,
    )

    passed = True
    try:
        # A list of tuples (route_rules, expected_instances).
        test_cases = [
            (
                [
                    {
                        "priority": 0,
                        # Header ExactMatch -> alternate_backend_service.
                        # EmptyCall is sent with the metadata.
                        "matchRules": [
                            {
                                "prefixMatch": "/",
                                "headerMatches": [
                                    {
                                        "headerName": _TEST_METADATA_KEY,
                                        "exactMatch": _TEST_METADATA_VALUE_EMPTY,
                                    }
                                ],
                            }
                        ],
                        "service": alternate_backend_service.url,
                    }
                ],
                {
                    "EmptyCall": alternate_backend_instances,
                    "UnaryCall": original_backend_instances,
                },
            ),
            (
                [
                    {
                        "priority": 0,
                        # Header PrefixMatch -> alternate_backend_service.
                        # UnaryCall is sent with the metadata.
                        "matchRules": [
                            {
                                "prefixMatch": "/",
                                "headerMatches": [
                                    {
                                        "headerName": _TEST_METADATA_KEY,
                                        "prefixMatch": _TEST_METADATA_VALUE_UNARY[
                                            :2
                                        ],
                                    }
                                ],
                            }
                        ],
                        "service": alternate_backend_service.url,
                    }
                ],
                {
                    "EmptyCall": original_backend_instances,
                    "UnaryCall": alternate_backend_instances,
                },
            ),
            (
                [
                    {
                        "priority": 0,
                        # Header SuffixMatch -> alternate_backend_service.
                        # EmptyCall is sent with the metadata.
                        "matchRules": [
                            {
                                "prefixMatch": "/",
                                "headerMatches": [
                                    {
                                        "headerName": _TEST_METADATA_KEY,
                                        "suffixMatch": _TEST_METADATA_VALUE_EMPTY[
                                            -2:
                                        ],
                                    }
                                ],
                            }
                        ],
                        "service": alternate_backend_service.url,
                    }
                ],
                {
                    "EmptyCall": alternate_backend_instances,
                    "UnaryCall": original_backend_instances,
                },
            ),
            (
                [
                    {
                        "priority": 0,
                        # Header 'xds_md_numeric' present -> alternate_backend_service.
                        # UnaryCall is sent with the metadata, so will be sent to alternative.
                        "matchRules": [
                            {
                                "prefixMatch": "/",
                                "headerMatches": [
                                    {
                                        "headerName": _TEST_METADATA_NUMERIC_KEY,
                                        "presentMatch": True,
                                    }
                                ],
                            }
                        ],
                        "service": alternate_backend_service.url,
                    }
                ],
                {
                    "EmptyCall": original_backend_instances,
                    "UnaryCall": alternate_backend_instances,
                },
            ),
            (
                [
                    {
                        "priority": 0,
                        # Header invert ExactMatch -> alternate_backend_service.
                        # UnaryCall is sent with the metadata, so will be sent to
                        # original. EmptyCall will be sent to alternative.
                        "matchRules": [
                            {
                                "prefixMatch": "/",
                                "headerMatches": [
                                    {
                                        "headerName": _TEST_METADATA_KEY,
                                        "exactMatch": _TEST_METADATA_VALUE_UNARY,
                                        "invertMatch": True,
                                    }
                                ],
                            }
                        ],
                        "service": alternate_backend_service.url,
                    }
                ],
                {
                    "EmptyCall": alternate_backend_instances,
                    "UnaryCall": original_backend_instances,
                },
            ),
            (
                [
                    {
                        "priority": 0,
                        # Header 'xds_md_numeric' range [100,200] -> alternate_backend_service.
                        # UnaryCall is sent with the metadata in range.
                        "matchRules": [
                            {
                                "prefixMatch": "/",
                                "headerMatches": [
                                    {
                                        "headerName": _TEST_METADATA_NUMERIC_KEY,
                                        "rangeMatch": {
                                            "rangeStart": "100",
                                            "rangeEnd": "200",
                                        },
                                    }
                                ],
                            }
                        ],
                        "service": alternate_backend_service.url,
                    }
                ],
                {
                    "EmptyCall": original_backend_instances,
                    "UnaryCall": alternate_backend_instances,
                },
            ),
            (
                [
                    {
                        "priority": 0,
                        # Header RegexMatch -> alternate_backend_service.
                        # EmptyCall is sent with the metadata.
                        "matchRules": [
                            {
                                "prefixMatch": "/",
                                "headerMatches": [
                                    {
                                        "headerName": _TEST_METADATA_KEY,
                                        "regexMatch": "^%s.*%s$"
                                        % (
                                            _TEST_METADATA_VALUE_EMPTY[:2],
                                            _TEST_METADATA_VALUE_EMPTY[-2:],
                                        ),
                                    }
                                ],
                            }
                        ],
                        "service": alternate_backend_service.url,
                    }
                ],
                {
                    "EmptyCall": alternate_backend_instances,
                    "UnaryCall": original_backend_instances,
                },
            ),
        ]

        for route_rules, expected_instances in test_cases:
            logger.info(
                "patching url map with %s -> alternative",
                route_rules[0]["matchRules"],
            )
            patch_url_map_backend_service(
                gcp, original_backend_service, route_rules=route_rules
            )

            # Wait for traffic to go to both services.
            logger.info(
                "waiting for traffic to go to all backends (including"
                " alternate)"
            )
            wait_until_all_rpcs_go_to_given_backends(
                original_backend_instances + alternate_backend_instances,
                _WAIT_FOR_STATS_SEC,
            )

            retry_count = 80
            # Each attempt takes about 5 seconds, 80 retries is equivalent to 400
            # seconds timeout.
            for i in range(retry_count):
                stats = get_client_stats(_NUM_TEST_RPCS, _WAIT_FOR_STATS_SEC)
                if not stats.rpcs_by_method:
                    raise ValueError(
                        "stats.rpcs_by_method is None, the interop client stats"
                        " service does not support this test case"
                    )
                logger.info("attempt %d", i)
                if compare_expected_instances(stats, expected_instances):
                    logger.info("success")
                    break
                elif i == retry_count - 1:
                    raise Exception(
                        "timeout waiting for RPCs to the expected instances: %s"
                        % expected_instances
                    )
    except Exception:
        passed = False
        raise
    finally:
        if passed or not args.halt_after_fail:
            patch_url_map_backend_service(gcp, original_backend_service)
            patch_backend_service(gcp, alternate_backend_service, [])


def test_circuit_breaking(
    gcp, original_backend_service, instance_group, same_zone_instance_group
):
    """
    Since backend service circuit_breakers configuration cannot be unset,
    which causes trouble for restoring validate_for_proxy flag in target
    proxy/global forwarding rule. This test uses dedicated backend sevices.
    The url_map and backend services undergoes the following state changes:

    Before test:
       original_backend_service -> [instance_group]
       extra_backend_service -> []
       more_extra_backend_service -> []

       url_map -> [original_backend_service]

    In test:
       extra_backend_service (with circuit_breakers) -> [instance_group]
       more_extra_backend_service (with circuit_breakers) -> [same_zone_instance_group]

       url_map -> [extra_backend_service, more_extra_backend_service]

    After test:
       original_backend_service -> [instance_group]
       extra_backend_service (with circuit_breakers) -> []
       more_extra_backend_service (with circuit_breakers) -> []

       url_map -> [original_backend_service]
    """
    logger.info("Running test_circuit_breaking")
    additional_backend_services = []
    passed = True
    try:
        # TODO(chengyuanzhang): Dedicated backend services created for circuit
        # breaking test. Once the issue for unsetting backend service circuit
        # breakers is resolved or configuring backend service circuit breakers is
        # enabled for config validation, these dedicated backend services can be
        # eliminated.
        extra_backend_service_name = (
            _BASE_BACKEND_SERVICE_NAME + "-extra" + gcp_suffix
        )
        more_extra_backend_service_name = (
            _BASE_BACKEND_SERVICE_NAME + "-more-extra" + gcp_suffix
        )
        extra_backend_service = add_backend_service(
            gcp, extra_backend_service_name
        )
        additional_backend_services.append(extra_backend_service)
        more_extra_backend_service = add_backend_service(
            gcp, more_extra_backend_service_name
        )
        additional_backend_services.append(more_extra_backend_service)
        # The config validation for proxyless doesn't allow setting
        # circuit_breakers. Disable validate validate_for_proxyless
        # for this test. This can be removed when validation
        # accepts circuit_breakers.
        logger.info("disabling validate_for_proxyless in target proxy")
        set_validate_for_proxyless(gcp, False)
        extra_backend_service_max_requests = 500
        more_extra_backend_service_max_requests = 1000
        patch_backend_service(
            gcp,
            extra_backend_service,
            [instance_group],
            circuit_breakers={
                "maxRequests": extra_backend_service_max_requests
            },
        )
        logger.info("Waiting for extra backends to become healthy")
        wait_for_healthy_backends(gcp, extra_backend_service, instance_group)
        patch_backend_service(
            gcp,
            more_extra_backend_service,
            [same_zone_instance_group],
            circuit_breakers={
                "maxRequests": more_extra_backend_service_max_requests
            },
        )
        logger.info("Waiting for more extra backend to become healthy")
        wait_for_healthy_backends(
            gcp, more_extra_backend_service, same_zone_instance_group
        )
        extra_backend_instances = get_instance_names(gcp, instance_group)
        more_extra_backend_instances = get_instance_names(
            gcp, same_zone_instance_group
        )
        route_rules = [
            {
                "priority": 0,
                # UnaryCall -> extra_backend_service
                "matchRules": [
                    {"fullPathMatch": "/grpc.testing.TestService/UnaryCall"}
                ],
                "service": extra_backend_service.url,
            },
            {
                "priority": 1,
                # EmptyCall -> more_extra_backend_service
                "matchRules": [
                    {"fullPathMatch": "/grpc.testing.TestService/EmptyCall"}
                ],
                "service": more_extra_backend_service.url,
            },
        ]

        # Make client send UNARY_CALL and EMPTY_CALL.
        configure_client(
            [
                messages_pb2.ClientConfigureRequest.RpcType.UNARY_CALL,
                messages_pb2.ClientConfigureRequest.RpcType.EMPTY_CALL,
            ]
        )
        logger.info("Patching url map with %s", route_rules)
        patch_url_map_backend_service(
            gcp, extra_backend_service, route_rules=route_rules
        )
        logger.info("Waiting for traffic to go to all backends")
        wait_until_all_rpcs_go_to_given_backends(
            extra_backend_instances + more_extra_backend_instances,
            _WAIT_FOR_STATS_SEC,
        )

        # Make all calls keep-open.
        configure_client(
            [
                messages_pb2.ClientConfigureRequest.RpcType.UNARY_CALL,
                messages_pb2.ClientConfigureRequest.RpcType.EMPTY_CALL,
            ],
            [
                (
                    messages_pb2.ClientConfigureRequest.RpcType.UNARY_CALL,
                    "rpc-behavior",
                    "keep-open",
                ),
                (
                    messages_pb2.ClientConfigureRequest.RpcType.EMPTY_CALL,
                    "rpc-behavior",
                    "keep-open",
                ),
            ],
        )
        wait_until_rpcs_in_flight(
            "UNARY_CALL",
            (
                _WAIT_FOR_BACKEND_SEC
                + int(extra_backend_service_max_requests / args.qps)
            ),
            extra_backend_service_max_requests,
            1,
        )
        logger.info(
            "UNARY_CALL reached stable state (%d)",
            extra_backend_service_max_requests,
        )
        wait_until_rpcs_in_flight(
            "EMPTY_CALL",
            (
                _WAIT_FOR_BACKEND_SEC
                + int(more_extra_backend_service_max_requests / args.qps)
            ),
            more_extra_backend_service_max_requests,
            1,
        )
        logger.info(
            "EMPTY_CALL reached stable state (%d)",
            more_extra_backend_service_max_requests,
        )

        # Increment circuit breakers max_requests threshold.
        extra_backend_service_max_requests = 800
        patch_backend_service(
            gcp,
            extra_backend_service,
            [instance_group],
            circuit_breakers={
                "maxRequests": extra_backend_service_max_requests
            },
        )
        wait_until_rpcs_in_flight(
            "UNARY_CALL",
            (
                _WAIT_FOR_BACKEND_SEC
                + int(extra_backend_service_max_requests / args.qps)
            ),
            extra_backend_service_max_requests,
            1,
        )
        logger.info(
            "UNARY_CALL reached stable state after increase (%d)",
            extra_backend_service_max_requests,
        )
        logger.info("success")
        # Avoid new RPCs being outstanding (some test clients create threads
        # for sending RPCs) after restoring backend services.
        configure_client(
            [messages_pb2.ClientConfigureRequest.RpcType.UNARY_CALL]
        )
    except Exception:
        passed = False
        raise
    finally:
        if passed or not args.halt_after_fail:
            patch_url_map_backend_service(gcp, original_backend_service)
            patch_backend_service(
                gcp, original_backend_service, [instance_group]
            )
            for backend_service in additional_backend_services:
                delete_backend_service(gcp, backend_service)
            set_validate_for_proxyless(gcp, True)


def test_timeout(gcp, original_backend_service, instance_group):
    logger.info("Running test_timeout")

    logger.info("waiting for original backends to become healthy")
    wait_for_healthy_backends(gcp, original_backend_service, instance_group)

    # UnaryCall -> maxStreamDuration:3s
    route_rules = [
        {
            "priority": 0,
            "matchRules": [
                {"fullPathMatch": "/grpc.testing.TestService/UnaryCall"}
            ],
            "service": original_backend_service.url,
            "routeAction": {
                "maxStreamDuration": {
                    "seconds": 3,
                },
            },
        }
    ]
    patch_url_map_backend_service(
        gcp, original_backend_service, route_rules=route_rules
    )
    # A list of tuples (testcase_name, {client_config}, {expected_results})
    test_cases = [
        (
            (
                "timeout_exceeded (UNARY_CALL), timeout_different_route"
                " (EMPTY_CALL)"
            ),
            # UnaryCall and EmptyCall both sleep-4.
            # UnaryCall timeouts, EmptyCall succeeds.
            {
                "rpc_types": [
                    messages_pb2.ClientConfigureRequest.RpcType.UNARY_CALL,
                    messages_pb2.ClientConfigureRequest.RpcType.EMPTY_CALL,
                ],
                "metadata": [
                    (
                        messages_pb2.ClientConfigureRequest.RpcType.UNARY_CALL,
                        "rpc-behavior",
                        "sleep-4",
                    ),
                    (
                        messages_pb2.ClientConfigureRequest.RpcType.EMPTY_CALL,
                        "rpc-behavior",
                        "sleep-4",
                    ),
                ],
            },
            {
                "UNARY_CALL": 4,  # DEADLINE_EXCEEDED
                "EMPTY_CALL": 0,
            },
        ),
        (
            "app_timeout_exceeded",
            # UnaryCall only with sleep-2; timeout=1s; calls timeout.
            {
                "rpc_types": [
                    messages_pb2.ClientConfigureRequest.RpcType.UNARY_CALL,
                ],
                "metadata": [
                    (
                        messages_pb2.ClientConfigureRequest.RpcType.UNARY_CALL,
                        "rpc-behavior",
                        "sleep-2",
                    ),
                ],
                "timeout_sec": 1,
            },
            {
                "UNARY_CALL": 4,  # DEADLINE_EXCEEDED
            },
        ),
        (
            "timeout_not_exceeded",
            # UnaryCall only with no sleep; calls succeed.
            {
                "rpc_types": [
                    messages_pb2.ClientConfigureRequest.RpcType.UNARY_CALL,
                ],
            },
            {
                "UNARY_CALL": 0,
            },
        ),
    ]

    passed = True
    try:
        first_case = True
        for testcase_name, client_config, expected_results in test_cases:
            logger.info("starting case %s", testcase_name)
            configure_client(**client_config)
            # wait a second to help ensure the client stops sending RPCs with
            # the old config.  We will make multiple attempts if it is failing,
            # but this improves confidence that the test is valid if the
            # previous client_config would lead to the same results.
            time.sleep(1)
            # Each attempt takes 10 seconds; 20 attempts is equivalent to 200
            # second timeout.
            attempt_count = 20
            if first_case:
                attempt_count = 120
                first_case = False
            before_stats = get_client_accumulated_stats()
            if not before_stats.stats_per_method:
                raise ValueError(
                    "stats.stats_per_method is None, the interop client stats"
                    " service does not support this test case"
                )
            for i in range(attempt_count):
                logger.info("%s: attempt %d", testcase_name, i)

                test_runtime_secs = 10
                time.sleep(test_runtime_secs)
                after_stats = get_client_accumulated_stats()

                success = True
                for rpc, status in list(expected_results.items()):
                    qty = (
                        after_stats.stats_per_method[rpc].result[status]
                        - before_stats.stats_per_method[rpc].result[status]
                    )
                    want = test_runtime_secs * args.qps
                    # Allow 10% deviation from expectation to reduce flakiness
                    if qty < (want * 0.9) or qty > (want * 1.1):
                        logger.info(
                            "%s: failed due to %s[%s]: got %d want ~%d",
                            testcase_name,
                            rpc,
                            status,
                            qty,
                            want,
                        )
                        success = False
                if success:
                    logger.info("success")
                    break
                logger.info("%s attempt %d failed", testcase_name, i)
                before_stats = after_stats
            else:
                raise Exception(
                    "%s: timeout waiting for expected results: %s; got %s"
                    % (
                        testcase_name,
                        expected_results,
                        after_stats.stats_per_method,
                    )
                )
    except Exception:
        passed = False
        raise
    finally:
        if passed or not args.halt_after_fail:
            patch_url_map_backend_service(gcp, original_backend_service)


def test_fault_injection(gcp, original_backend_service, instance_group):
    logger.info("Running test_fault_injection")

    logger.info("waiting for original backends to become healthy")
    wait_for_healthy_backends(gcp, original_backend_service, instance_group)

    testcase_header = "fi_testcase"

    def _route(pri, name, fi_policy):
        return {
            "priority": pri,
            "matchRules": [
                {
                    "prefixMatch": "/",
                    "headerMatches": [
                        {
                            "headerName": testcase_header,
                            "exactMatch": name,
                        }
                    ],
                }
            ],
            "service": original_backend_service.url,
            "routeAction": {"faultInjectionPolicy": fi_policy},
        }

    def _abort(pct):
        return {
            "abort": {
                "httpStatus": 401,
                "percentage": pct,
            }
        }

    def _delay(pct):
        return {
            "delay": {
                "fixedDelay": {"seconds": "20"},
                "percentage": pct,
            }
        }

    zero_route = _abort(0)
    zero_route.update(_delay(0))
    route_rules = [
        _route(0, "zero_percent_fault_injection", zero_route),
        _route(1, "always_delay", _delay(100)),
        _route(2, "always_abort", _abort(100)),
        _route(3, "delay_half", _delay(50)),
        _route(4, "abort_half", _abort(50)),
        {
            "priority": 5,
            "matchRules": [{"prefixMatch": "/"}],
            "service": original_backend_service.url,
        },
    ]
    set_validate_for_proxyless(gcp, False)
    patch_url_map_backend_service(
        gcp, original_backend_service, route_rules=route_rules
    )
    # A list of tuples (testcase_name, {client_config}, {code: percent}).  Each
    # test case will set the testcase_header with the testcase_name for routing
    # to the appropriate config for the case, defined above.
    test_cases = [
        (
            "always_delay",
            {"timeout_sec": 2},
            {4: 1},  # DEADLINE_EXCEEDED
        ),
        (
            "always_abort",
            {},
            {16: 1},  # UNAUTHENTICATED
        ),
        (
            "delay_half",
            {"timeout_sec": 2},
            {4: 0.5, 0: 0.5},  # DEADLINE_EXCEEDED / OK: 50% / 50%
        ),
        (
            "abort_half",
            {},
            {16: 0.5, 0: 0.5},  # UNAUTHENTICATED / OK: 50% / 50%
        ),
        (
            "zero_percent_fault_injection",
            {},
            {0: 1},  # OK
        ),
        (
            "non_matching_fault_injection",  # Not in route_rules, above.
            {},
            {0: 1},  # OK
        ),
    ]

    passed = True
    try:
        first_case = True
        for testcase_name, client_config, expected_results in test_cases:
            logger.info("starting case %s", testcase_name)

            client_config["metadata"] = [
                (
                    messages_pb2.ClientConfigureRequest.RpcType.UNARY_CALL,
                    testcase_header,
                    testcase_name,
                )
            ]
            client_config["rpc_types"] = [
                messages_pb2.ClientConfigureRequest.RpcType.UNARY_CALL,
            ]
            configure_client(**client_config)
            # wait a second to help ensure the client stops sending RPCs with
            # the old config.  We will make multiple attempts if it is failing,
            # but this improves confidence that the test is valid if the
            # previous client_config would lead to the same results.
            time.sleep(1)
            # Each attempt takes 10 seconds
            if first_case:
                # Give the first test case 600s for xDS config propagation.
                attempt_count = 60
                first_case = False
            else:
                # The accumulated stats might include previous sub-test, running
                # the test multiple times to deflake
                attempt_count = 10
            before_stats = get_client_accumulated_stats()
            if not before_stats.stats_per_method:
                raise ValueError(
                    "stats.stats_per_method is None, the interop client stats"
                    " service does not support this test case"
                )
            for i in range(attempt_count):
                logger.info("%s: attempt %d", testcase_name, i)

                test_runtime_secs = 10
                time.sleep(test_runtime_secs)
                after_stats = get_client_accumulated_stats()

                success = True
                for status, pct in list(expected_results.items()):
                    rpc = "UNARY_CALL"
                    qty = (
                        after_stats.stats_per_method[rpc].result[status]
                        - before_stats.stats_per_method[rpc].result[status]
                    )
                    want = pct * args.qps * test_runtime_secs
                    # Allow 10% deviation from expectation to reduce flakiness
                    VARIANCE_ALLOWED = 0.1
                    if abs(qty - want) > want * VARIANCE_ALLOWED:
                        logger.info(
                            "%s: failed due to %s[%s]: got %d want ~%d",
                            testcase_name,
                            rpc,
                            status,
                            qty,
                            want,
                        )
                        success = False
                if success:
                    logger.info("success")
                    break
                logger.info("%s attempt %d failed", testcase_name, i)
                before_stats = after_stats
            else:
                raise Exception(
                    "%s: timeout waiting for expected results: %s; got %s"
                    % (
                        testcase_name,
                        expected_results,
                        after_stats.stats_per_method,
                    )
                )
    except Exception:
        passed = False
        raise
    finally:
        if passed or not args.halt_after_fail:
            patch_url_map_backend_service(gcp, original_backend_service)
            set_validate_for_proxyless(gcp, True)


def test_csds(gcp, original_backend_service, instance_group, server_uri):
    test_csds_timeout_s = datetime.timedelta(minutes=5).total_seconds()
    sleep_interval_between_attempts_s = datetime.timedelta(
        seconds=2
    ).total_seconds()
    logger.info("Running test_csds")

    logger.info("waiting for original backends to become healthy")
    wait_for_healthy_backends(gcp, original_backend_service, instance_group)

    # Test case timeout: 5 minutes
    deadline = time.time() + test_csds_timeout_s
    cnt = 0
    while time.time() <= deadline:
        client_config = get_client_xds_config_dump()
        logger.info(
            "test_csds attempt %d: received xDS config %s",
            cnt,
            json.dumps(client_config, indent=2),
        )
        if client_config is not None:
            # Got the xDS config dump, now validate it
            ok = True
            try:
                if client_config["node"]["locality"]["zone"] != args.zone:
                    logger.info(
                        "Invalid zone %s != %s",
                        client_config["node"]["locality"]["zone"],
                        args.zone,
                    )
                    ok = False
                seen = set()
                for xds_config in client_config.get("xds_config", []):
                    if "listener_config" in xds_config:
                        listener_name = xds_config["listener_config"][
                            "dynamic_listeners"
                        ][0]["active_state"]["listener"]["name"]
                        if listener_name != server_uri:
                            logger.info(
                                "Invalid Listener name %s != %s",
                                listener_name,
                                server_uri,
                            )
                            ok = False
                        else:
                            seen.add("lds")
                    elif "route_config" in xds_config:
                        num_vh = len(
                            xds_config["route_config"]["dynamic_route_configs"][
                                0
                            ]["route_config"]["virtual_hosts"]
                        )
                        if num_vh <= 0:
                            logger.info(
                                "Invalid number of VirtualHosts %s", num_vh
                            )
                            ok = False
                        else:
                            seen.add("rds")
                    elif "cluster_config" in xds_config:
                        cluster_type = xds_config["cluster_config"][
                            "dynamic_active_clusters"
                        ][0]["cluster"]["type"]
                        if cluster_type != "EDS":
                            logger.info(
                                "Invalid cluster type %s != EDS", cluster_type
                            )
                            ok = False
                        else:
                            seen.add("cds")
                    elif "endpoint_config" in xds_config:
                        sub_zone = xds_config["endpoint_config"][
                            "dynamic_endpoint_configs"
                        ][0]["endpoint_config"]["endpoints"][0]["locality"][
                            "sub_zone"
                        ]
                        if args.zone not in sub_zone:
                            logger.info(
                                "Invalid endpoint sub_zone %s", sub_zone
                            )
                            ok = False
                        else:
                            seen.add("eds")
                for generic_xds_config in client_config.get(
                    "generic_xds_configs", []
                ):
                    if re.search(
                        r"\.Listener$", generic_xds_config["type_url"]
                    ):
                        seen.add("lds")
                        listener = generic_xds_config["xds_config"]
                        if listener["name"] != server_uri:
                            logger.info(
                                "Invalid Listener name %s != %s",
                                listener_name,
                                server_uri,
                            )
                            ok = False
                    elif re.search(
                        r"\.RouteConfiguration$", generic_xds_config["type_url"]
                    ):
                        seen.add("rds")
                        route_config = generic_xds_config["xds_config"]
                        if not len(route_config["virtual_hosts"]):
                            logger.info(
                                "Invalid number of VirtualHosts %s", num_vh
                            )
                            ok = False
                    elif re.search(
                        r"\.Cluster$", generic_xds_config["type_url"]
                    ):
                        seen.add("cds")
                        cluster = generic_xds_config["xds_config"]
                        if cluster["type"] != "EDS":
                            logger.info(
                                "Invalid cluster type %s != EDS", cluster_type
                            )
                            ok = False
                    elif re.search(
                        r"\.ClusterLoadAssignment$",
                        generic_xds_config["type_url"],
                    ):
                        seen.add("eds")
                        endpoint = generic_xds_config["xds_config"]
                        if (
                            args.zone
                            not in endpoint["endpoints"][0]["locality"][
                                "sub_zone"
                            ]
                        ):
                            logger.info(
                                "Invalid endpoint sub_zone %s", sub_zone
                            )
                            ok = False
                want = {"lds", "rds", "cds", "eds"}
                if seen != want:
                    logger.info("Incomplete xDS config dump, seen=%s", seen)
                    ok = False
            except:
                logger.exception("Error in xDS config dump:")
                ok = False
            finally:
                if ok:
                    # Successfully fetched xDS config, and they looks good.
                    logger.info("success")
                    return
        logger.info("test_csds attempt %d failed", cnt)
        # Give the client some time to fetch xDS resources
        time.sleep(sleep_interval_between_attempts_s)
        cnt += 1

    raise RuntimeError(
        "failed to receive a valid xDS config in %s seconds"
        % test_csds_timeout_s
    )


def set_validate_for_proxyless(gcp, validate_for_proxyless):
    if not gcp.alpha_compute:
        logger.debug(
            "Not setting validateForProxy because alpha is not enabled"
        )
        return
    if (
        len(gcp.global_forwarding_rules) != 1
        or len(gcp.target_proxies) != 1
        or len(gcp.url_maps) != 1
    ):
        logger.debug(
            "Global forwarding rule, target proxy or url map not found."
        )
        return
    # This function deletes global_forwarding_rule and target_proxy, then
    # recreate target_proxy with validateForProxyless=False. This is necessary
    # because patching target_grpc_proxy isn't supported.
    delete_global_forwarding_rule(gcp, gcp.global_forwarding_rules[0])
    delete_target_proxy(gcp, gcp.target_proxies[0])
    create_target_proxy(gcp, target_proxy_name, validate_for_proxyless)
    create_global_forwarding_rule(gcp, forwarding_rule_name, [gcp.service_port])


def get_serving_status(instance, service_port):
    with grpc.insecure_channel("%s:%d" % (instance, service_port)) as channel:
        health_stub = health_pb2_grpc.HealthStub(channel)
        return health_stub.Check(health_pb2.HealthCheckRequest())


def set_serving_status(instances, service_port, serving):
    logger.info("setting %s serving status to %s", instances, serving)
    for instance in instances:
        with grpc.insecure_channel(
            "%s:%d" % (instance, service_port)
        ) as channel:
            logger.info("setting %s serving status to %s", instance, serving)
            stub = test_pb2_grpc.XdsUpdateHealthServiceStub(channel)
            retry_count = 5
            for i in range(5):
                if serving:
                    stub.SetServing(empty_pb2.Empty())
                else:
                    stub.SetNotServing(empty_pb2.Empty())
                serving_status = get_serving_status(instance, service_port)
                logger.info("got instance service status %s", serving_status)
                want_status = (
                    health_pb2.HealthCheckResponse.SERVING
                    if serving
                    else health_pb2.HealthCheckResponse.NOT_SERVING
                )
                if serving_status.status == want_status:
                    break
                if i == retry_count - 1:
                    raise Exception(
                        "failed to set instance service status after %d retries"
                        % retry_count
                    )


def is_primary_instance_group(gcp, instance_group):
    # Clients may connect to a TD instance in a different region than the
    # client, in which case primary/secondary assignments may not be based on
    # the client's actual locality.
    instance_names = get_instance_names(gcp, instance_group)
    stats = get_client_stats(_NUM_TEST_RPCS, _WAIT_FOR_STATS_SEC)
    return all(
        peer in instance_names for peer in list(stats.rpcs_by_peer.keys())
    )


def get_startup_script(path_to_server_binary, service_port):
    if path_to_server_binary:
        return "nohup %s --port=%d 1>/dev/null &" % (
            path_to_server_binary,
            service_port,
        )
    else:
        return (
            """#!/bin/bash
sudo apt update
sudo apt install -y git default-jdk
mkdir java_server
pushd java_server
git clone https://github.com/grpc/grpc-java.git
pushd grpc-java
pushd interop-testing
../gradlew installDist -x test -PskipCodegen=true -PskipAndroid=true

nohup build/install/grpc-interop-testing/bin/xds-test-server \
    --port=%d 1>/dev/null &"""
            % service_port
        )


def create_instance_template(
    gcp, name, network, source_image, machine_type, startup_script
):
    config = {
        "name": name,
        "properties": {
            "tags": {"items": ["allow-health-checks"]},
            "machineType": machine_type,
            "serviceAccounts": [
                {
                    "email": "default",
                    "scopes": [
                        "https://www.googleapis.com/auth/cloud-platform",
                    ],
                }
            ],
            "networkInterfaces": [
                {
                    "accessConfigs": [{"type": "ONE_TO_ONE_NAT"}],
                    "network": network,
                }
            ],
            "disks": [
                {
                    "boot": True,
                    "initializeParams": {"sourceImage": source_image},
                    "autoDelete": True,
                }
            ],
            "metadata": {
                "items": [{"key": "startup-script", "value": startup_script}]
            },
        },
    }

    logger.debug("Sending GCP request with body=%s", config)
    result = (
        gcp.compute.instanceTemplates()
        .insert(project=gcp.project, body=config)
        .execute(num_retries=_GCP_API_RETRIES)
    )
    wait_for_global_operation(gcp, result["name"])
    gcp.instance_template = GcpResource(config["name"], result["targetLink"])


def add_instance_group(gcp, zone, name, size):
    config = {
        "name": name,
        "instanceTemplate": gcp.instance_template.url,
        "targetSize": size,
        "namedPorts": [{"name": "grpc", "port": gcp.service_port}],
    }

    logger.debug("Sending GCP request with body=%s", config)
    result = (
        gcp.compute.instanceGroupManagers()
        .insert(project=gcp.project, zone=zone, body=config)
        .execute(num_retries=_GCP_API_RETRIES)
    )
    wait_for_zone_operation(gcp, zone, result["name"])
    result = (
        gcp.compute.instanceGroupManagers()
        .get(
            project=gcp.project, zone=zone, instanceGroupManager=config["name"]
        )
        .execute(num_retries=_GCP_API_RETRIES)
    )
    instance_group = InstanceGroup(
        config["name"], result["instanceGroup"], zone
    )
    gcp.instance_groups.append(instance_group)
    wait_for_instance_group_to_reach_expected_size(
        gcp, instance_group, size, _WAIT_FOR_OPERATION_SEC
    )
    return instance_group


def create_health_check(gcp, name):
    if gcp.alpha_compute:
        config = {
            "name": name,
            "type": "GRPC",
            "grpcHealthCheck": {"portSpecification": "USE_SERVING_PORT"},
        }
        compute_to_use = gcp.alpha_compute
    else:
        config = {
            "name": name,
            "type": "TCP",
            "tcpHealthCheck": {"portName": "grpc"},
        }
        compute_to_use = gcp.compute
    logger.debug("Sending GCP request with body=%s", config)
    result = (
        compute_to_use.healthChecks()
        .insert(project=gcp.project, body=config)
        .execute(num_retries=_GCP_API_RETRIES)
    )
    wait_for_global_operation(gcp, result["name"])
    gcp.health_check = GcpResource(config["name"], result["targetLink"])


def create_health_check_firewall_rule(gcp, name):
    config = {
        "name": name,
        "direction": "INGRESS",
        "allowed": [{"IPProtocol": "tcp"}],
        "sourceRanges": ["35.191.0.0/16", "130.211.0.0/22"],
        "targetTags": ["allow-health-checks"],
    }
    logger.debug("Sending GCP request with body=%s", config)
    result = (
        gcp.compute.firewalls()
        .insert(project=gcp.project, body=config)
        .execute(num_retries=_GCP_API_RETRIES)
    )
    wait_for_global_operation(gcp, result["name"])
    gcp.health_check_firewall_rule = GcpResource(
        config["name"], result["targetLink"]
    )


def add_backend_service(gcp, name):
    if gcp.alpha_compute:
        protocol = "GRPC"
        compute_to_use = gcp.alpha_compute
    else:
        protocol = "HTTP2"
        compute_to_use = gcp.compute
    config = {
        "name": name,
        "loadBalancingScheme": "INTERNAL_SELF_MANAGED",
        "healthChecks": [gcp.health_check.url],
        "portName": "grpc",
        "protocol": protocol,
    }
    logger.debug("Sending GCP request with body=%s", config)
    result = (
        compute_to_use.backendServices()
        .insert(project=gcp.project, body=config)
        .execute(num_retries=_GCP_API_RETRIES)
    )
    wait_for_global_operation(gcp, result["name"])
    backend_service = GcpResource(config["name"], result["targetLink"])
    gcp.backend_services.append(backend_service)
    return backend_service


def create_url_map(gcp, name, backend_service, host_name):
    config = {
        "name": name,
        "defaultService": backend_service.url,
        "pathMatchers": [
            {
                "name": _PATH_MATCHER_NAME,
                "defaultService": backend_service.url,
            }
        ],
        "hostRules": [
            {"hosts": [host_name], "pathMatcher": _PATH_MATCHER_NAME}
        ],
    }
    logger.debug("Sending GCP request with body=%s", config)
    result = (
        gcp.compute.urlMaps()
        .insert(project=gcp.project, body=config)
        .execute(num_retries=_GCP_API_RETRIES)
    )
    wait_for_global_operation(gcp, result["name"])
    url_map = GcpResource(config["name"], result["targetLink"])
    gcp.url_maps.append(url_map)
    return url_map


def patch_url_map_host_rule_with_port(gcp, name, backend_service, host_name):
    config = {
        "hostRules": [
            {
                "hosts": ["%s:%d" % (host_name, gcp.service_port)],
                "pathMatcher": _PATH_MATCHER_NAME,
            }
        ]
    }
    logger.debug("Sending GCP request with body=%s", config)
    result = (
        gcp.compute.urlMaps()
        .patch(project=gcp.project, urlMap=name, body=config)
        .execute(num_retries=_GCP_API_RETRIES)
    )
    wait_for_global_operation(gcp, result["name"])


def create_target_proxy(gcp, name, validate_for_proxyless=True, url_map=None):
    if url_map:
        arg_url_map_url = url_map.url
    else:
        arg_url_map_url = gcp.url_maps[0].url
    if gcp.alpha_compute:
        config = {
            "name": name,
            "url_map": arg_url_map_url,
            "validate_for_proxyless": validate_for_proxyless,
        }
        logger.debug("Sending GCP request with body=%s", config)
        result = (
            gcp.alpha_compute.targetGrpcProxies()
            .insert(project=gcp.project, body=config)
            .execute(num_retries=_GCP_API_RETRIES)
        )
    else:
        config = {
            "name": name,
            "url_map": arg_url_map_url,
        }
        logger.debug("Sending GCP request with body=%s", config)
        result = (
            gcp.compute.targetHttpProxies()
            .insert(project=gcp.project, body=config)
            .execute(num_retries=_GCP_API_RETRIES)
        )
    wait_for_global_operation(gcp, result["name"])
    target_proxy = GcpResource(config["name"], result["targetLink"])
    gcp.target_proxies.append(target_proxy)
    return target_proxy


def create_global_forwarding_rule(
    gcp,
    name,
    potential_ports,
    potential_ip_addresses=["0.0.0.0"],
    target_proxy=None,
):
    if target_proxy:
        arg_target_proxy_url = target_proxy.url
    else:
        arg_target_proxy_url = gcp.target_proxies[0].url
    if gcp.alpha_compute:
        compute_to_use = gcp.alpha_compute
    else:
        compute_to_use = gcp.compute
    for port in potential_ports:
        for ip_address in potential_ip_addresses:
            try:
                config = {
                    "name": name,
                    "loadBalancingScheme": "INTERNAL_SELF_MANAGED",
                    "portRange": str(port),
                    "IPAddress": ip_address,
                    "network": args.network,
                    "target": arg_target_proxy_url,
                }
                logger.debug("Sending GCP request with body=%s", config)
                result = (
                    compute_to_use.globalForwardingRules()
                    .insert(project=gcp.project, body=config)
                    .execute(num_retries=_GCP_API_RETRIES)
                )
                wait_for_global_operation(gcp, result["name"])
                global_forwarding_rule = GcpResource(
                    config["name"], result["targetLink"]
                )
                gcp.global_forwarding_rules.append(global_forwarding_rule)
                gcp.service_port = port
                return
            except googleapiclient.errors.HttpError as http_error:
                logger.warning(
                    "Got error %s when attempting to create forwarding rule to "
                    "%s:%d. Retrying with another port."
                    % (http_error, ip_address, port)
                )


def get_health_check(gcp, health_check_name):
    try:
        result = (
            gcp.compute.healthChecks()
            .get(project=gcp.project, healthCheck=health_check_name)
            .execute()
        )
        gcp.health_check = GcpResource(health_check_name, result["selfLink"])
    except Exception as e:
        gcp.errors.append(e)
        gcp.health_check = GcpResource(health_check_name, None)


def get_health_check_firewall_rule(gcp, firewall_name):
    try:
        result = (
            gcp.compute.firewalls()
            .get(project=gcp.project, firewall=firewall_name)
            .execute()
        )
        gcp.health_check_firewall_rule = GcpResource(
            firewall_name, result["selfLink"]
        )
    except Exception as e:
        gcp.errors.append(e)
        gcp.health_check_firewall_rule = GcpResource(firewall_name, None)


def get_backend_service(gcp, backend_service_name, record_error=True):
    try:
        result = (
            gcp.compute.backendServices()
            .get(project=gcp.project, backendService=backend_service_name)
            .execute()
        )
        backend_service = GcpResource(backend_service_name, result["selfLink"])
    except Exception as e:
        if record_error:
            gcp.errors.append(e)
        backend_service = GcpResource(backend_service_name, None)
    gcp.backend_services.append(backend_service)
    return backend_service


def get_url_map(gcp, url_map_name, record_error=True):
    try:
        result = (
            gcp.compute.urlMaps()
            .get(project=gcp.project, urlMap=url_map_name)
            .execute()
        )
        url_map = GcpResource(url_map_name, result["selfLink"])
        gcp.url_maps.append(url_map)
    except Exception as e:
        if record_error:
            gcp.errors.append(e)


def get_target_proxy(gcp, target_proxy_name, record_error=True):
    try:
        if gcp.alpha_compute:
            result = (
                gcp.alpha_compute.targetGrpcProxies()
                .get(project=gcp.project, targetGrpcProxy=target_proxy_name)
                .execute()
            )
        else:
            result = (
                gcp.compute.targetHttpProxies()
                .get(project=gcp.project, targetHttpProxy=target_proxy_name)
                .execute()
            )
        target_proxy = GcpResource(target_proxy_name, result["selfLink"])
        gcp.target_proxies.append(target_proxy)
    except Exception as e:
        if record_error:
            gcp.errors.append(e)


def get_global_forwarding_rule(gcp, forwarding_rule_name, record_error=True):
    try:
        result = (
            gcp.compute.globalForwardingRules()
            .get(project=gcp.project, forwardingRule=forwarding_rule_name)
            .execute()
        )
        global_forwarding_rule = GcpResource(
            forwarding_rule_name, result["selfLink"]
        )
        gcp.global_forwarding_rules.append(global_forwarding_rule)
    except Exception as e:
        if record_error:
            gcp.errors.append(e)


def get_instance_template(gcp, template_name):
    try:
        result = (
            gcp.compute.instanceTemplates()
            .get(project=gcp.project, instanceTemplate=template_name)
            .execute()
        )
        gcp.instance_template = GcpResource(template_name, result["selfLink"])
    except Exception as e:
        gcp.errors.append(e)
        gcp.instance_template = GcpResource(template_name, None)


def get_instance_group(gcp, zone, instance_group_name):
    try:
        result = (
            gcp.compute.instanceGroups()
            .get(
                project=gcp.project,
                zone=zone,
                instanceGroup=instance_group_name,
            )
            .execute()
        )
        gcp.service_port = result["namedPorts"][0]["port"]
        instance_group = InstanceGroup(
            instance_group_name, result["selfLink"], zone
        )
    except Exception as e:
        gcp.errors.append(e)
        instance_group = InstanceGroup(instance_group_name, None, zone)
    gcp.instance_groups.append(instance_group)
    return instance_group


def delete_global_forwarding_rule(gcp, forwarding_rule_to_delete=None):
    if not forwarding_rule_to_delete:
        return
    try:
        logger.debug(
            "Deleting forwarding rule %s", forwarding_rule_to_delete.name
        )
        result = (
            gcp.compute.globalForwardingRules()
            .delete(
                project=gcp.project,
                forwardingRule=forwarding_rule_to_delete.name,
            )
            .execute(num_retries=_GCP_API_RETRIES)
        )
        wait_for_global_operation(gcp, result["name"])
        if forwarding_rule_to_delete in gcp.global_forwarding_rules:
            gcp.global_forwarding_rules.remove(forwarding_rule_to_delete)
        else:
            logger.debug(
                (
                    "Forwarding rule %s does not exist in"
                    " gcp.global_forwarding_rules"
                ),
                forwarding_rule_to_delete.name,
            )
    except googleapiclient.errors.HttpError as http_error:
        logger.info("Delete failed: %s", http_error)


def delete_global_forwarding_rules(gcp):
    forwarding_rules_to_delete = gcp.global_forwarding_rules.copy()
    for forwarding_rule in forwarding_rules_to_delete:
        delete_global_forwarding_rule(gcp, forwarding_rule)


def delete_target_proxy(gcp, proxy_to_delete=None):
    if not proxy_to_delete:
        return
    try:
        if gcp.alpha_compute:
            logger.debug("Deleting grpc proxy %s", proxy_to_delete.name)
            result = (
                gcp.alpha_compute.targetGrpcProxies()
                .delete(
                    project=gcp.project, targetGrpcProxy=proxy_to_delete.name
                )
                .execute(num_retries=_GCP_API_RETRIES)
            )
        else:
            logger.debug("Deleting http proxy %s", proxy_to_delete.name)
            result = (
                gcp.compute.targetHttpProxies()
                .delete(
                    project=gcp.project, targetHttpProxy=proxy_to_delete.name
                )
                .execute(num_retries=_GCP_API_RETRIES)
            )
        wait_for_global_operation(gcp, result["name"])
        if proxy_to_delete in gcp.target_proxies:
            gcp.target_proxies.remove(proxy_to_delete)
        else:
            logger.debug(
                "Gcp proxy %s does not exist in gcp.target_proxies",
                proxy_to_delete.name,
            )
    except googleapiclient.errors.HttpError as http_error:
        logger.info("Delete failed: %s", http_error)


def delete_target_proxies(gcp):
    target_proxies_to_delete = gcp.target_proxies.copy()
    for target_proxy in target_proxies_to_delete:
        delete_target_proxy(gcp, target_proxy)


def delete_url_map(gcp, url_map_to_delete=None):
    if not url_map_to_delete:
        return
    try:
        logger.debug("Deleting url map %s", url_map_to_delete.name)
        result = (
            gcp.compute.urlMaps()
            .delete(project=gcp.project, urlMap=url_map_to_delete.name)
            .execute(num_retries=_GCP_API_RETRIES)
        )
        wait_for_global_operation(gcp, result["name"])
        if url_map_to_delete in gcp.url_maps:
            gcp.url_maps.remove(url_map_to_delete)
        else:
            logger.debug(
                "Url map %s does not exist in gcp.url_maps",
                url_map_to_delete.name,
            )
    except googleapiclient.errors.HttpError as http_error:
        logger.info("Delete failed: %s", http_error)


def delete_url_maps(gcp):
    url_maps_to_delete = gcp.url_maps.copy()
    for url_map in url_maps_to_delete:
        delete_url_map(gcp, url_map)


def delete_backend_service(gcp, backend_service):
    try:
        logger.debug("Deleting backend service %s", backend_service.name)
        result = (
            gcp.compute.backendServices()
            .delete(project=gcp.project, backendService=backend_service.name)
            .execute(num_retries=_GCP_API_RETRIES)
        )
        wait_for_global_operation(gcp, result["name"])
    except googleapiclient.errors.HttpError as http_error:
        logger.info("Delete failed: %s", http_error)


def delete_backend_services(gcp):
    for backend_service in gcp.backend_services:
        delete_backend_service(gcp, backend_service)


def delete_firewall(gcp):
    try:
        logger.debug(
            "Deleting firewall %s", gcp.health_check_firewall_rule.name
        )
        result = (
            gcp.compute.firewalls()
            .delete(
                project=gcp.project,
                firewall=gcp.health_check_firewall_rule.name,
            )
            .execute(num_retries=_GCP_API_RETRIES)
        )
        wait_for_global_operation(gcp, result["name"])
    except googleapiclient.errors.HttpError as http_error:
        logger.info("Delete failed: %s", http_error)


def delete_health_check(gcp):
    try:
        logger.debug("Deleting health check %s", gcp.health_check.name)
        result = (
            gcp.compute.healthChecks()
            .delete(project=gcp.project, healthCheck=gcp.health_check.name)
            .execute(num_retries=_GCP_API_RETRIES)
        )
        wait_for_global_operation(gcp, result["name"])
    except googleapiclient.errors.HttpError as http_error:
        logger.info("Delete failed: %s", http_error)


def delete_instance_groups(gcp):
    for instance_group in gcp.instance_groups:
        try:
            logger.debug(
                "Deleting instance group %s %s",
                instance_group.name,
                instance_group.zone,
            )
            result = (
                gcp.compute.instanceGroupManagers()
                .delete(
                    project=gcp.project,
                    zone=instance_group.zone,
                    instanceGroupManager=instance_group.name,
                )
                .execute(num_retries=_GCP_API_RETRIES)
            )
            wait_for_zone_operation(
                gcp,
                instance_group.zone,
                result["name"],
                timeout_sec=_WAIT_FOR_BACKEND_SEC,
            )
        except googleapiclient.errors.HttpError as http_error:
            logger.info("Delete failed: %s", http_error)


def delete_instance_template(gcp):
    try:
        logger.debug(
            "Deleting instance template %s", gcp.instance_template.name
        )
        result = (
            gcp.compute.instanceTemplates()
            .delete(
                project=gcp.project, instanceTemplate=gcp.instance_template.name
            )
            .execute(num_retries=_GCP_API_RETRIES)
        )
        wait_for_global_operation(gcp, result["name"])
    except googleapiclient.errors.HttpError as http_error:
        logger.info("Delete failed: %s", http_error)


def patch_backend_service(
    gcp,
    backend_service,
    instance_groups,
    balancing_mode="UTILIZATION",
    max_rate=1,
    circuit_breakers=None,
):
    if gcp.alpha_compute:
        compute_to_use = gcp.alpha_compute
    else:
        compute_to_use = gcp.compute
    config = {
        "backends": [
            {
                "group": instance_group.url,
                "balancingMode": balancing_mode,
                "maxRate": max_rate if balancing_mode == "RATE" else None,
            }
            for instance_group in instance_groups
        ],
        "circuitBreakers": circuit_breakers,
    }
    logger.debug("Sending GCP request with body=%s", config)
    result = (
        compute_to_use.backendServices()
        .patch(
            project=gcp.project,
            backendService=backend_service.name,
            body=config,
        )
        .execute(num_retries=_GCP_API_RETRIES)
    )
    wait_for_global_operation(
        gcp, result["name"], timeout_sec=_WAIT_FOR_BACKEND_SEC
    )


def resize_instance_group(
    gcp, instance_group, new_size, timeout_sec=_WAIT_FOR_OPERATION_SEC
):
    result = (
        gcp.compute.instanceGroupManagers()
        .resize(
            project=gcp.project,
            zone=instance_group.zone,
            instanceGroupManager=instance_group.name,
            size=new_size,
        )
        .execute(num_retries=_GCP_API_RETRIES)
    )
    wait_for_zone_operation(
        gcp, instance_group.zone, result["name"], timeout_sec=360
    )
    wait_for_instance_group_to_reach_expected_size(
        gcp, instance_group, new_size, timeout_sec
    )


def patch_url_map_backend_service(
    gcp,
    backend_service=None,
    services_with_weights=None,
    route_rules=None,
    url_map=None,
):
    if url_map:
        url_map_name = url_map.name
    else:
        url_map_name = gcp.url_maps[0].name
    """change url_map's backend service

    Only one of backend_service and service_with_weights can be not None.
    """
    if gcp.alpha_compute:
        compute_to_use = gcp.alpha_compute
    else:
        compute_to_use = gcp.compute

    if backend_service and services_with_weights:
        raise ValueError(
            "both backend_service and service_with_weights are not None."
        )

    default_service = backend_service.url if backend_service else None
    default_route_action = (
        {
            "weightedBackendServices": [
                {
                    "backendService": service.url,
                    "weight": w,
                }
                for service, w in list(services_with_weights.items())
            ]
        }
        if services_with_weights
        else None
    )

    config = {
        "pathMatchers": [
            {
                "name": _PATH_MATCHER_NAME,
                "defaultService": default_service,
                "defaultRouteAction": default_route_action,
                "routeRules": route_rules,
            }
        ]
    }
    logger.debug("Sending GCP request with body=%s", config)
    result = (
        compute_to_use.urlMaps()
        .patch(project=gcp.project, urlMap=url_map_name, body=config)
        .execute(num_retries=_GCP_API_RETRIES)
    )
    wait_for_global_operation(gcp, result["name"])


def wait_for_instance_group_to_reach_expected_size(
    gcp, instance_group, expected_size, timeout_sec
):
    start_time = time.time()
    while True:
        current_size = len(get_instance_names(gcp, instance_group))
        if current_size == expected_size:
            break
        if time.time() - start_time > timeout_sec:
            raise Exception(
                "Instance group had expected size %d but actual size %d"
                % (expected_size, current_size)
            )
        time.sleep(2)


def wait_for_global_operation(
    gcp, operation, timeout_sec=_WAIT_FOR_OPERATION_SEC
):
    start_time = time.time()
    while time.time() - start_time <= timeout_sec:
        result = (
            gcp.compute.globalOperations()
            .get(project=gcp.project, operation=operation)
            .execute(num_retries=_GCP_API_RETRIES)
        )
        if result["status"] == "DONE":
            if "error" in result:
                raise Exception(result["error"])
            return
        time.sleep(2)
    raise Exception(
        "Operation %s did not complete within %d" % (operation, timeout_sec)
    )


def wait_for_zone_operation(
    gcp, zone, operation, timeout_sec=_WAIT_FOR_OPERATION_SEC
):
    start_time = time.time()
    while time.time() - start_time <= timeout_sec:
        result = (
            gcp.compute.zoneOperations()
            .get(project=gcp.project, zone=zone, operation=operation)
            .execute(num_retries=_GCP_API_RETRIES)
        )
        if result["status"] == "DONE":
            if "error" in result:
                raise Exception(result["error"])
            return
        time.sleep(2)
    raise Exception(
        "Operation %s did not complete within %d" % (operation, timeout_sec)
    )


def wait_for_healthy_backends(
    gcp, backend_service, instance_group, timeout_sec=_WAIT_FOR_BACKEND_SEC
):
    start_time = time.time()
    config = {"group": instance_group.url}
    instance_names = get_instance_names(gcp, instance_group)
    expected_size = len(instance_names)
    while time.time() - start_time <= timeout_sec:
        for instance_name in instance_names:
            try:
                status = get_serving_status(instance_name, gcp.service_port)
                logger.info(
                    "serving status response from %s: %s", instance_name, status
                )
            except grpc.RpcError as rpc_error:
                logger.info(
                    "checking serving status of %s failed: %s",
                    instance_name,
                    rpc_error,
                )
        result = (
            gcp.compute.backendServices()
            .getHealth(
                project=gcp.project,
                backendService=backend_service.name,
                body=config,
            )
            .execute(num_retries=_GCP_API_RETRIES)
        )
        if "healthStatus" in result:
            logger.info("received GCP healthStatus: %s", result["healthStatus"])
            healthy = True
            for instance in result["healthStatus"]:
                if instance["healthState"] != "HEALTHY":
                    healthy = False
                    break
            if healthy and expected_size == len(result["healthStatus"]):
                return
        else:
            logger.info("no healthStatus received from GCP")
        time.sleep(5)
    raise Exception(
        "Not all backends became healthy within %d seconds: %s"
        % (timeout_sec, result)
    )


def get_instance_names(gcp, instance_group):
    instance_names = []
    result = (
        gcp.compute.instanceGroups()
        .listInstances(
            project=gcp.project,
            zone=instance_group.zone,
            instanceGroup=instance_group.name,
            body={"instanceState": "ALL"},
        )
        .execute(num_retries=_GCP_API_RETRIES)
    )
    if "items" not in result:
        return []
    for item in result["items"]:
        # listInstances() returns the full URL of the instance, which ends with
        # the instance name. compute.instances().get() requires using the
        # instance name (not the full URL) to look up instance details, so we
        # just extract the name manually.
        instance_name = item["instance"].split("/")[-1]
        instance_names.append(instance_name)
    logger.info("retrieved instance names: %s", instance_names)
    return instance_names


def clean_up(gcp):
    delete_global_forwarding_rules(gcp)
    delete_target_proxies(gcp)
    delete_url_maps(gcp)
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
    def __init__(self, compute, alpha_compute, project, project_num):
        self.compute = compute
        self.alpha_compute = alpha_compute
        self.project = project
        self.project_num = project_num
        self.health_check = None
        self.health_check_firewall_rule = None
        self.backend_services = []
        self.url_maps = []
        self.target_proxies = []
        self.global_forwarding_rules = []
        self.service_port = None
        self.instance_template = None
        self.instance_groups = []
        self.errors = []


logging.debug(
    "script start time: %s",
    datetime.datetime.now(datetime.timezone.utc)
    .astimezone()
    .strftime("%Y-%m-%dT%H:%M:%S %Z"),
)
logging.debug(
    "logging local timezone: %s",
    datetime.datetime.now(datetime.timezone.utc).astimezone().tzinfo,
)
alpha_compute = None
if args.compute_discovery_document:
    with open(args.compute_discovery_document, "r") as discovery_doc:
        compute = googleapiclient.discovery.build_from_document(
            discovery_doc.read()
        )
    if not args.only_stable_gcp_apis and args.alpha_compute_discovery_document:
        with open(args.alpha_compute_discovery_document, "r") as discovery_doc:
            alpha_compute = googleapiclient.discovery.build_from_document(
                discovery_doc.read()
            )
else:
    compute = googleapiclient.discovery.build("compute", "v1")
    if not args.only_stable_gcp_apis:
        alpha_compute = googleapiclient.discovery.build("compute", "alpha")

test_results = {}
failed_tests = []
try:
    gcp = GcpState(compute, alpha_compute, args.project_id, args.project_num)
    gcp_suffix = args.gcp_suffix
    health_check_name = _BASE_HEALTH_CHECK_NAME + gcp_suffix
    if not args.use_existing_gcp_resources:
        if args.keep_gcp_resources:
            # Auto-generating a unique suffix in case of conflict should not be
            # combined with --keep_gcp_resources, as the suffix actually used
            # for GCP resources will not match the provided --gcp_suffix value.
            num_attempts = 1
        else:
            num_attempts = 5
        for i in range(num_attempts):
            try:
                logger.info("Using GCP suffix %s", gcp_suffix)
                create_health_check(gcp, health_check_name)
                break
            except googleapiclient.errors.HttpError as http_error:
                gcp_suffix = "%s-%04d" % (gcp_suffix, random.randint(0, 9999))
                health_check_name = _BASE_HEALTH_CHECK_NAME + gcp_suffix
                logger.exception("HttpError when creating health check")
        if gcp.health_check is None:
            raise Exception(
                "Failed to create health check name after %d attempts"
                % num_attempts
            )
    firewall_name = _BASE_FIREWALL_RULE_NAME + gcp_suffix
    backend_service_name = _BASE_BACKEND_SERVICE_NAME + gcp_suffix
    alternate_backend_service_name = (
        _BASE_BACKEND_SERVICE_NAME + "-alternate" + gcp_suffix
    )
    extra_backend_service_name = (
        _BASE_BACKEND_SERVICE_NAME + "-extra" + gcp_suffix
    )
    more_extra_backend_service_name = (
        _BASE_BACKEND_SERVICE_NAME + "-more-extra" + gcp_suffix
    )
    url_map_name = _BASE_URL_MAP_NAME + gcp_suffix
    url_map_name_2 = url_map_name + "2"
    service_host_name = _BASE_SERVICE_HOST + gcp_suffix
    target_proxy_name = _BASE_TARGET_PROXY_NAME + gcp_suffix
    target_proxy_name_2 = target_proxy_name + "2"
    forwarding_rule_name = _BASE_FORWARDING_RULE_NAME + gcp_suffix
    forwarding_rule_name_2 = forwarding_rule_name + "2"
    template_name = _BASE_TEMPLATE_NAME + gcp_suffix
    instance_group_name = _BASE_INSTANCE_GROUP_NAME + gcp_suffix
    same_zone_instance_group_name = (
        _BASE_INSTANCE_GROUP_NAME + "-same-zone" + gcp_suffix
    )
    secondary_zone_instance_group_name = (
        _BASE_INSTANCE_GROUP_NAME + "-secondary-zone" + gcp_suffix
    )
    potential_service_ports = list(args.service_port_range)
    random.shuffle(potential_service_ports)
    if args.use_existing_gcp_resources:
        logger.info("Reusing existing GCP resources")
        get_health_check(gcp, health_check_name)
        get_health_check_firewall_rule(gcp, firewall_name)
        backend_service = get_backend_service(gcp, backend_service_name)
        alternate_backend_service = get_backend_service(
            gcp, alternate_backend_service_name
        )
        extra_backend_service = get_backend_service(
            gcp, extra_backend_service_name, record_error=False
        )
        more_extra_backend_service = get_backend_service(
            gcp, more_extra_backend_service_name, record_error=False
        )
        get_url_map(gcp, url_map_name)
        get_target_proxy(gcp, target_proxy_name)
        get_global_forwarding_rule(gcp, forwarding_rule_name)
        get_url_map(gcp, url_map_name_2, record_error=False)
        get_target_proxy(gcp, target_proxy_name_2, record_error=False)
        get_global_forwarding_rule(
            gcp, forwarding_rule_name_2, record_error=False
        )
        get_instance_template(gcp, template_name)
        instance_group = get_instance_group(gcp, args.zone, instance_group_name)
        same_zone_instance_group = get_instance_group(
            gcp, args.zone, same_zone_instance_group_name
        )
        secondary_zone_instance_group = get_instance_group(
            gcp, args.secondary_zone, secondary_zone_instance_group_name
        )
        if gcp.errors:
            raise Exception(gcp.errors)
    else:
        create_health_check_firewall_rule(gcp, firewall_name)
        backend_service = add_backend_service(gcp, backend_service_name)
        alternate_backend_service = add_backend_service(
            gcp, alternate_backend_service_name
        )
        create_url_map(gcp, url_map_name, backend_service, service_host_name)
        create_target_proxy(gcp, target_proxy_name)
        create_global_forwarding_rule(
            gcp, forwarding_rule_name, potential_service_ports
        )
        if not gcp.service_port:
            raise Exception(
                "Failed to find a valid ip:port for the forwarding rule"
            )
        if gcp.service_port != _DEFAULT_SERVICE_PORT:
            patch_url_map_host_rule_with_port(
                gcp, url_map_name, backend_service, service_host_name
            )
        startup_script = get_startup_script(
            args.path_to_server_binary, gcp.service_port
        )
        create_instance_template(
            gcp,
            template_name,
            args.network,
            args.source_image,
            args.machine_type,
            startup_script,
        )
        instance_group = add_instance_group(
            gcp, args.zone, instance_group_name, _INSTANCE_GROUP_SIZE
        )
        patch_backend_service(gcp, backend_service, [instance_group])
        same_zone_instance_group = add_instance_group(
            gcp, args.zone, same_zone_instance_group_name, _INSTANCE_GROUP_SIZE
        )
        secondary_zone_instance_group = add_instance_group(
            gcp,
            args.secondary_zone,
            secondary_zone_instance_group_name,
            _INSTANCE_GROUP_SIZE,
        )

    wait_for_healthy_backends(gcp, backend_service, instance_group)

    if args.test_case:
        client_env = dict(os.environ)
        if original_grpc_trace:
            client_env["GRPC_TRACE"] = original_grpc_trace
        if original_grpc_verbosity:
            client_env["GRPC_VERBOSITY"] = original_grpc_verbosity
        bootstrap_server_features = []

        if gcp.service_port == _DEFAULT_SERVICE_PORT:
            server_uri = service_host_name
        else:
            server_uri = service_host_name + ":" + str(gcp.service_port)
        if args.xds_v3_support:
            client_env["GRPC_XDS_EXPERIMENTAL_V3_SUPPORT"] = "true"
            bootstrap_server_features.append("xds_v3")
        if args.bootstrap_file:
            bootstrap_path = os.path.abspath(args.bootstrap_file)
        else:
            with tempfile.NamedTemporaryFile(delete=False) as bootstrap_file:
                bootstrap_file.write(
                    _BOOTSTRAP_TEMPLATE.format(
                        node_id="projects/%s/networks/%s/nodes/%s"
                        % (
                            gcp.project_num,
                            args.network.split("/")[-1],
                            uuid.uuid1(),
                        ),
                        server_features=json.dumps(bootstrap_server_features),
                    ).encode("utf-8")
                )
                bootstrap_path = bootstrap_file.name
        client_env["GRPC_XDS_BOOTSTRAP"] = bootstrap_path
        client_env["GRPC_XDS_EXPERIMENTAL_CIRCUIT_BREAKING"] = "true"
        client_env["GRPC_XDS_EXPERIMENTAL_ENABLE_TIMEOUT"] = "true"
        client_env["GRPC_XDS_EXPERIMENTAL_FAULT_INJECTION"] = "true"
        for test_case in args.test_case:
            if test_case in _V3_TEST_CASES and not args.xds_v3_support:
                logger.info(
                    "skipping test %s due to missing v3 support", test_case
                )
                continue
            if test_case in _ALPHA_TEST_CASES and not gcp.alpha_compute:
                logger.info(
                    "skipping test %s due to missing alpha support", test_case
                )
                continue
            if (
                test_case
                in [
                    "api_listener",
                    "forwarding_rule_port_match",
                    "forwarding_rule_default_port",
                ]
                and CLIENT_HOSTS
            ):
                logger.info(
                    (
                        "skipping test %s because test configuration is"
                        "not compatible with client processes on existing"
                        "client hosts"
                    ),
                    test_case,
                )
                continue
            if test_case == "forwarding_rule_default_port":
                server_uri = service_host_name
            result = jobset.JobResult()
            log_dir = os.path.join(_TEST_LOG_BASE_DIR, test_case)
            if not os.path.exists(log_dir):
                os.makedirs(log_dir)
            test_log_filename = os.path.join(log_dir, _SPONGE_LOG_NAME)
            test_log_file = open(test_log_filename, "w+")
            client_process = None

            if test_case in _TESTS_TO_RUN_MULTIPLE_RPCS:
                rpcs_to_send = '--rpc="UnaryCall,EmptyCall"'
            else:
                rpcs_to_send = '--rpc="UnaryCall"'

            if test_case in _TESTS_TO_SEND_METADATA:
                metadata_to_send = '--metadata="EmptyCall:{keyE}:{valueE},UnaryCall:{keyU}:{valueU},UnaryCall:{keyNU}:{valueNU}"'.format(
                    keyE=_TEST_METADATA_KEY,
                    valueE=_TEST_METADATA_VALUE_EMPTY,
                    keyU=_TEST_METADATA_KEY,
                    valueU=_TEST_METADATA_VALUE_UNARY,
                    keyNU=_TEST_METADATA_NUMERIC_KEY,
                    valueNU=_TEST_METADATA_NUMERIC_VALUE,
                )
            else:
                # Setting the arg explicitly to empty with '--metadata=""'
                # makes C# client fail
                # (see https://github.com/commandlineparser/commandline/issues/412),
                # so instead we just rely on clients using the default when
                # metadata arg is not specified.
                metadata_to_send = ""

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
            fail_on_failed_rpc = ""

            try:
                if not CLIENT_HOSTS:
                    client_cmd_formatted = args.client_cmd.format(
                        server_uri=server_uri,
                        stats_port=args.stats_port,
                        qps=args.qps,
                        fail_on_failed_rpc=fail_on_failed_rpc,
                        rpcs_to_send=rpcs_to_send,
                        metadata_to_send=metadata_to_send,
                    )
                    logger.debug("running client: %s", client_cmd_formatted)
                    client_cmd = shlex.split(client_cmd_formatted)
                    client_process = subprocess.Popen(
                        client_cmd,
                        env=client_env,
                        stderr=subprocess.STDOUT,
                        stdout=test_log_file,
                    )
                if test_case == "backends_restart":
                    test_backends_restart(gcp, backend_service, instance_group)
                elif test_case == "change_backend_service":
                    test_change_backend_service(
                        gcp,
                        backend_service,
                        instance_group,
                        alternate_backend_service,
                        same_zone_instance_group,
                    )
                elif test_case == "gentle_failover":
                    test_gentle_failover(
                        gcp,
                        backend_service,
                        instance_group,
                        secondary_zone_instance_group,
                    )
                elif test_case == "load_report_based_failover":
                    test_load_report_based_failover(
                        gcp,
                        backend_service,
                        instance_group,
                        secondary_zone_instance_group,
                    )
                elif test_case == "ping_pong":
                    test_ping_pong(gcp, backend_service, instance_group)
                elif test_case == "remove_instance_group":
                    test_remove_instance_group(
                        gcp,
                        backend_service,
                        instance_group,
                        same_zone_instance_group,
                    )
                elif test_case == "round_robin":
                    test_round_robin(gcp, backend_service, instance_group)
                elif (
                    test_case
                    == "secondary_locality_gets_no_requests_on_partial_primary_failure"
                ):
                    test_secondary_locality_gets_no_requests_on_partial_primary_failure(
                        gcp,
                        backend_service,
                        instance_group,
                        secondary_zone_instance_group,
                    )
                elif (
                    test_case
                    == "secondary_locality_gets_requests_on_primary_failure"
                ):
                    test_secondary_locality_gets_requests_on_primary_failure(
                        gcp,
                        backend_service,
                        instance_group,
                        secondary_zone_instance_group,
                    )
                elif test_case == "traffic_splitting":
                    test_traffic_splitting(
                        gcp,
                        backend_service,
                        instance_group,
                        alternate_backend_service,
                        same_zone_instance_group,
                    )
                elif test_case == "path_matching":
                    test_path_matching(
                        gcp,
                        backend_service,
                        instance_group,
                        alternate_backend_service,
                        same_zone_instance_group,
                    )
                elif test_case == "header_matching":
                    test_header_matching(
                        gcp,
                        backend_service,
                        instance_group,
                        alternate_backend_service,
                        same_zone_instance_group,
                    )
                elif test_case == "circuit_breaking":
                    test_circuit_breaking(
                        gcp,
                        backend_service,
                        instance_group,
                        same_zone_instance_group,
                    )
                elif test_case == "timeout":
                    test_timeout(gcp, backend_service, instance_group)
                elif test_case == "fault_injection":
                    test_fault_injection(gcp, backend_service, instance_group)
                elif test_case == "api_listener":
                    server_uri = test_api_listener(
                        gcp,
                        backend_service,
                        instance_group,
                        alternate_backend_service,
                    )
                elif test_case == "forwarding_rule_port_match":
                    server_uri = test_forwarding_rule_port_match(
                        gcp, backend_service, instance_group
                    )
                elif test_case == "forwarding_rule_default_port":
                    server_uri = test_forwarding_rule_default_port(
                        gcp, backend_service, instance_group
                    )
                elif test_case == "metadata_filter":
                    test_metadata_filter(
                        gcp,
                        backend_service,
                        instance_group,
                        alternate_backend_service,
                        same_zone_instance_group,
                    )
                elif test_case == "csds":
                    test_csds(gcp, backend_service, instance_group, server_uri)
                else:
                    logger.error("Unknown test case: %s", test_case)
                    sys.exit(1)
                if client_process and client_process.poll() is not None:
                    raise Exception(
                        "Client process exited prematurely with exit code %d"
                        % client_process.returncode
                    )
                result.state = "PASSED"
                result.returncode = 0
            except Exception as e:
                logger.exception("Test case %s failed", test_case)
                failed_tests.append(test_case)
                result.state = "FAILED"
                result.message = str(e)
                if args.halt_after_fail:
                    # Stop the test suite if one case failed.
                    raise
            finally:
                if client_process:
                    if client_process.returncode:
                        logger.info(
                            "Client exited with code %d"
                            % client_process.returncode
                        )
                    else:
                        client_process.terminate()
                test_log_file.close()
                # Workaround for Python 3, as report_utils will invoke decode() on
                # result.message, which has a default value of ''.
                result.message = result.message.encode("UTF-8")
                test_results[test_case] = [result]
                if args.log_client_output:
                    logger.info("Client output:")
                    with open(test_log_filename, "r") as client_output:
                        logger.info(client_output.read())
        if not os.path.exists(_TEST_LOG_BASE_DIR):
            os.makedirs(_TEST_LOG_BASE_DIR)
        report_utils.render_junit_xml_report(
            test_results,
            os.path.join(_TEST_LOG_BASE_DIR, _SPONGE_XML_NAME),
            suite_name="xds_tests",
            multi_target=True,
        )
        if failed_tests:
            logger.error("Test case(s) %s failed", failed_tests)
            sys.exit(1)
finally:
    keep_resources = args.keep_gcp_resources
    if args.halt_after_fail and failed_tests:
        logger.info(
            "Halt after fail triggered, exiting without cleaning up resources"
        )
        keep_resources = True
    if not keep_resources:
        logger.info("Cleaning up GCP resources. This may take some time.")
        clean_up(gcp)

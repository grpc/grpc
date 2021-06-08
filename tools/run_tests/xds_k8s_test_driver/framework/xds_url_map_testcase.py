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
"""A test framework built for urlMap related xDS test cases."""

import abc
import unittest
import json
import time
import inspect
import collections
import threading
from dataclasses import dataclass
import functools
from typing import Any, Union, Tuple, Mapping, Iterable, Optional

from framework import xds_flags
from framework import xds_k8s_flags
from framework.infrastructure import gcp
from framework.infrastructure import k8s
from framework.infrastructure import traffic_director
from framework.test_app import client_app
from framework.test_app import server_app

import grpc
from absl import flags
from absl import logging
from absl.testing import absltest
from google.protobuf import json_format
import googleapiclient
from framework.rpc import grpc_testing

# Load existing flags
flags.adopt_module_key_flags(xds_flags)
flags.adopt_module_key_flags(xds_k8s_flags)

# Define urlMap specific flags
QPS = flags.DEFINE_integer('qps', default=25, help='The QPS client is sending')
_STRATEGY = flags.DEFINE_enum('strategy',
                              default='create',
                              enum_values=['create', 'keep', 'reuse'],
                              help='Strategy of GCP resources management')
# TODO(lidiz) find a better way to filter test cases; e.g., Bazel
_TEST_FILTER = flags.DEFINE_string(
    'test_filter',
    default='',
    help='Only run test case which match the given substring')

# Test configs
_URL_MAP_PROPAGATE_TIMEOUT_SEC = 1800
_URL_MAP_PROPAGATE_CHECK_INTERVAL_SEC = 2
_K8S_NAMESPACE_DELETE_DEADLINE_SEC = 600
_K8S_NAMESPACE_DELETE_CHECK_INTERVAL_SEC = 10
_RPC_DISTRIBUTION_READY_TIMEOUT_SEC = 600

# Global states of this module
_url_map_change_aggregator = None
_gcp_resource_manager = None

# Type aliases
JsonType = Any
HostRule = JsonType
PathMatcher = JsonType
LoadBalancerAccumulatedStatsResponse = grpc_testing.LoadBalancerAccumulatedStatsResponse
XdsTestClient = client_app.XdsTestClient

# ProtoBuf translatable RpcType enums
RpcTypeUnaryCall = 'UNARY_CALL'
RpcTypeEmptyCall = 'EMPTY_CALL'


def _split_camel(s: str, delimiter: str = '-') -> str:
    """Turn camel case name to snake-case-like name."""
    return ''.join([delimiter + c.lower() if c.isupper() else c for c in s
                   ]).lstrip(delimiter)


def _ensure_k8s_namespace_removed(namespace: k8s.KubernetesNamespace):
    try:
        # Start deletion
        namespace.delete(0)
    except Exception as e:
        # Maybe the namespace doesn't exist at all, that's okay
        logging.info('Namespace deletion failed with %s: %s', type(e), e)
    namespace.wait_for_namespace_deleted()
    # Deletion succeed!
    logging.info('K8s namespace "%s" deleted', xds_flags.NAMESPACE.value)


class GcpResourceManager:
    """Manages the lifecycle of GCP resources created by urlMap testcases."""
    lock: threading.Lock
    setup_finished: bool
    ref_count: int

    k8s_api_manager: k8s.KubernetesApiManager
    gcp_api_manager: gcp.api.GcpApiManager
    td: traffic_director.TrafficDirectorManager

    k8s_namespace: k8s.KubernetesNamespace
    test_client_runner: client_app.KubernetesClientRunner

    def __init__(self):
        # Instance states
        self.lock = threading.Lock()
        self.setup_finished = False
        self.ref_count = 0
        # API managers
        self.k8s_api_manager = k8s.KubernetesApiManager(
            xds_k8s_flags.KUBE_CONTEXT.value)
        self.gcp_api_manager = gcp.api.GcpApiManager()
        self.td = traffic_director.TrafficDirectorManager(
            self.gcp_api_manager,
            xds_flags.PROJECT.value,
            resource_prefix=xds_flags.NAMESPACE.value,
            network=xds_flags.NETWORK.value,
        )
        # Kubernetes namespace
        self.k8s_namespace = k8s.KubernetesNamespace(self.k8s_api_manager,
                                                     xds_flags.NAMESPACE.value)
        # Kubernetes Test Client
        self.test_client_runner = client_app.KubernetesClientRunner(
            self.k8s_namespace,
            deployment_name=xds_flags.CLIENT_NAME.value,
            image_name=xds_k8s_flags.CLIENT_IMAGE.value,
            gcp_service_account=xds_k8s_flags.GCP_SERVICE_ACCOUNT.value,
            td_bootstrap_image=xds_k8s_flags.TD_BOOTSTRAP_IMAGE.value,
            network=xds_flags.NETWORK.value,
            debug_use_port_forwarding=xds_k8s_flags.DEBUG_USE_PORT_FORWARDING.
            value,
            stats_port=xds_flags.CLIENT_PORT.value,
            reuse_namespace=True)
        # Cleanup existing debris
        logging.info('Strategy of GCP resources management: %s',
                     _STRATEGY.value)
        if _STRATEGY.value in ['create', 'keep']:
            self.td.cleanup(force=True)
            _ensure_k8s_namespace_removed(self.k8s_namespace)

    @staticmethod
    def default_backend_service() -> str:
        """Returns default backend service URL without GCP interaction."""
        return f'https://www.googleapis.com/compute/v1/projects/{xds_flags.PROJECT.value}/global/backendServices/{xds_flags.NAMESPACE.value}-{traffic_director.TrafficDirectorManager.BACKEND_SERVICE_NAME}'

    @staticmethod
    def alternative_backend_service() -> str:
        """Returns alternative backend service URL without GCP interaction."""
        return f'https://www.googleapis.com/compute/v1/projects/{xds_flags.PROJECT.value}/global/backendServices/{xds_flags.NAMESPACE.value}-{traffic_director.TrafficDirectorManager.ALTERNATIVE_BACKEND_SERVICE_NAME}'

    def _create_reusable_resources(self) -> None:
        """Create the reuseable resources.
        
        The GCP resources including:
        - 2 K8s deployment (backends, alternative backends)
        - Full set of the Traffic Director ritual
        - Merged gigantic urlMap from all imported test cases

        These resources are intended to be used across test cases and multiple runs.
        """
        # Firewall
        if xds_flags.ENSURE_FIREWALL.value:
            self.td.create_firewall_rule(
                allowed_ports=xds_flags.FIREWALL_ALLOWED_PORTS.value)
        # Health Checks
        self.td.create_health_check()
        # Backend Services
        self.td.create_backend_service()
        self.td.create_alternative_backend_service()
        # UrlMap
        final_url_map = _url_map_change_aggregator.get_map()
        self.td.create_url_map_with_content(final_url_map)
        # Target Proxy
        self.td.create_target_proxy()
        # Forwarding Rule
        self.td.create_forwarding_rule(xds_flags.SERVER_XDS_PORT.value)
        # Kubernetes Test Server
        self.test_server_runner = server_app.KubernetesServerRunner(
            self.k8s_namespace,
            deployment_name=xds_flags.SERVER_NAME.value,
            image_name=xds_k8s_flags.SERVER_IMAGE.value,
            gcp_service_account=xds_k8s_flags.GCP_SERVICE_ACCOUNT.value,
            td_bootstrap_image=xds_k8s_flags.TD_BOOTSTRAP_IMAGE.value,
            network=xds_flags.NETWORK.value)
        self.test_server = self.test_server_runner.run(
            replica_count=1,
            test_port=xds_flags.SERVER_PORT.value,
            maintenance_port=xds_flags.SERVER_MAINTENANCE_PORT.value)
        # Kubernetes Test Server Alternative
        self.test_server_alternative_runner = server_app.KubernetesServerRunner(
            self.k8s_namespace,
            deployment_name=xds_flags.SERVER_NAME.value + '-alternative',
            image_name=xds_k8s_flags.SERVER_IMAGE.value,
            gcp_service_account=xds_k8s_flags.GCP_SERVICE_ACCOUNT.value,
            td_bootstrap_image=xds_k8s_flags.TD_BOOTSTRAP_IMAGE.value,
            network=xds_flags.NETWORK.value,
            reuse_namespace=True)
        self.test_server_alternative = self.test_server_alternative_runner.run(
            replica_count=1,
            test_port=xds_flags.SERVER_PORT.value,
            maintenance_port=xds_flags.SERVER_MAINTENANCE_PORT.value)
        # Add backend to default backend service
        neg_name, neg_zones = self.k8s_namespace.get_service_neg(
            self.test_server_runner.service_name, xds_flags.SERVER_PORT.value)
        self.td.backend_service_add_neg_backends(neg_name, neg_zones)
        # Add backend to alternative backend service
        neg_name, neg_zones = self.k8s_namespace.get_service_neg(
            self.test_server_alternative_runner.service_name,
            xds_flags.SERVER_PORT.value)
        self.td.alternative_backend_service_add_neg_backends(
            neg_name, neg_zones)
        # Wait for healthy backends
        self.td.wait_for_backends_healthy_status()
        self.td.wait_for_alternative_backends_healthy_status()

    def ensure_setup(self):
        """A thread-safe method to ensure the GCP resources are up."""
        with self.lock:
            self.ref_count += 1
            if self.setup_finished:
                return
            logging.info('GcpResourceManager: start setup')
            if _STRATEGY.value in ['create', 'keep']:
                self._create_reusable_resources()
            # Flip flag
            self.setup_finished = True

    def maybe_tear_down(self):
        """A thread-safe method to remove resources when refcount reaches 0."""
        with self.lock:
            self.ref_count -= 1
            if self.ref_count == 0:
                if _STRATEGY.value == 'create':
                    self.cleanup()
                else:
                    logging.info('Skipping GCP resource cleanup')

    def cleanup(self):
        logging.info('GcpResourceManager: cleanup')
        if hasattr(self, 'td'):
            self.td.cleanup(force=True)
        if hasattr(self, 'test_client_runner'):
            self.test_client_runner.cleanup(force=True)
        if hasattr(self, 'test_server_runner'):
            self.test_server_runner.cleanup(force=True)
        if hasattr(self, 'test_server_alternative_runner'):
            self.test_server_alternative_runner.cleanup(force=True,
                                                        force_namespace=True)


class DumpedXdsConfig(dict):
    """A convenience class to check xDS config.

    Feel free to add more pre-compute fields.
    """

    def __init__(self, json_config: JsonType):
        super().__init__(json_config)
        self.json_config = json_config
        self.lds = None
        self.rds = None
        self.cds = []
        self.eds = []
        self.endpoints = []
        for xds_config in json_config['xdsConfig']:
            try:
                if 'listenerConfig' in xds_config:
                    self.lds = xds_config['listenerConfig']['dynamicListeners'][
                        0]['activeState']['listener']
                elif 'routeConfig' in xds_config:
                    self.rds = xds_config['routeConfig']['dynamicRouteConfigs'][
                        0]['routeConfig']
                elif 'clusterConfig' in xds_config:
                    for cluster in xds_config['clusterConfig'][
                            'dynamicActiveClusters']:
                        self.cds.append(cluster['cluster'])
                elif 'endpointConfig' in xds_config:
                    for endpoint in xds_config['endpointConfig'][
                            'dynamicEndpointConfigs']:
                        self.eds.append(endpoint['endpointConfig'])
            except Exception as e:
                logging.debug('Parse dumped xDS config failed with %s: %s',
                              type(e), e)
        for endpoint_config in self.eds:
            for endpoint in endpoint_config.get('endpoints', {}):
                for lb_endpoint in endpoint.get('lbEndpoints', {}):
                    try:
                        if lb_endpoint['healthStatus'] == 'HEALTHY':
                            self.endpoints.append(
                                '%s:%s' % (lb_endpoint['endpoint']['address']
                                           ['socketAddress']['address'],
                                           lb_endpoint['endpoint']['address']
                                           ['socketAddress']['portValue']))
                    except Exception as e:
                        logging.debug('Parse endpoint failed with %s: %s',
                                      type(e), e)

    def __str__(self) -> str:
        return json.dumps(self.json_config, indent=2)


class RpcDistributionStats:
    """A convenience class to check RPC distribution.

    Feel free to add more pre-compute fields.
    """
    num_failures: int
    num_oks: int
    default_service_rpc_count: int
    alternative_service_rpc_count: int
    unary_call_default_service_rpc_count: int
    empty_call_default_service_rpc_count: int
    unary_call_alternative_service_rpc_count: int
    empty_call_alternative_service_rpc_count: int

    def __init__(self, json_lb_stats: JsonType):
        self.num_failures = json_lb_stats.get('numFailures', 0)

        self.num_oks = 0
        self.default_service_rpc_count = 0
        self.alternative_service_rpc_count = 0
        self.unary_call_default_service_rpc_count = 0
        self.empty_call_default_service_rpc_count = 0
        self.unary_call_alternative_service_rpc_count = 0
        self.empty_call_alternative_service_rpc_count = 0

        if 'rpcsByMethod' in json_lb_stats:
            for rpc_type in json_lb_stats['rpcsByMethod']:
                for peer in json_lb_stats['rpcsByMethod'][rpc_type][
                        'rpcsByPeer']:
                    count = json_lb_stats['rpcsByMethod'][rpc_type][
                        'rpcsByPeer'][peer]
                    self.num_oks += count
                    if rpc_type == 'UnaryCall':
                        if 'alternative' in peer:
                            self.unary_call_alternative_service_rpc_count = count
                            self.alternative_service_rpc_count += count
                        else:
                            self.unary_call_default_service_rpc_count = count
                            self.default_service_rpc_count += count
                    else:
                        if 'alternative' in peer:
                            self.empty_call_alternative_service_rpc_count = count
                            self.alternative_service_rpc_count += count
                        else:
                            self.empty_call_default_service_rpc_count = count
                            self.default_service_rpc_count += count


@dataclass
class ExpectedResult:
    """Describes the expected result of assertRpcStatusCode method below."""
    rpc_type: str = RpcTypeUnaryCall
    status_code: grpc.StatusCode = grpc.StatusCode.OK
    ratio: float = 1


class XdsUrlMapTestCase(abc.ABC, absltest.TestCase):
    """XdsUrlMapTestCase is the base class for urlMap related tests.
    
    The subclass is expected to implement 3 methods:
    - url_map_change: Updates the urlMap components for this test case
    - xds_config_validate: Validates if the client received legit xDS configs
    - rpc_distribution_validate: Validates if the routing behavior is correct
    """

    @staticmethod
    @abc.abstractstaticmethod
    def url_map_change(
            host_rule: HostRule,
            path_matcher: PathMatcher) -> Tuple[HostRule, PathMatcher]:
        """Updates the dedicated urlMap components for this test case.

        Each test case will have a dedicated HostRule, where the hostname is
        generated from the test case name. The HostRule will be linked to a
        PathMatcher, where stores the routing logic.

        Args:
            host_rule: A HostRule GCP resource as a JSON dict.
            path_matcher: A PathMatcher GCP resource as a JSON dict.
        
        Returns:
            A tuple contains the updated version of given HostRule and
            PathMatcher.
        """
        pass

    @abc.abstractmethod
    def xds_config_validate(self, xds_config: DumpedXdsConfig) -> None:
        """Validates received xDS config, if any is wrong, raise.
        
        Args:
            xds_config: A DumpedXdsConfig instance can be used as a JSON dict,
              but also provides helper fields for commonly checked xDS config.
        """
        pass

    @abc.abstractmethod
    def rpc_distribution_validate(self,
                                  client: client_app.XdsTestClient) -> None:
        """Validates the routing behavior, if any is wrong, raise.
        
        Args:
            client: A XdsTestClient instance for all sorts of end2end testing.
        """
        pass

    @classmethod
    def set_module_name(cls, name: str) -> None:
        # E.g.: tests.url_map.header_matching_test
        cls.module_name = name

    @classmethod
    @functools.lru_cache(None)
    def _shorter_module_name(cls) -> str:
        # E.g.: tests.url_map.header_matching_test -> header-matching
        return cls.module_name.split('.')[-1].replace('_test',
                                                      '').replace('_', '-')

    @classmethod
    @functools.lru_cache(None)
    def hostname(cls) -> str:
        return cls._shorter_module_name() + '.' + _split_camel(
            cls.__name__) + f':{xds_flags.SERVER_XDS_PORT.value}'

    @classmethod
    @functools.lru_cache(None)
    def path_matcher(cls) -> str:
        # Path matcher name must match r'(?:[a-z](?:[-a-z0-9]{0,61}[a-z0-9])?)'
        return cls._shorter_module_name() + '-' + _split_camel(
            cls.__name__) + '-pm'

    @classmethod
    def setUpClass(cls):
        cls.resource = _gcp_resource_manager
        cls.resource.ensure_setup()
        # TODO(lidiz) technically, we should be able to create deployments for
        # each individual test cases to allow them to be run concurrently.
        # However, the framework uses workload identity to connect to TD. The
        # workload identity requires IAM policy binding with (Kubernetes
        # Namespace, Kubernetes Deployment, Kubernetes Workload). This demands
        # admin permission.
        cls.resource.test_client_runner.cleanup(force=True)
        # Sending both RPCs when starting.
        cls.test_client = cls.resource.test_client_runner.run(
            server_target=f'xds:///{cls.hostname()}',
            rpc='UnaryCall,EmptyCall',
            qps=QPS.value,
            print_response=True)

    @classmethod
    def tearDownClass(cls):
        cls.resource.test_client_runner.cleanup(force=True)
        cls.resource.maybe_tear_down()

    def test_rpc_distribution(self):
        # TODO(https://github.com/grpc/grpc-java/issues/8213)
        # We need this call because there is a possible race: xDS config is
        # ready, but the client channel is still not READY. Or the client
        # channel turned READY but only connects to one of the backends.
        # When we find a better way, remove this.
        deadline = time.time() + _RPC_DISTRIBUTION_READY_TIMEOUT_SEC
        while time.time() < deadline:
            result = self.configure_and_send(
                self.test_client,
                rpc_types=[RpcTypeUnaryCall, RpcTypeEmptyCall],
                num_rpcs=QPS.value)
            if result.num_oks:
                break
        if time.time() >= deadline:
            raise RuntimeError('failed to send any OK RPC within %s seconds',
                               _RPC_DISTRIBUTION_READY_TIMEOUT_SEC)
        # Runs test case logic
        self.rpc_distribution_validate(self.test_client)

    def test_client_config(self):
        config = None
        json_config = None
        last_exception = None
        deadline = time.time() + _URL_MAP_PROPAGATE_TIMEOUT_SEC
        while time.time() < deadline:
            try:
                config = self.test_client.csds.fetch_client_status(
                    log_level=logging.INFO)
                if config is None:
                    json_config = None
                else:
                    json_config = json_format.MessageToDict(config)
                    try:
                        self.xds_config_validate(DumpedXdsConfig(json_config))
                    except Exception as e:
                        logging.info('xDS config check failed: %s: %s', type(e),
                                     e)
                        if type(last_exception) != type(e) or str(
                                last_exception) != str(e):
                            logging.exception(e)
                            last_exception = e
                    else:
                        return
                time.sleep(_URL_MAP_PROPAGATE_CHECK_INTERVAL_SEC)
            except KeyboardInterrupt:
                logging.info('latest xDS config: %s',
                             json.dumps(json_config, indent=2))
                raise
        raise RuntimeError(
            'failed to receive valid xDS config within %s seconds, latest: %s' %
            (_URL_MAP_PROPAGATE_TIMEOUT_SEC, json.dumps(json_config, indent=2)))

    @staticmethod
    def configure_and_send(test_client: client_app.XdsTestClient,
                           *,
                           rpc_types: Iterable[str],
                           metadata: Optional[Iterable[Tuple[str, str,
                                                             str]]] = None,
                           app_timeout: Optional[int] = None,
                           num_rpcs: int) -> RpcDistributionStats:
        test_client.update_client_config(rpc_types=rpc_types,
                                         metadata=metadata,
                                         app_timeout=app_timeout)
        json_lb_stats = json_format.MessageToDict(
            test_client.get_load_balancer_stats(num_rpcs=num_rpcs))
        logging.info(
            'Received LoadBalancerStatsResponse from test client %s:\n%s',
            test_client.ip, json.dumps(json_lb_stats, indent=2))
        return RpcDistributionStats(json_lb_stats)

    def assertNumEndpoints(self, xds_config: DumpedXdsConfig, k: int) -> None:
        self.assertEqual(
            k, len(xds_config.endpoints),
            f'insufficient endpoints in EDS: want={k} seen={xds_config.endpoints}'
        )

    def assertRpcStatusCode(self, test_client: XdsTestClient, *,
                            expected: Iterable[ExpectedResult], length: int,
                            tolerance: float) -> None:
        """Assert the distribution of RPC statuses over a period of time."""
        # Sending with pre-set QPS for a period of time
        before_stats = test_client.get_load_balancer_accumulated_stats()
        logging.info(
            'Received LoadBalancerAccumulatedStatsResponse from test client %s: before:\n%s',
            test_client.ip, before_stats)
        time.sleep(length)
        after_stats = test_client.get_load_balancer_accumulated_stats()
        logging.info(
            'Received LoadBalancerAccumulatedStatsResponse from test client %s: after: \n%s',
            test_client.ip, after_stats)
        # Validate the diff
        total = length * QPS.value
        for expected_result in expected:
            rpc = expected_result.rpc_type
            status = expected_result.status_code.value[0]
            seen = after_stats.stats_per_method.get(rpc, {}).result.get(
                status, 0) - before_stats.stats_per_method.get(
                    rpc, {}).result.get(status, 0)
            want = total * expected_result.ratio
            self.assertLessEqual(
                abs(seen - want) / want, tolerance,
                'Expect rpc [%s] to return [%s] at %.2f ratio: seen=%d want=%d diff_ratio=%.4f > %.2f'
                % (rpc, expected_result.status_code, expected_result.ratio,
                   seen, want, abs(seen - want) / want, tolerance))


class UrlMapChangeAggregator:
    """Where all the urlMap change happens."""
    _map: JsonType

    def __init__(self):
        self._map = {
            "name":
                f"{xds_flags.NAMESPACE.value}-{traffic_director.TrafficDirectorManager.URL_MAP_NAME}",
            "defaultService":
                GcpResourceManager.default_backend_service(),
            "hostRules": [],
            "pathMatchers": [],
        }

    def get_map(self) -> JsonType:
        return self._map

    def apply_change(self, test_case: XdsUrlMapTestCase):
        logging.info('Apply urlMap change for test case: %s.%s',
                     test_case.module_name, test_case.__name__)
        url_map_parts = test_case.url_map_change(
            *self._get_test_case_url_map(test_case))
        self._set_test_case_url_map(*url_map_parts)

    def _get_test_case_url_map(
            self, test_case: XdsUrlMapTestCase) -> Tuple[JsonType, JsonType]:
        host_rule = {
            "hosts": [test_case.hostname()],
            "pathMatcher": test_case.path_matcher(),
        }
        path_matcher = {
            "name": test_case.path_matcher(),
            "defaultService": GcpResourceManager.default_backend_service(),
        }
        return host_rule, path_matcher

    def _set_test_case_url_map(self, host_rule: JsonType,
                               path_matcher: JsonType) -> None:
        self._map["hostRules"].append(host_rule)
        self._map["pathMatchers"].append(path_matcher)


def load_tests(
        loader: absltest.TestLoader,
        *modules: 'Module',
        module_name_override: Optional[str] = None) -> unittest.TestSuite:
    """The implementation of the load_tests protocol of unittest.

    TODO(lidiz) parallelize the test case run. After the resource creation, each
    test case should be able to be run in a dedicated thread. But this might
    amplify the complexity of existing API managers. So, maybe parallelize the
    test runs in a separate attempt.

    Args:
        loader: A TestLoader instance fetches test case methods from the test
          case classes, then build TestSuite instances.
        modules: A list of Python modules for this function to search for test
          case classes.
        module_name_override: A temporary solution to get consistent module name
          instead of "__main__" if a test is invoked directly.

    Returns:
        A TestSuite instances which contains all test cases, and controls how
        they will be executed.
    """
    # Prepares global variables for merging resources
    global _gcp_resource_manager
    global _url_map_change_aggregator
    if _gcp_resource_manager is None:
        _gcp_resource_manager = GcpResourceManager()
    if _url_map_change_aggregator is None:
        _url_map_change_aggregator = UrlMapChangeAggregator()
    # Validates input arguments
    if module_name_override is not None:
        assert len(modules) == 1
    # Crafting test cases
    suite = unittest.TestSuite()
    for module in modules:
        for key, value in inspect.getmembers(module):
            if not key.startswith('Test'):
                # A test class name needs to start with 'Test' in camel case
                continue
            test_class = value
            if not isinstance(test_class, type):
                # This is not a class
                continue
            if module_name_override is not None:
                module_name = module_name_override
            else:
                module_name = module.__name__
            # Prepares global states.
            # NOTE(lidiz) We want this to happen before skipping tests, so the
            # created resources can be reused widely.
            test_class.set_module_name(module_name)
            _url_map_change_aggregator.apply_change(test_class)
            # Skip unwanted test cases if test filter is given
            if _TEST_FILTER.value != '':
                if _TEST_FILTER.value not in (module_name + '.' +
                                              test_class.__name__):
                    continue
            # Load tests from the given test case class
            tests = loader.loadTestsFromTestCase(test_class)
            suite.addTests(tests)
    return suite

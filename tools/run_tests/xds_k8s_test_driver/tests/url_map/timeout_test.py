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
import abc
import collections
import json
import logging
import sys
import time
from typing import Iterable, Tuple

import grpc
from absl import flags
from absl.testing import absltest
from framework import xds_url_map_testcase
from framework.rpc import grpc_testing
from framework.test_app import client_app
from google.protobuf import json_format

# Type aliases
HostRule = xds_url_map_testcase.HostRule
PathMatcher = xds_url_map_testcase.PathMatcher
GcpResourceManager = xds_url_map_testcase.GcpResourceManager
DumpedXdsConfig = xds_url_map_testcase.DumpedXdsConfig
RpcTypeUnaryCall = xds_url_map_testcase.RpcTypeUnaryCall
RpcTypeEmptyCall = xds_url_map_testcase.RpcTypeEmptyCall
XdsTestClient = client_app.XdsTestClient

logger = logging.getLogger(__name__)
flags.adopt_module_key_flags(xds_url_map_testcase)

# The first batch of RPCs don't count towards the result of test case. They are
# meant to prove the communication between driver and client is fine.
_NUM_RPCS = 25
_ERROR_TOLERANCE = 0.1

LoadBalancerAccumulatedStatsResponse = grpc_testing.LoadBalancerAccumulatedStatsResponse


def _get_diff(before: LoadBalancerAccumulatedStatsResponse,
              after: LoadBalancerAccumulatedStatsResponse, rpc: str,
              status: int) -> int:
    return after.stats_per_method[rpc].result[status] - before.stats_per_method[
        rpc].result[status]


def _equal_with_error(seen: int,
                      want: int,
                      tolerance: float = _ERROR_TOLERANCE):
    return abs((seen - want) / want) <= tolerance


_ExpectedResult = collections.namedtuple('_ExpectedResult',
                                         ['rpc_type', 'status_code'])


class _BaseXdsTimeOutTestCase(xds_url_map_testcase.XdsUrlMapTestCase):
    _LENGTH_OF_RPC_SENDING_SEC: int = 10

    @staticmethod
    def url_map_change(
            host_rule: HostRule,
            path_matcher: PathMatcher) -> Tuple[HostRule, PathMatcher]:
        path_matcher['routeRules'] = [{
            'priority': 0,
            'matchRules': [{
                'fullPathMatch': '/grpc.testing.TestService/UnaryCall'
            }],
            'service': GcpResourceManager.default_backend_service(),
            'routeAction': {
                'maxStreamDuration': {
                    'seconds': 3,
                },
            },
        }]
        return host_rule, path_matcher

    def xds_config_validate(self, xds_config: DumpedXdsConfig):
        self.assertTrue(xds_config.endpoints)
        self.assertEqual(
            xds_config.rds['virtualHosts'][0]['routes'][0]['route']
            ['maxStreamDuration']['maxStreamDuration'], '3s')
        self.assertEqual(
            xds_config.rds['virtualHosts'][0]['routes'][0]['route']
            ['maxStreamDuration']['grpcTimeoutHeaderMax'], '3s')

    def rpc_distribution_validate(self, unused_test_client):
        raise NotImplementedError()

    def assertRpcStatusCode(self,
                            test_client: XdsTestClient,
                            *,
                            expected: Iterable[_ExpectedResult],
                            length: int = _LENGTH_OF_RPC_SENDING_SEC) -> None:
        # Sending with pre-set QPS for a period of time
        before_stats = test_client.get_load_balancer_accumulated_stats()
        time.sleep(length)
        after_stats = test_client.get_load_balancer_accumulated_stats()
        logging.info(
            'Received LoadBalancerAccumulatedStatsResponse from test client %s: before:\n%s',
            test_client.ip, before_stats)
        logging.info(
            'Received LoadBalancerAccumulatedStatsResponse from test client %s: after: \n%s',
            test_client.ip, after_stats)
        # Validate the diff
        want = length * xds_url_map_testcase.QPS.value
        for expected_result in expected:
            self.assertTrue(
                _equal_with_error(
                    _get_diff(
                        before_stats,
                        after_stats,
                        expected_result.rpc_type,
                        expected_result.status_code.value[0],
                    ),
                    want,
                ))


class TestTimeoutInRouteRule(_BaseXdsTimeOutTestCase):

    def rpc_distribution_validate(self, test_client: XdsTestClient):
        rpc_distribution = self.configure_and_send(
            test_client,
            rpc_types=[RpcTypeUnaryCall, RpcTypeEmptyCall],
            # UnaryCall and EmptyCall both sleep-4.
            # UnaryCall timeouts, EmptyCall succeeds.
            metadata=(
                (RpcTypeUnaryCall, 'rpc-behavior', 'sleep-4'),
                (RpcTypeEmptyCall, 'rpc-behavior', 'sleep-4'),
            ),
            num_rpcs=_NUM_RPCS)
        self.assertRpcStatusCode(
            test_client,
            expected=(
                _ExpectedResult(rpc_type=RpcTypeUnaryCall,
                                status_code=grpc.StatusCode.DEADLINE_EXCEEDED),
                _ExpectedResult(rpc_type=RpcTypeEmptyCall,
                                status_code=grpc.StatusCode.OK),
            ))


class TestTimeoutInApplication(_BaseXdsTimeOutTestCase):

    def rpc_distribution_validate(self, test_client: XdsTestClient):
        rpc_distribution = self.configure_and_send(
            test_client,
            rpc_types=[RpcTypeUnaryCall],
            # UnaryCall only with sleep-2; timeout=1s; calls timeout.
            metadata=((RpcTypeUnaryCall, 'rpc-behavior', 'sleep-2'),),
            app_timeout=1,
            num_rpcs=_NUM_RPCS)
        self.assertRpcStatusCode(
            test_client,
            expected=(_ExpectedResult(
                rpc_type=RpcTypeUnaryCall,
                status_code=grpc.StatusCode.DEADLINE_EXCEEDED),))


class TestTimeoutNotExceeded(_BaseXdsTimeOutTestCase):

    def rpc_distribution_validate(self, test_client: XdsTestClient):
        rpc_distribution = self.configure_and_send(
            test_client,
            # UnaryCall only with no sleep; calls succeed.
            rpc_types=[RpcTypeUnaryCall],
            num_rpcs=_NUM_RPCS)
        self.assertRpcStatusCode(test_client,
                                 expected=(_ExpectedResult(
                                     rpc_type=RpcTypeUnaryCall,
                                     status_code=grpc.StatusCode.OK),))


def load_tests(loader: absltest.TestLoader, unused_tests, unused_pattern):
    return xds_url_map_testcase.load_tests(loader,
                                           sys.modules[__name__],
                                           module_name_override='timeout_test')


if __name__ == '__main__':
    absltest.main(failfast=True)

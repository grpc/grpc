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
import logging
import time
from typing import Tuple

from absl import flags
from absl.testing import absltest
import grpc

from framework import xds_k8s_flags
from framework import xds_url_map_testcase
from framework.test_app import client_app

# Type aliases
HostRule = xds_url_map_testcase.HostRule
PathMatcher = xds_url_map_testcase.PathMatcher
GcpResourceManager = xds_url_map_testcase.GcpResourceManager
DumpedXdsConfig = xds_url_map_testcase.DumpedXdsConfig
RpcTypeUnaryCall = xds_url_map_testcase.RpcTypeUnaryCall
XdsTestClient = client_app.XdsTestClient
ExpectedResult = xds_url_map_testcase.ExpectedResult
TestConfig = xds_url_map_testcase.TestConfig

logger = logging.getLogger(__name__)
flags.adopt_module_key_flags(xds_url_map_testcase)

# The first batch of RPCs don't count towards the result of test case. They are
# meant to prove the communication between driver and client is fine.
_NUM_RPCS = 10
_LENGTH_OF_RPC_SENDING_SEC = 16
# We are using sleep to synchronize test driver and the client... Even though
# the client is sending at QPS rate, we can't assert that exactly QPS *
# SLEEP_DURATION number of RPC is finished. The final completed RPC might be
# slightly more or less.
_NON_RANDOM_ERROR_TOLERANCE = 0.01
_RPC_BEHAVIOR_HEADER_NAME = 'rpc-behavior'


def _build_retry_route_rule(retryConditions, num_retries):
    return {
        'priority': 0,
        'matchRules': [{
            'fullPathMatch': '/grpc.testing.TestService/UnaryCall'
        }],
        'service': GcpResourceManager().default_backend_service(),
        'routeAction': {
            'retryPolicy': {
                'retryConditions': retryConditions,
                'numRetries': num_retries,
            }
        },
    }


class TestRetryUpTo3AttemptsAndFail(xds_url_map_testcase.XdsUrlMapTestCase):

    @staticmethod
    def is_supported(config: TestConfig) -> bool:
        if config.client_lang in ['cpp', 'java', 'python']:
            return config.version_ge('v1.40.x')
        elif config.client_lang == 'go':
            return config.version_ge('v1.41.x')
        return False

    @staticmethod
    def url_map_change(
            host_rule: HostRule,
            path_matcher: PathMatcher) -> Tuple[HostRule, PathMatcher]:
        path_matcher["routeRules"] = [
            _build_retry_route_rule(retryConditions=["unavailable"],
                                    num_retries=3)
        ]
        return host_rule, path_matcher

    def xds_config_validate(self, xds_config: DumpedXdsConfig):
        self.assertNumEndpoints(xds_config, 1)
        retry_config = xds_config.rds['virtualHosts'][0]['routes'][0]['route'][
            'retryPolicy']
        self.assertEqual(3, retry_config['numRetries'])
        self.assertEqual('unavailable', retry_config['retryOn'])

    def rpc_distribution_validate(self, test_client: XdsTestClient):
        rpc_distribution = self.configure_and_send(
            test_client,
            rpc_types=[RpcTypeUnaryCall],
            metadata=[(RpcTypeUnaryCall, _RPC_BEHAVIOR_HEADER_NAME,
                       'error-code-14,succeed-on-retry-attempt-4')],
            num_rpcs=_NUM_RPCS)
        self.assertRpcStatusCode(test_client,
                                 expected=(ExpectedResult(
                                     rpc_type=RpcTypeUnaryCall,
                                     status_code=grpc.StatusCode.UNAVAILABLE,
                                     ratio=1),),
                                 length=_LENGTH_OF_RPC_SENDING_SEC,
                                 tolerance=_NON_RANDOM_ERROR_TOLERANCE)


class TestRetryUpTo4AttemptsAndSucceed(xds_url_map_testcase.XdsUrlMapTestCase):

    @staticmethod
    def is_supported(config: TestConfig) -> bool:
        if config.client_lang in ['cpp', 'java', 'python']:
            return config.version_ge('v1.40.x')
        elif config.client_lang == 'go':
            return config.version_ge('v1.41.x')
        return False

    @staticmethod
    def url_map_change(
            host_rule: HostRule,
            path_matcher: PathMatcher) -> Tuple[HostRule, PathMatcher]:
        path_matcher["routeRules"] = [
            _build_retry_route_rule(retryConditions=["unavailable"],
                                    num_retries=4)
        ]
        return host_rule, path_matcher

    def xds_config_validate(self, xds_config: DumpedXdsConfig):
        self.assertNumEndpoints(xds_config, 1)
        retry_config = xds_config.rds['virtualHosts'][0]['routes'][0]['route'][
            'retryPolicy']
        self.assertEqual(4, retry_config['numRetries'])
        self.assertEqual('unavailable', retry_config['retryOn'])

    def rpc_distribution_validate(self, test_client: XdsTestClient):
        rpc_distribution = self.configure_and_send(
            test_client,
            rpc_types=[RpcTypeUnaryCall],
            metadata=[(RpcTypeUnaryCall, _RPC_BEHAVIOR_HEADER_NAME,
                       'error-code-14,succeed-on-retry-attempt-4')],
            num_rpcs=_NUM_RPCS)
        self.assertRpcStatusCode(test_client,
                                 expected=(ExpectedResult(
                                     rpc_type=RpcTypeUnaryCall,
                                     status_code=grpc.StatusCode.OK,
                                     ratio=1),),
                                 length=_LENGTH_OF_RPC_SENDING_SEC,
                                 tolerance=_NON_RANDOM_ERROR_TOLERANCE)


if __name__ == '__main__':
    absltest.main()

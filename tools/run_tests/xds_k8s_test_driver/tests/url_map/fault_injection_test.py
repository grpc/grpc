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

from framework import xds_url_map_testcase
from framework.helpers import skips
from framework.test_app import client_app

# Type aliases
HostRule = xds_url_map_testcase.HostRule
PathMatcher = xds_url_map_testcase.PathMatcher
GcpResourceManager = xds_url_map_testcase.GcpResourceManager
DumpedXdsConfig = xds_url_map_testcase.DumpedXdsConfig
RpcTypeUnaryCall = xds_url_map_testcase.RpcTypeUnaryCall
RpcTypeEmptyCall = xds_url_map_testcase.RpcTypeEmptyCall
XdsTestClient = client_app.XdsTestClient
ExpectedResult = xds_url_map_testcase.ExpectedResult
_Lang = skips.Lang

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
# For random generator involved test cases, we want to be more loose about the
# final result. Otherwise, we will need more test duration (sleep duration) and
# more accurate communication mechanism. The accurate of random number
# generation is not the intention of this test.
_ERROR_TOLERANCE = 0.2
_DELAY_CASE_APPLICATION_TIMEOUT_SEC = 1
_BACKLOG_WAIT_TIME_SEC = 20


def _build_fault_injection_route_rule(
    abort_percentage: int = 0, delay_percentage: int = 0
):
    return {
        "priority": 0,
        "matchRules": [
            {"fullPathMatch": "/grpc.testing.TestService/UnaryCall"}
        ],
        "service": GcpResourceManager().default_backend_service(),
        "routeAction": {
            "faultInjectionPolicy": {
                "abort": {
                    "httpStatus": 401,
                    "percentage": abort_percentage,
                },
                "delay": {
                    "fixedDelay": {"seconds": "20"},
                    "percentage": delay_percentage,
                },
            }
        },
    }


def _wait_until_backlog_cleared(
    test_client: XdsTestClient, timeout: int = _BACKLOG_WAIT_TIME_SEC
):
    """Wait until the completed RPC is close to started RPC.

    For delay injected test cases, there might be a backlog of RPCs due to slow
    initialization of the client. E.g., if initialization took 20s and qps is
    25, then there will be a backlog of 500 RPCs. In normal test cases, this is
    fine, because RPCs will fail immediately. But for delay injected test cases,
    the RPC might linger much longer and affect the stability of test results.
    """
    logger.info("Waiting for RPC backlog to clear for %d seconds", timeout)
    deadline = time.time() + timeout
    while time.time() < deadline:
        stats = test_client.get_load_balancer_accumulated_stats()
        ok = True
        for rpc_type in [RpcTypeUnaryCall, RpcTypeEmptyCall]:
            started = stats.num_rpcs_started_by_method.get(rpc_type, 0)
            completed = stats.num_rpcs_succeeded_by_method.get(
                rpc_type, 0
            ) + stats.num_rpcs_failed_by_method.get(rpc_type, 0)
            # We consider the backlog is healthy, if the diff between started
            # RPCs and completed RPCs is less than 1.5 QPS.
            if abs(started - completed) > xds_url_map_testcase.QPS.value * 1.1:
                logger.info(
                    "RPC backlog exist: rpc_type=%s started=%s completed=%s",
                    rpc_type,
                    started,
                    completed,
                )
                time.sleep(_DELAY_CASE_APPLICATION_TIMEOUT_SEC)
                ok = False
            else:
                logger.info(
                    "RPC backlog clear: rpc_type=%s started=%s completed=%s",
                    rpc_type,
                    started,
                    completed,
                )
        if ok:
            # Both backlog of both types of RPCs is clear, success, return.
            return

    raise RuntimeError("failed to clear RPC backlog in %s seconds" % timeout)


def _is_supported(config: skips.TestConfig) -> bool:
    if config.client_lang == _Lang.NODE:
        return config.version_gte("v1.4.x")
    return True


class TestZeroPercentFaultInjection(xds_url_map_testcase.XdsUrlMapTestCase):
    @staticmethod
    def is_supported(config: skips.TestConfig) -> bool:
        return _is_supported(config)

    @staticmethod
    def url_map_change(
        host_rule: HostRule, path_matcher: PathMatcher
    ) -> Tuple[HostRule, PathMatcher]:
        path_matcher["routeRules"] = [
            _build_fault_injection_route_rule(
                abort_percentage=0, delay_percentage=0
            )
        ]
        return host_rule, path_matcher

    def xds_config_validate(self, xds_config: DumpedXdsConfig):
        self.assertNumEndpoints(xds_config, 1)
        filter_config = xds_config.rds["virtualHosts"][0]["routes"][0][
            "typedPerFilterConfig"
        ]["envoy.filters.http.fault"]
        self.assertEqual("20s", filter_config["delay"]["fixedDelay"])
        self.assertEqual(
            0, filter_config["delay"]["percentage"].get("numerator", 0)
        )
        self.assertEqual(
            "MILLION", filter_config["delay"]["percentage"]["denominator"]
        )
        self.assertEqual(401, filter_config["abort"]["httpStatus"])
        self.assertEqual(
            0, filter_config["abort"]["percentage"].get("numerator", 0)
        )
        self.assertEqual(
            "MILLION", filter_config["abort"]["percentage"]["denominator"]
        )

    def rpc_distribution_validate(self, test_client: XdsTestClient):
        self.configure_and_send(
            test_client, rpc_types=(RpcTypeUnaryCall,), num_rpcs=_NUM_RPCS
        )
        self.assertRpcStatusCode(
            test_client,
            expected=(
                ExpectedResult(
                    rpc_type=RpcTypeUnaryCall,
                    status_code=grpc.StatusCode.OK,
                    ratio=1,
                ),
            ),
            length=_LENGTH_OF_RPC_SENDING_SEC,
            tolerance=_NON_RANDOM_ERROR_TOLERANCE,
        )


class TestNonMatchingFaultInjection(xds_url_map_testcase.XdsUrlMapTestCase):
    """EMPTY_CALL is not fault injected, so it should succeed."""

    @staticmethod
    def is_supported(config: skips.TestConfig) -> bool:
        return _is_supported(config)

    @staticmethod
    def client_init_config(rpc: str, metadata: str):
        # Python interop client will stuck if the traffic is slow (in this case,
        # 20s injected). The purpose of this test is examining the un-injected
        # traffic is not impacted, so it's fine to just send un-injected
        # traffic.
        return "EmptyCall", metadata

    @staticmethod
    def url_map_change(
        host_rule: HostRule, path_matcher: PathMatcher
    ) -> Tuple[HostRule, PathMatcher]:
        path_matcher["routeRules"] = [
            _build_fault_injection_route_rule(
                abort_percentage=100, delay_percentage=100
            )
        ]
        return host_rule, path_matcher

    def xds_config_validate(self, xds_config: DumpedXdsConfig):
        self.assertNumEndpoints(xds_config, 1)
        # The first route rule for UNARY_CALL is fault injected
        self.assertEqual(
            "/grpc.testing.TestService/UnaryCall",
            xds_config.rds["virtualHosts"][0]["routes"][0]["match"]["path"],
        )
        filter_config = xds_config.rds["virtualHosts"][0]["routes"][0][
            "typedPerFilterConfig"
        ]["envoy.filters.http.fault"]
        self.assertEqual("20s", filter_config["delay"]["fixedDelay"])
        self.assertEqual(
            1000000, filter_config["delay"]["percentage"]["numerator"]
        )
        self.assertEqual(
            "MILLION", filter_config["delay"]["percentage"]["denominator"]
        )
        self.assertEqual(401, filter_config["abort"]["httpStatus"])
        self.assertEqual(
            1000000, filter_config["abort"]["percentage"]["numerator"]
        )
        self.assertEqual(
            "MILLION", filter_config["abort"]["percentage"]["denominator"]
        )
        # The second route rule for all other RPCs is untouched
        self.assertNotIn(
            "envoy.filters.http.fault",
            xds_config.rds["virtualHosts"][0]["routes"][1].get(
                "typedPerFilterConfig", {}
            ),
        )

    def rpc_distribution_validate(self, test_client: XdsTestClient):
        self.assertRpcStatusCode(
            test_client,
            expected=(
                ExpectedResult(
                    rpc_type=RpcTypeEmptyCall,
                    status_code=grpc.StatusCode.OK,
                    ratio=1,
                ),
            ),
            length=_LENGTH_OF_RPC_SENDING_SEC,
            tolerance=_NON_RANDOM_ERROR_TOLERANCE,
        )


@absltest.skip("20% RPC might pass immediately, reason unknown")
class TestAlwaysDelay(xds_url_map_testcase.XdsUrlMapTestCase):
    @staticmethod
    def is_supported(config: skips.TestConfig) -> bool:
        return _is_supported(config)

    @staticmethod
    def url_map_change(
        host_rule: HostRule, path_matcher: PathMatcher
    ) -> Tuple[HostRule, PathMatcher]:
        path_matcher["routeRules"] = [
            _build_fault_injection_route_rule(
                abort_percentage=0, delay_percentage=100
            )
        ]
        return host_rule, path_matcher

    def xds_config_validate(self, xds_config: DumpedXdsConfig):
        self.assertNumEndpoints(xds_config, 1)
        filter_config = xds_config.rds["virtualHosts"][0]["routes"][0][
            "typedPerFilterConfig"
        ]["envoy.filters.http.fault"]
        self.assertEqual("20s", filter_config["delay"]["fixedDelay"])
        self.assertEqual(
            1000000, filter_config["delay"]["percentage"]["numerator"]
        )
        self.assertEqual(
            "MILLION", filter_config["delay"]["percentage"]["denominator"]
        )

    def rpc_distribution_validate(self, test_client: XdsTestClient):
        self.configure_and_send(
            test_client,
            rpc_types=(RpcTypeUnaryCall,),
            num_rpcs=_NUM_RPCS,
            app_timeout=_DELAY_CASE_APPLICATION_TIMEOUT_SEC,
        )
        _wait_until_backlog_cleared(test_client)
        self.assertRpcStatusCode(
            test_client,
            expected=(
                ExpectedResult(
                    rpc_type=RpcTypeUnaryCall,
                    status_code=grpc.StatusCode.DEADLINE_EXCEEDED,
                    ratio=1,
                ),
            ),
            length=_LENGTH_OF_RPC_SENDING_SEC,
            tolerance=_NON_RANDOM_ERROR_TOLERANCE,
        )


class TestAlwaysAbort(xds_url_map_testcase.XdsUrlMapTestCase):
    @staticmethod
    def is_supported(config: skips.TestConfig) -> bool:
        return _is_supported(config)

    @staticmethod
    def url_map_change(
        host_rule: HostRule, path_matcher: PathMatcher
    ) -> Tuple[HostRule, PathMatcher]:
        path_matcher["routeRules"] = [
            _build_fault_injection_route_rule(
                abort_percentage=100, delay_percentage=0
            )
        ]
        return host_rule, path_matcher

    def xds_config_validate(self, xds_config: DumpedXdsConfig):
        self.assertNumEndpoints(xds_config, 1)
        filter_config = xds_config.rds["virtualHosts"][0]["routes"][0][
            "typedPerFilterConfig"
        ]["envoy.filters.http.fault"]
        self.assertEqual(401, filter_config["abort"]["httpStatus"])
        self.assertEqual(
            1000000, filter_config["abort"]["percentage"]["numerator"]
        )
        self.assertEqual(
            "MILLION", filter_config["abort"]["percentage"]["denominator"]
        )

    def rpc_distribution_validate(self, test_client: XdsTestClient):
        self.configure_and_send(
            test_client, rpc_types=(RpcTypeUnaryCall,), num_rpcs=_NUM_RPCS
        )
        self.assertRpcStatusCode(
            test_client,
            expected=(
                ExpectedResult(
                    rpc_type=RpcTypeUnaryCall,
                    status_code=grpc.StatusCode.UNAUTHENTICATED,
                    ratio=1,
                ),
            ),
            length=_LENGTH_OF_RPC_SENDING_SEC,
            tolerance=_NON_RANDOM_ERROR_TOLERANCE,
        )


class TestDelayHalf(xds_url_map_testcase.XdsUrlMapTestCase):
    @staticmethod
    def is_supported(config: skips.TestConfig) -> bool:
        return _is_supported(config)

    @staticmethod
    def url_map_change(
        host_rule: HostRule, path_matcher: PathMatcher
    ) -> Tuple[HostRule, PathMatcher]:
        path_matcher["routeRules"] = [
            _build_fault_injection_route_rule(
                abort_percentage=0, delay_percentage=50
            )
        ]
        return host_rule, path_matcher

    def xds_config_validate(self, xds_config: DumpedXdsConfig):
        self.assertNumEndpoints(xds_config, 1)
        filter_config = xds_config.rds["virtualHosts"][0]["routes"][0][
            "typedPerFilterConfig"
        ]["envoy.filters.http.fault"]
        self.assertEqual("20s", filter_config["delay"]["fixedDelay"])
        self.assertEqual(
            500000, filter_config["delay"]["percentage"]["numerator"]
        )
        self.assertEqual(
            "MILLION", filter_config["delay"]["percentage"]["denominator"]
        )

    def rpc_distribution_validate(self, test_client: XdsTestClient):
        self.configure_and_send(
            test_client,
            rpc_types=(RpcTypeUnaryCall,),
            num_rpcs=_NUM_RPCS,
            app_timeout=_DELAY_CASE_APPLICATION_TIMEOUT_SEC,
        )
        _wait_until_backlog_cleared(test_client)
        self.assertRpcStatusCode(
            test_client,
            expected=(
                ExpectedResult(
                    rpc_type=RpcTypeUnaryCall,
                    status_code=grpc.StatusCode.DEADLINE_EXCEEDED,
                    ratio=0.5,
                ),
            ),
            length=_LENGTH_OF_RPC_SENDING_SEC,
            tolerance=_ERROR_TOLERANCE,
        )


class TestAbortHalf(xds_url_map_testcase.XdsUrlMapTestCase):
    @staticmethod
    def is_supported(config: skips.TestConfig) -> bool:
        return _is_supported(config)

    @staticmethod
    def url_map_change(
        host_rule: HostRule, path_matcher: PathMatcher
    ) -> Tuple[HostRule, PathMatcher]:
        path_matcher["routeRules"] = [
            _build_fault_injection_route_rule(
                abort_percentage=50, delay_percentage=0
            )
        ]
        return host_rule, path_matcher

    def xds_config_validate(self, xds_config: DumpedXdsConfig):
        self.assertNumEndpoints(xds_config, 1)
        filter_config = xds_config.rds["virtualHosts"][0]["routes"][0][
            "typedPerFilterConfig"
        ]["envoy.filters.http.fault"]
        self.assertEqual(401, filter_config["abort"]["httpStatus"])
        self.assertEqual(
            500000, filter_config["abort"]["percentage"]["numerator"]
        )
        self.assertEqual(
            "MILLION", filter_config["abort"]["percentage"]["denominator"]
        )

    def rpc_distribution_validate(self, test_client: XdsTestClient):
        self.configure_and_send(
            test_client, rpc_types=(RpcTypeUnaryCall,), num_rpcs=_NUM_RPCS
        )
        self.assertRpcStatusCode(
            test_client,
            expected=(
                ExpectedResult(
                    rpc_type=RpcTypeUnaryCall,
                    status_code=grpc.StatusCode.UNAUTHENTICATED,
                    ratio=0.5,
                ),
            ),
            length=_LENGTH_OF_RPC_SENDING_SEC,
            tolerance=_ERROR_TOLERANCE,
        )


if __name__ == "__main__":
    absltest.main()

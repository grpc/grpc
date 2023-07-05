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
from typing import Tuple

from absl import flags
from absl.testing import absltest

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
_Lang = skips.Lang

logger = logging.getLogger(__name__)
flags.adopt_module_key_flags(xds_url_map_testcase)

_NUM_RPCS = 150


def _is_supported(config: skips.TestConfig) -> bool:
    if config.client_lang == _Lang.NODE:
        return config.version_gte("v1.3.x")
    return True


class TestFullPathMatchEmptyCall(xds_url_map_testcase.XdsUrlMapTestCase):
    @staticmethod
    def is_supported(config: skips.TestConfig) -> bool:
        return _is_supported(config)

    @staticmethod
    def url_map_change(
        host_rule: HostRule, path_matcher: PathMatcher
    ) -> Tuple[HostRule, PathMatcher]:
        path_matcher["routeRules"] = [
            {
                "priority": 0,
                # FullPath EmptyCall -> alternate_backend_service.
                "matchRules": [
                    {"fullPathMatch": "/grpc.testing.TestService/EmptyCall"}
                ],
                "service": GcpResourceManager().alternative_backend_service(),
            }
        ]
        return host_rule, path_matcher

    def xds_config_validate(self, xds_config: DumpedXdsConfig):
        self.assertNumEndpoints(xds_config, 2)
        self.assertEqual(
            xds_config.rds["virtualHosts"][0]["routes"][0]["match"]["path"],
            "/grpc.testing.TestService/EmptyCall",
        )

    def rpc_distribution_validate(self, test_client: XdsTestClient):
        rpc_distribution = self.configure_and_send(
            test_client, rpc_types=[RpcTypeEmptyCall], num_rpcs=_NUM_RPCS
        )
        self.assertEqual(
            _NUM_RPCS, rpc_distribution.empty_call_alternative_service_rpc_count
        )


class TestFullPathMatchUnaryCall(xds_url_map_testcase.XdsUrlMapTestCase):
    @staticmethod
    def is_supported(config: skips.TestConfig) -> bool:
        return _is_supported(config)

    @staticmethod
    def url_map_change(
        host_rule: HostRule, path_matcher: PathMatcher
    ) -> Tuple[HostRule, PathMatcher]:
        path_matcher["routeRules"] = [
            {
                "priority": 0,
                # FullPath EmptyCall -> alternate_backend_service.
                "matchRules": [
                    {"fullPathMatch": "/grpc.testing.TestService/UnaryCall"}
                ],
                "service": GcpResourceManager().alternative_backend_service(),
            }
        ]
        return host_rule, path_matcher

    def xds_config_validate(self, xds_config: DumpedXdsConfig):
        self.assertNumEndpoints(xds_config, 2)
        self.assertEqual(
            xds_config.rds["virtualHosts"][0]["routes"][0]["match"]["path"],
            "/grpc.testing.TestService/UnaryCall",
        )

    def rpc_distribution_validate(self, test_client: XdsTestClient):
        rpc_distribution = self.configure_and_send(
            test_client, rpc_types=(RpcTypeUnaryCall,), num_rpcs=_NUM_RPCS
        )
        self.assertEqual(
            _NUM_RPCS, rpc_distribution.unary_call_alternative_service_rpc_count
        )


class TestTwoRoutesAndPrefixMatch(xds_url_map_testcase.XdsUrlMapTestCase):
    """This test case is similar to the one above (but with route services
    swapped). This test has two routes (full_path and the default) to match
    EmptyCall, and both routes set alternative_backend_service as the action.
    This forces the client to handle duplicate Clusters in the RDS response."""

    @staticmethod
    def is_supported(config: skips.TestConfig) -> bool:
        return _is_supported(config)

    @staticmethod
    def url_map_change(
        host_rule: HostRule, path_matcher: PathMatcher
    ) -> Tuple[HostRule, PathMatcher]:
        path_matcher["routeRules"] = [
            {
                "priority": 0,
                # Prefix UnaryCall -> default_backend_service.
                "matchRules": [
                    {"prefixMatch": "/grpc.testing.TestService/Unary"}
                ],
                "service": GcpResourceManager().default_backend_service(),
            },
            {
                "priority": 1,
                # FullPath EmptyCall -> alternate_backend_service.
                "matchRules": [
                    {"fullPathMatch": "/grpc.testing.TestService/EmptyCall"}
                ],
                "service": GcpResourceManager().alternative_backend_service(),
            },
        ]
        return host_rule, path_matcher

    def xds_config_validate(self, xds_config: DumpedXdsConfig):
        self.assertNumEndpoints(xds_config, 2)
        self.assertEqual(
            xds_config.rds["virtualHosts"][0]["routes"][0]["match"]["prefix"],
            "/grpc.testing.TestService/Unary",
        )
        self.assertEqual(
            xds_config.rds["virtualHosts"][0]["routes"][1]["match"]["path"],
            "/grpc.testing.TestService/EmptyCall",
        )

    def rpc_distribution_validate(self, test_client: XdsTestClient):
        rpc_distribution = self.configure_and_send(
            test_client,
            rpc_types=[RpcTypeUnaryCall, RpcTypeEmptyCall],
            num_rpcs=_NUM_RPCS,
        )
        self.assertEqual(0, rpc_distribution.num_failures)
        self.assertEqual(
            0, rpc_distribution.unary_call_alternative_service_rpc_count
        )
        self.assertEqual(
            0, rpc_distribution.empty_call_default_service_rpc_count
        )


class TestRegexMatch(xds_url_map_testcase.XdsUrlMapTestCase):
    @staticmethod
    def is_supported(config: skips.TestConfig) -> bool:
        return _is_supported(config)

    @staticmethod
    def url_map_change(
        host_rule: HostRule, path_matcher: PathMatcher
    ) -> Tuple[HostRule, PathMatcher]:
        path_matcher["routeRules"] = [
            {
                "priority": 0,
                # Regex UnaryCall -> alternate_backend_service.
                "matchRules": [
                    {
                        "regexMatch": (  # Unary methods with any services.
                            r"^\/.*\/UnaryCall$"
                        )
                    }
                ],
                "service": GcpResourceManager().alternative_backend_service(),
            }
        ]
        return host_rule, path_matcher

    def xds_config_validate(self, xds_config: DumpedXdsConfig):
        self.assertNumEndpoints(xds_config, 2)
        self.assertEqual(
            xds_config.rds["virtualHosts"][0]["routes"][0]["match"][
                "safeRegex"
            ]["regex"],
            r"^\/.*\/UnaryCall$",
        )

    def rpc_distribution_validate(self, test_client: XdsTestClient):
        rpc_distribution = self.configure_and_send(
            test_client, rpc_types=(RpcTypeUnaryCall,), num_rpcs=_NUM_RPCS
        )
        self.assertEqual(
            _NUM_RPCS, rpc_distribution.unary_call_alternative_service_rpc_count
        )


class TestCaseInsensitiveMatch(xds_url_map_testcase.XdsUrlMapTestCase):
    @staticmethod
    def is_supported(config: skips.TestConfig) -> bool:
        return _is_supported(config)

    @staticmethod
    def url_map_change(
        host_rule: HostRule, path_matcher: PathMatcher
    ) -> Tuple[HostRule, PathMatcher]:
        path_matcher["routeRules"] = [
            {
                "priority": 0,
                # ignoreCase EmptyCall -> alternate_backend_service.
                "matchRules": [
                    {
                        # Case insensitive matching.
                        "fullPathMatch": "/gRpC.tEsTinG.tEstseRvice/empTycaLl",
                        "ignoreCase": True,
                    }
                ],
                "service": GcpResourceManager().alternative_backend_service(),
            }
        ]
        return host_rule, path_matcher

    def xds_config_validate(self, xds_config: DumpedXdsConfig):
        self.assertNumEndpoints(xds_config, 2)
        self.assertEqual(
            xds_config.rds["virtualHosts"][0]["routes"][0]["match"]["path"],
            "/gRpC.tEsTinG.tEstseRvice/empTycaLl",
        )
        self.assertEqual(
            xds_config.rds["virtualHosts"][0]["routes"][0]["match"][
                "caseSensitive"
            ],
            False,
        )

    def rpc_distribution_validate(self, test_client: XdsTestClient):
        rpc_distribution = self.configure_and_send(
            test_client, rpc_types=[RpcTypeEmptyCall], num_rpcs=_NUM_RPCS
        )
        self.assertEqual(
            _NUM_RPCS, rpc_distribution.empty_call_alternative_service_rpc_count
        )


if __name__ == "__main__":
    absltest.main()

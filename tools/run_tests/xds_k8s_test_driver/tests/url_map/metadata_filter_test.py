# Copyright 2022 The gRPC Authors
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
from framework.test_app import client_app

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

_NUM_RPCS = 150
_TEST_METADATA_KEY = "xds_md"
_TEST_METADATA_VALUE_EMPTY = "empty_ytpme"
_TEST_METADATA = (
    (RpcTypeEmptyCall, _TEST_METADATA_KEY, _TEST_METADATA_VALUE_EMPTY),
)
match_labels = [
    {"name": "TRAFFICDIRECTOR_NETWORK_NAME", "value": "default-vpc"}
]
not_match_labels = [{"name": "fake", "value": "fail"}]


class TestMetadataFilterMatchAll(xds_url_map_testcase.XdsUrlMapTestCase):
    """ " The test url-map has two routeRules: the higher priority routes to
    the default backends, but is supposed to be filtered out by TD because
    of non-matching metadata filters. The lower priority routes to alternative
    backends and metadata filter matches. Thus, it verifies that TD evaluates
    metadata filters correctly."""

    @staticmethod
    def url_map_change(
        host_rule: HostRule, path_matcher: PathMatcher
    ) -> Tuple[HostRule, PathMatcher]:
        path_matcher["routeRules"] = [
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
                "service": GcpResourceManager().default_backend_service(),
            },
            {
                "priority": 1,
                "matchRules": [
                    {
                        "prefixMatch": "/grpc.testing.TestService/Empty",
                        "headerMatches": [
                            {
                                "headerName": _TEST_METADATA_KEY,
                                "exactMatch": _TEST_METADATA_VALUE_EMPTY,
                            }
                        ],
                        "metadataFilters": [
                            {
                                "filterMatchCriteria": "MATCH_ALL",
                                "filterLabels": match_labels,
                            }
                        ],
                    }
                ],
                "service": GcpResourceManager().alternative_backend_service(),
            },
        ]
        return host_rule, path_matcher

    def xds_config_validate(self, xds_config: DumpedXdsConfig):
        self.assertNumEndpoints(xds_config, 2)
        self.assertEqual(len(xds_config.rds["virtualHosts"][0]["routes"]), 2)
        self.assertEqual(
            xds_config.rds["virtualHosts"][0]["routes"][0]["match"]["prefix"],
            "/grpc.testing.TestService/Empty",
        )
        self.assertEqual(
            xds_config.rds["virtualHosts"][0]["routes"][0]["match"]["headers"][
                0
            ]["name"],
            _TEST_METADATA_KEY,
        )
        self.assertEqual(
            xds_config.rds["virtualHosts"][0]["routes"][0]["match"]["headers"][
                0
            ]["exactMatch"],
            _TEST_METADATA_VALUE_EMPTY,
        )
        self.assertEqual(
            xds_config.rds["virtualHosts"][0]["routes"][1]["match"]["prefix"],
            "",
        )
        self.assertEqual(2, 3)

    def rpc_distribution_validate(self, test_client: XdsTestClient):
        rpc_distribution = self.configure_and_send(
            test_client,
            rpc_types=[RpcTypeEmptyCall],
            metadata=_TEST_METADATA,
            num_rpcs=_NUM_RPCS,
        )
        self.assertEqual(
            _NUM_RPCS, rpc_distribution.empty_call_alternative_service_rpc_count
        )


class TestMetadataFilterMatchAny(xds_url_map_testcase.XdsUrlMapTestCase):
    @staticmethod
    def url_map_change(
        host_rule: HostRule, path_matcher: PathMatcher
    ) -> Tuple[HostRule, PathMatcher]:
        path_matcher["routeRules"] = [
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
                "service": GcpResourceManager().default_backend_service(),
            },
            {
                "priority": 1,
                "matchRules": [
                    {
                        "prefixMatch": "/grpc.testing.TestService/Unary",
                        "metadataFilters": [
                            {
                                "filterMatchCriteria": "MATCH_ANY",
                                "filterLabels": not_match_labels + match_labels,
                            }
                        ],
                    }
                ],
                "service": GcpResourceManager().alternative_backend_service(),
            },
        ]
        return host_rule, path_matcher

    def xds_config_validate(self, xds_config: DumpedXdsConfig):
        self.assertNumEndpoints(xds_config, 2)
        self.assertEqual(len(xds_config.rds["virtualHosts"][0]["routes"]), 2)
        self.assertEqual(
            xds_config.rds["virtualHosts"][0]["routes"][0]["match"]["prefix"],
            "/grpc.testing.TestService/Unary",
        )
        self.assertEqual(
            xds_config.rds["virtualHosts"][0]["routes"][1]["match"]["prefix"],
            "",
        )

    def rpc_distribution_validate(self, test_client: XdsTestClient):
        rpc_distribution = self.configure_and_send(
            test_client, rpc_types=(RpcTypeUnaryCall,), num_rpcs=_NUM_RPCS
        )
        self.assertEqual(
            _NUM_RPCS, rpc_distribution.unary_call_alternative_service_rpc_count
        )


class TestMetadataFilterMatchAnyAndAll(xds_url_map_testcase.XdsUrlMapTestCase):
    @staticmethod
    def url_map_change(
        host_rule: HostRule, path_matcher: PathMatcher
    ) -> Tuple[HostRule, PathMatcher]:
        path_matcher["routeRules"] = [
            {
                "priority": 0,
                "matchRules": [
                    {
                        "prefixMatch": "/",
                        "metadataFilters": [
                            {
                                "filterMatchCriteria": "MATCH_ALL",
                                "filterLabels": not_match_labels + match_labels,
                            }
                        ],
                    }
                ],
                "service": GcpResourceManager().default_backend_service(),
            },
            {
                "priority": 1,
                "matchRules": [
                    {
                        "prefixMatch": "/grpc.testing.TestService/Unary",
                        "metadataFilters": [
                            {
                                "filterMatchCriteria": "MATCH_ANY",
                                "filterLabels": not_match_labels + match_labels,
                            }
                        ],
                    }
                ],
                "service": GcpResourceManager().alternative_backend_service(),
            },
        ]
        return host_rule, path_matcher

    def xds_config_validate(self, xds_config: DumpedXdsConfig):
        self.assertNumEndpoints(xds_config, 2)
        self.assertEqual(len(xds_config.rds["virtualHosts"][0]["routes"]), 2)
        self.assertEqual(
            xds_config.rds["virtualHosts"][0]["routes"][0]["match"]["prefix"],
            "/grpc.testing.TestService/Unary",
        )
        self.assertEqual(
            xds_config.rds["virtualHosts"][0]["routes"][1]["match"]["prefix"],
            "",
        )

    def rpc_distribution_validate(self, test_client: XdsTestClient):
        rpc_distribution = self.configure_and_send(
            test_client, rpc_types=(RpcTypeUnaryCall,), num_rpcs=_NUM_RPCS
        )
        self.assertEqual(
            _NUM_RPCS, rpc_distribution.unary_call_alternative_service_rpc_count
        )


class TestMetadataFilterMatchMultipleRules(
    xds_url_map_testcase.XdsUrlMapTestCase
):
    @staticmethod
    def url_map_change(
        host_rule: HostRule, path_matcher: PathMatcher
    ) -> Tuple[HostRule, PathMatcher]:
        path_matcher["routeRules"] = [
            {
                "priority": 0,
                "matchRules": [
                    {
                        "prefixMatch": "/",
                        "headerMatches": [
                            {
                                "headerName": _TEST_METADATA_KEY,
                                "exactMatch": _TEST_METADATA_VALUE_EMPTY,
                            }
                        ],
                        "metadataFilters": [
                            {
                                "filterMatchCriteria": "MATCH_ANY",
                                "filterLabels": match_labels,
                            }
                        ],
                    }
                ],
                "service": GcpResourceManager().alternative_backend_service(),
            },
            {
                "priority": 1,
                "matchRules": [
                    {
                        "prefixMatch": "/grpc.testing.TestService/Unary",
                        "metadataFilters": [
                            {
                                "filterMatchCriteria": "MATCH_ALL",
                                "filterLabels": match_labels,
                            }
                        ],
                    }
                ],
                "service": GcpResourceManager().default_backend_service(),
            },
        ]
        return host_rule, path_matcher

    def xds_config_validate(self, xds_config: DumpedXdsConfig):
        self.assertNumEndpoints(xds_config, 2)
        self.assertEqual(len(xds_config.rds["virtualHosts"][0]["routes"]), 3)
        self.assertEqual(
            xds_config.rds["virtualHosts"][0]["routes"][0]["match"]["headers"][
                0
            ]["name"],
            _TEST_METADATA_KEY,
        )
        self.assertEqual(
            xds_config.rds["virtualHosts"][0]["routes"][0]["match"]["headers"][
                0
            ]["exactMatch"],
            _TEST_METADATA_VALUE_EMPTY,
        )
        self.assertEqual(
            xds_config.rds["virtualHosts"][0]["routes"][1]["match"]["prefix"],
            "/grpc.testing.TestService/Unary",
        )
        self.assertEqual(
            xds_config.rds["virtualHosts"][0]["routes"][2]["match"]["prefix"],
            "",
        )

    def rpc_distribution_validate(self, test_client: XdsTestClient):
        rpc_distribution = self.configure_and_send(
            test_client,
            rpc_types=[RpcTypeEmptyCall],
            metadata=_TEST_METADATA,
            num_rpcs=_NUM_RPCS,
        )
        self.assertEqual(
            _NUM_RPCS, rpc_distribution.empty_call_alternative_service_rpc_count
        )


if __name__ == "__main__":
    absltest.main()

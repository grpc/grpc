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
_TEST_METADATA_KEY = "xds_md"
_TEST_METADATA_VALUE_UNARY = "unary_yranu"
_TEST_METADATA_VALUE_EMPTY = "empty_ytpme"
_TEST_METADATA_NUMERIC_KEY = "xds_md_numeric"
_TEST_METADATA_NUMERIC_VALUE = "159"

_TEST_METADATA = (
    (RpcTypeUnaryCall, _TEST_METADATA_KEY, _TEST_METADATA_VALUE_UNARY),
    (RpcTypeEmptyCall, _TEST_METADATA_KEY, _TEST_METADATA_VALUE_EMPTY),
    (
        RpcTypeUnaryCall,
        _TEST_METADATA_NUMERIC_KEY,
        _TEST_METADATA_NUMERIC_VALUE,
    ),
)


def _is_supported(config: skips.TestConfig) -> bool:
    if config.client_lang == _Lang.NODE:
        return config.version_gte("v1.3.x")
    return True


class TestExactMatch(xds_url_map_testcase.XdsUrlMapTestCase):
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
                "service": GcpResourceManager().alternative_backend_service(),
            }
        ]
        return host_rule, path_matcher

    def xds_config_validate(self, xds_config: DumpedXdsConfig):
        self.assertNumEndpoints(xds_config, 2)
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


@absltest.skip("the xDS config is good, but distribution is wrong.")
class TestPrefixMatch(xds_url_map_testcase.XdsUrlMapTestCase):
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
                # Header PrefixMatch -> alternate_backend_service.
                # UnaryCall is sent with the metadata.
                "matchRules": [
                    {
                        "prefixMatch": "/",
                        "headerMatches": [
                            {
                                "headerName": _TEST_METADATA_KEY,
                                "prefixMatch": _TEST_METADATA_VALUE_UNARY[:2],
                            }
                        ],
                    }
                ],
                "service": GcpResourceManager().alternative_backend_service(),
            }
        ]
        return host_rule, path_matcher

    def xds_config_validate(self, xds_config: DumpedXdsConfig):
        self.assertNumEndpoints(xds_config, 2)
        self.assertEqual(
            xds_config.rds["virtualHosts"][0]["routes"][0]["match"]["headers"][
                0
            ]["name"],
            _TEST_METADATA_KEY,
        )
        self.assertEqual(
            xds_config.rds["virtualHosts"][0]["routes"][0]["match"]["headers"][
                0
            ]["prefixMatch"],
            _TEST_METADATA_VALUE_UNARY[:2],
        )

    def rpc_distribution_validate(self, test_client: XdsTestClient):
        rpc_distribution = self.configure_and_send(
            test_client,
            rpc_types=(RpcTypeUnaryCall,),
            metadata=_TEST_METADATA,
            num_rpcs=_NUM_RPCS,
        )
        self.assertEqual(
            _NUM_RPCS, rpc_distribution.unary_call_alternative_service_rpc_count
        )


class TestSuffixMatch(xds_url_map_testcase.XdsUrlMapTestCase):
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
                # Header SuffixMatch -> alternate_backend_service.
                # EmptyCall is sent with the metadata.
                "matchRules": [
                    {
                        "prefixMatch": "/",
                        "headerMatches": [
                            {
                                "headerName": _TEST_METADATA_KEY,
                                "suffixMatch": _TEST_METADATA_VALUE_EMPTY[-2:],
                            }
                        ],
                    }
                ],
                "service": GcpResourceManager().alternative_backend_service(),
            }
        ]
        return host_rule, path_matcher

    def xds_config_validate(self, xds_config: DumpedXdsConfig):
        self.assertNumEndpoints(xds_config, 2)
        self.assertEqual(
            xds_config.rds["virtualHosts"][0]["routes"][0]["match"]["headers"][
                0
            ]["name"],
            _TEST_METADATA_KEY,
        )
        self.assertEqual(
            xds_config.rds["virtualHosts"][0]["routes"][0]["match"]["headers"][
                0
            ]["suffixMatch"],
            _TEST_METADATA_VALUE_EMPTY[-2:],
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


class TestPresentMatch(xds_url_map_testcase.XdsUrlMapTestCase):
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
                "service": GcpResourceManager().alternative_backend_service(),
            }
        ]
        return host_rule, path_matcher

    def xds_config_validate(self, xds_config: DumpedXdsConfig):
        self.assertNumEndpoints(xds_config, 2)
        self.assertEqual(
            xds_config.rds["virtualHosts"][0]["routes"][0]["match"]["headers"][
                0
            ]["name"],
            _TEST_METADATA_NUMERIC_KEY,
        )
        self.assertEqual(
            xds_config.rds["virtualHosts"][0]["routes"][0]["match"]["headers"][
                0
            ]["presentMatch"],
            True,
        )

    def rpc_distribution_validate(self, test_client: XdsTestClient):
        rpc_distribution = self.configure_and_send(
            test_client,
            rpc_types=(RpcTypeUnaryCall,),
            metadata=_TEST_METADATA,
            num_rpcs=_NUM_RPCS,
        )
        self.assertEqual(
            _NUM_RPCS, rpc_distribution.unary_call_alternative_service_rpc_count
        )


class TestInvertMatch(xds_url_map_testcase.XdsUrlMapTestCase):
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
                # Header invert ExactMatch -> alternate_backend_service.
                # UnaryCall is sent with the metadata, so will be sent to
                # default. EmptyCall will be sent to alternative.
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
                "service": GcpResourceManager().alternative_backend_service(),
            }
        ]
        return host_rule, path_matcher

    def xds_config_validate(self, xds_config: DumpedXdsConfig):
        self.assertNumEndpoints(xds_config, 2)
        self.assertEqual(
            xds_config.rds["virtualHosts"][0]["routes"][0]["match"]["headers"][
                0
            ]["name"],
            _TEST_METADATA_KEY,
        )
        self.assertEqual(
            xds_config.rds["virtualHosts"][0]["routes"][0]["match"]["headers"][
                0
            ]["invertMatch"],
            True,
        )

    def rpc_distribution_validate(self, test_client: XdsTestClient):
        rpc_distribution = self.configure_and_send(
            test_client,
            rpc_types=[RpcTypeUnaryCall, RpcTypeEmptyCall],
            metadata=_TEST_METADATA,
            num_rpcs=_NUM_RPCS,
        )
        self.assertEqual(_NUM_RPCS, rpc_distribution.num_oks)
        self.assertEqual(
            0, rpc_distribution.unary_call_alternative_service_rpc_count
        )
        self.assertEqual(
            0, rpc_distribution.empty_call_default_service_rpc_count
        )


class TestRangeMatch(xds_url_map_testcase.XdsUrlMapTestCase):
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
                "service": GcpResourceManager().alternative_backend_service(),
            }
        ]
        return host_rule, path_matcher

    def xds_config_validate(self, xds_config: DumpedXdsConfig):
        self.assertNumEndpoints(xds_config, 2)
        self.assertEqual(
            xds_config.rds["virtualHosts"][0]["routes"][0]["match"]["headers"][
                0
            ]["name"],
            _TEST_METADATA_NUMERIC_KEY,
        )
        self.assertEqual(
            xds_config.rds["virtualHosts"][0]["routes"][0]["match"]["headers"][
                0
            ]["rangeMatch"]["start"],
            "100",
        )
        self.assertEqual(
            xds_config.rds["virtualHosts"][0]["routes"][0]["match"]["headers"][
                0
            ]["rangeMatch"]["end"],
            "200",
        )

    def rpc_distribution_validate(self, test_client: XdsTestClient):
        rpc_distribution = self.configure_and_send(
            test_client,
            rpc_types=[RpcTypeUnaryCall, RpcTypeEmptyCall],
            metadata=_TEST_METADATA,
            num_rpcs=_NUM_RPCS,
        )
        self.assertEqual(_NUM_RPCS, rpc_distribution.num_oks)
        self.assertEqual(
            0, rpc_distribution.unary_call_default_service_rpc_count
        )
        self.assertEqual(
            0, rpc_distribution.empty_call_alternative_service_rpc_count
        )


class TestRegexMatch(xds_url_map_testcase.XdsUrlMapTestCase):
    @staticmethod
    def is_supported(config: skips.TestConfig) -> bool:
        return _is_supported(config)

    @staticmethod
    def url_map_change(
        host_rule: HostRule, path_matcher: PathMatcher
    ) -> Tuple[HostRule, PathMatcher]:
        path_matcher["routeRules"] = (
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
                    "service": GcpResourceManager().alternative_backend_service(),
                }
            ],
        )
        return host_rule, path_matcher

    def xds_config_validate(self, xds_config: DumpedXdsConfig):
        self.assertNumEndpoints(xds_config, 2)
        self.assertEqual(
            xds_config.rds["virtualHosts"][0]["routes"][0]["match"]["headers"][
                0
            ]["name"],
            _TEST_METADATA_KEY,
        )
        self.assertEqual(
            xds_config.rds["virtualHosts"][0]["routes"][0]["match"]["headers"][
                0
            ]["safeRegexMatch"]["regex"],
            "^%s.*%s$"
            % (_TEST_METADATA_VALUE_EMPTY[:2], _TEST_METADATA_VALUE_EMPTY[-2:]),
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

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
from framework.infrastructure import traffic_director
from framework.rpc import grpc_channelz
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
_TEST_METADATA_KEY = traffic_director.TEST_AFFINITY_METADATA_KEY
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

_ChannelzChannelState = grpc_channelz.ChannelState


def _is_supported(config: skips.TestConfig) -> bool:
    # Per "Ring hash" in
    # https://github.com/grpc/grpc/blob/master/doc/grpc_xds_features.md
    if config.client_lang in _Lang.CPP | _Lang.JAVA:
        return config.version_gte("v1.40.x")
    elif config.client_lang == _Lang.GO:
        return config.version_gte("v1.41.x")
    elif config.client_lang == _Lang.PYTHON:
        # TODO(https://github.com/grpc/grpc/issues/27430): supported after
        #      the issue is fixed.
        return False
    elif config.client_lang == _Lang.NODE:
        return False
    return True


class TestHeaderBasedAffinity(xds_url_map_testcase.XdsUrlMapTestCase):
    @staticmethod
    def is_supported(config: skips.TestConfig) -> bool:
        return _is_supported(config)

    @staticmethod
    def client_init_config(rpc: str, metadata: str):
        # Config the init RPCs to send with the same set of metadata. Without
        # this, the init RPCs will not have headers, and will pick random
        # backends (behavior of RING_HASH). This is necessary to only one
        # sub-channel is picked and used from the beginning, thus the channel
        # will only create this one sub-channel.
        return "EmptyCall", "EmptyCall:%s:%s" % (
            _TEST_METADATA_KEY,
            _TEST_METADATA_VALUE_EMPTY,
        )

    @staticmethod
    def url_map_change(
        host_rule: HostRule, path_matcher: PathMatcher
    ) -> Tuple[HostRule, PathMatcher]:
        # Update default service to the affinity service.
        path_matcher[
            "defaultService"
        ] = GcpResourceManager().affinity_backend_service()
        return host_rule, path_matcher

    def xds_config_validate(self, xds_config: DumpedXdsConfig):
        # 3 endpoints in the affinity backend service.
        self.assertNumEndpoints(xds_config, 3)
        self.assertEqual(
            xds_config.rds["virtualHosts"][0]["routes"][0]["route"][
                "hashPolicy"
            ][0]["header"]["headerName"],
            _TEST_METADATA_KEY,
        )
        self.assertEqual(xds_config.cds[0]["lbPolicy"], "RING_HASH")

    def rpc_distribution_validate(self, test_client: XdsTestClient):
        rpc_distribution = self.configure_and_send(
            test_client,
            rpc_types=[RpcTypeEmptyCall],
            metadata=_TEST_METADATA,
            num_rpcs=_NUM_RPCS,
        )
        # Only one backend should receive traffic, even though there are 3
        # backends.
        self.assertEqual(1, rpc_distribution.num_peers)
        self.assertLen(
            test_client.find_subchannels_with_state(
                _ChannelzChannelState.READY
            ),
            1,
        )
        self.assertLen(
            test_client.find_subchannels_with_state(_ChannelzChannelState.IDLE),
            2,
        )
        # Send 150 RPCs without headers. RPCs without headers will pick random
        # backends. After this, we expect to see all backends to be connected.
        rpc_distribution = self.configure_and_send(
            test_client,
            rpc_types=[RpcTypeEmptyCall, RpcTypeUnaryCall],
            num_rpcs=_NUM_RPCS,
        )
        self.assertEqual(3, rpc_distribution.num_peers)
        self.assertLen(
            test_client.find_subchannels_with_state(
                _ChannelzChannelState.READY
            ),
            3,
        )


class TestHeaderBasedAffinityMultipleHeaders(
    xds_url_map_testcase.XdsUrlMapTestCase
):
    @staticmethod
    def is_supported(config: skips.TestConfig) -> bool:
        return _is_supported(config)

    @staticmethod
    def client_init_config(rpc: str, metadata: str):
        # Config the init RPCs to send with the same set of metadata. Without
        # this, the init RPCs will not have headers, and will pick random
        # backends (behavior of RING_HASH). This is necessary to only one
        # sub-channel is picked and used from the beginning, thus the channel
        # will only create this one sub-channel.
        return "EmptyCall", "EmptyCall:%s:%s" % (
            _TEST_METADATA_KEY,
            _TEST_METADATA_VALUE_EMPTY,
        )

    @staticmethod
    def url_map_change(
        host_rule: HostRule, path_matcher: PathMatcher
    ) -> Tuple[HostRule, PathMatcher]:
        # Update default service to the affinity service.
        path_matcher[
            "defaultService"
        ] = GcpResourceManager().affinity_backend_service()
        return host_rule, path_matcher

    def xds_config_validate(self, xds_config: DumpedXdsConfig):
        # 3 endpoints in the affinity backend service.
        self.assertNumEndpoints(xds_config, 3)
        self.assertEqual(
            xds_config.rds["virtualHosts"][0]["routes"][0]["route"][
                "hashPolicy"
            ][0]["header"]["headerName"],
            _TEST_METADATA_KEY,
        )
        self.assertEqual(xds_config.cds[0]["lbPolicy"], "RING_HASH")

    def rpc_distribution_validate(self, test_client: XdsTestClient):
        rpc_distribution = self.configure_and_send(
            test_client,
            rpc_types=[RpcTypeEmptyCall],
            metadata=_TEST_METADATA,
            num_rpcs=_NUM_RPCS,
        )
        # Only one backend should receive traffic, even though there are 3
        # backends.
        self.assertEqual(1, rpc_distribution.num_peers)
        self.assertLen(
            test_client.find_subchannels_with_state(
                _ChannelzChannelState.READY
            ),
            1,
        )
        self.assertLen(
            test_client.find_subchannels_with_state(_ChannelzChannelState.IDLE),
            2,
        )
        empty_call_peer = list(
            rpc_distribution.raw["rpcsByMethod"]["EmptyCall"][
                "rpcsByPeer"
            ].keys()
        )[0]
        # Send RPCs with a different metadata value, try different values to
        # verify that the client will pick a different backend.
        #
        # EmptyCalls will be sent with the same metadata as before, and
        # UnaryCalls will be sent with headers from ["0".."29"]. We check the
        # endpoint picked for UnaryCall, and stop as soon as one different from
        # the EmptyCall peer is picked.
        #
        # Note that there's a small chance all the headers would still pick the
        # same backend used by EmptyCall. But there will be over a thousand
        # nodes on the ring (default min size is 1024), and the probability of
        # picking the same backend should be fairly small.
        different_peer_picked = False
        for i in range(30):
            new_metadata = (
                (
                    RpcTypeEmptyCall,
                    _TEST_METADATA_KEY,
                    _TEST_METADATA_VALUE_EMPTY,
                ),
                (RpcTypeUnaryCall, _TEST_METADATA_KEY, str(i)),
            )
            rpc_distribution = self.configure_and_send(
                test_client,
                rpc_types=[RpcTypeEmptyCall, RpcTypeUnaryCall],
                metadata=new_metadata,
                num_rpcs=_NUM_RPCS,
            )
            unary_call_peer = list(
                rpc_distribution.raw["rpcsByMethod"]["UnaryCall"][
                    "rpcsByPeer"
                ].keys()
            )[0]
            if unary_call_peer != empty_call_peer:
                different_peer_picked = True
                break
        self.assertTrue(
            different_peer_picked,
            (
                "the same endpoint was picked for all the headers, expect a "
                "different endpoint to be picked"
            ),
        )
        self.assertLen(
            test_client.find_subchannels_with_state(
                _ChannelzChannelState.READY
            ),
            2,
        )
        self.assertLen(
            test_client.find_subchannels_with_state(_ChannelzChannelState.IDLE),
            1,
        )


# TODO: add more test cases
# 1. based on the basic test, turn down the backend in use, then verify that all
#    RPCs are sent to another backend

if __name__ == "__main__":
    absltest.main()

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

from framework import xds_k8s_flags
from framework import xds_url_map_testcase
from framework.rpc import grpc_channelz
from framework.test_app import client_app
from framework.infrastructure import traffic_director

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
_TEST_METADATA_KEY = traffic_director.TEST_AFFINITY_METADATA_KEY
_TEST_METADATA_VALUE_UNARY = 'unary_yranu'
_TEST_METADATA_VALUE_EMPTY = 'empty_ytpme'
_TEST_METADATA_NUMERIC_KEY = 'xds_md_numeric'
_TEST_METADATA_NUMERIC_VALUE = '159'

_TEST_METADATA = (
    (RpcTypeUnaryCall, _TEST_METADATA_KEY, _TEST_METADATA_VALUE_UNARY),
    (RpcTypeEmptyCall, _TEST_METADATA_KEY, _TEST_METADATA_VALUE_EMPTY),
    (RpcTypeUnaryCall, _TEST_METADATA_NUMERIC_KEY,
     _TEST_METADATA_NUMERIC_VALUE),
)

_ChannelzChannelState = grpc_channelz.ChannelState


@absltest.skipUnless('cpp-client' in xds_k8s_flags.CLIENT_IMAGE.value or \
                     'java-client' in xds_k8s_flags.CLIENT_IMAGE.value,
                     'Affinity is currently only implemented in C++ and Java.')
class TestHeaderBasedAffinity(xds_url_map_testcase.XdsUrlMapTestCase):

    @staticmethod
    def client_init_config(rpc: str, metadata: str):
        # Config the init RPCs to send with the same set of metadata. Without
        # this, the init RPCs will not have headers, and will pick random
        # backends (behavior of RING_HASH). This is necessary to only one
        # sub-channel is picked and used from the beginning, thus the channel
        # will only create this one sub-channel.
        return 'EmptyCall', 'EmptyCall:%s:%s' % (_TEST_METADATA_KEY,
                                                 _TEST_METADATA_VALUE_EMPTY)

    @staticmethod
    def url_map_change(
            host_rule: HostRule,
            path_matcher: PathMatcher) -> Tuple[HostRule, PathMatcher]:
        # Update default service to the affinity service.
        path_matcher["defaultService"] = GcpResourceManager(
        ).affinity_backend_service()
        return host_rule, path_matcher

    def xds_config_validate(self, xds_config: DumpedXdsConfig):
        # 3 endpoints in the affinity backend service.
        self.assertNumEndpoints(xds_config, 3)
        self.assertEqual(
            xds_config.rds['virtualHosts'][0]['routes'][0]['route']
            ['hashPolicy'][0]['header']['headerName'], _TEST_METADATA_KEY)
        self.assertEqual(xds_config.cds[0]['lbPolicy'], 'RING_HASH')

    def rpc_distribution_validate(self, test_client: XdsTestClient):
        rpc_distribution = self.configure_and_send(test_client,
                                                   rpc_types=[RpcTypeEmptyCall],
                                                   metadata=_TEST_METADATA,
                                                   num_rpcs=_NUM_RPCS)
        # Only one backend should receive traffic, even though there are 3
        # backends.
        self.assertEqual(1, rpc_distribution.num_peers)
        self.assertLen(
            test_client.find_subchannels_with_state(
                _ChannelzChannelState.READY),
            1,
        )
        self.assertLen(
            test_client.find_subchannels_with_state(_ChannelzChannelState.IDLE),
            2,
        )


# TODO: add more test cases
# 1. based on the basic test, turn down the backend in use, then verify that all
#    RPCs are sent to another backend
# 2. based on the basic test, send more RPCs with other metadata, then verify
#    that they can pick another backend, and there are total of two READY
#    sub-channels

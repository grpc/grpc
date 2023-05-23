# Copyright 2021 gRPC authors.
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
from typing import List

from absl import flags
from absl.testing import absltest
from google.protobuf import json_format

from framework import xds_k8s_flags
from framework import xds_k8s_testcase
from framework import xds_url_map_testcase
from framework.helpers import skips
from framework.rpc import grpc_channelz

logger = logging.getLogger(__name__)
flags.adopt_module_key_flags(xds_k8s_testcase)

# Type aliases
_XdsTestServer = xds_k8s_testcase.XdsTestServer
_XdsTestClient = xds_k8s_testcase.XdsTestClient
_ChannelzChannelState = grpc_channelz.ChannelState
_Lang = skips.Lang

# Testing consts
_TEST_AFFINITY_METADATA_KEY = 'xds_md'
_TD_PROPAGATE_CHECK_INTERVAL_SEC = 10
_TD_PROPAGATE_TIMEOUT = 600
_REPLICA_COUNT = 3
_RPC_COUNT = 100


class AffinityTest(xds_k8s_testcase.RegularXdsKubernetesTestCase):

    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        # Force the python client to use the reference server image (Java)
        # because the python server doesn't yet support set_not_serving RPC.
        # TODO(https://github.com/grpc/grpc/issues/30635): Remove when resolved.
        if cls.lang_spec.client_lang == _Lang.PYTHON:
            cls.server_image = xds_k8s_flags.SERVER_IMAGE_CANONICAL.value

    @staticmethod
    def is_supported(config: skips.TestConfig) -> bool:
        if config.client_lang in _Lang.CPP | _Lang.JAVA:
            return config.version_gte('v1.40.x')
        elif config.client_lang == _Lang.GO:
            return config.version_gte('v1.41.x')
        elif config.client_lang == _Lang.PYTHON:
            # TODO(https://github.com/grpc/grpc/issues/27430): supported after
            #      the issue is fixed.
            return False
        elif config.client_lang == _Lang.NODE:
            return False
        return True

    def test_affinity(self) -> None:  # pylint: disable=too-many-statements

        with self.subTest('00_create_health_check'):
            pass

        with self.subTest('01_create_backend_services'):
            pass

        with self.subTest('02_create_url_map'):
            pass

        with self.subTest('03_create_target_proxy'):
            pass

        with self.subTest('04_create_forwarding_rule'):
            pass

        with self.subTest('05_start_test_servers'):
            pass

        with self.subTest('06_add_server_backends_to_backend_services'):
            pass

        with self.subTest('07_start_test_client'):
            pass

        with self.subTest('08_test_client_xds_config_exists'):
            pass

        with self.subTest('09_test_server_received_rpcs_from_test_client'):
            pass

        with self.subTest('10_first_100_affinity_rpcs_pick_same_backend'):
            pass

        with self.subTest('11_turn_down_server_in_use'):
            pass

        with self.subTest('12_wait_for_unhealth_status_propagation'):
            pass

        with self.subTest('12_next_100_affinity_rpcs_pick_different_backend'):
            pass

if __name__ == '__main__':
    absltest.main(failfast=True)

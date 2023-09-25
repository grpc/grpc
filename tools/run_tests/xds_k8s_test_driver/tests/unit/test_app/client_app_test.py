# Copyright 2023 gRPC authors.
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
from typing import Optional

from absl.testing import absltest

from framework.test_app import client_app

# Alias
XdsTestClient = client_app.XdsTestClient

# Test values.
CANNED_IP: str = "10.0.0.42"
CANNED_RPC_PORT: int = 1111
CANNED_HOSTNAME: str = "test-client.local"
CANNED_SERVER_TARGET: str = "xds:///test-server"


class ClientAppTest(absltest.TestCase):
    """Unit test for the ClientApp."""

    def test_constructor(self):
        xds_client = XdsTestClient(
            ip=CANNED_IP,
            rpc_port=CANNED_RPC_PORT,
            hostname=CANNED_HOSTNAME,
            server_target=CANNED_SERVER_TARGET,
        )
        # Channels list empty.
        self.assertEmpty(xds_client.channels)

        # Test fields set as is.
        self.assertEqual(xds_client.ip, CANNED_IP)
        self.assertEqual(xds_client.rpc_port, CANNED_RPC_PORT)
        self.assertEqual(xds_client.server_target, CANNED_SERVER_TARGET)
        self.assertEqual(xds_client.hostname, CANNED_HOSTNAME)

        # Test optional argument defaults.
        self.assertEqual(xds_client.rpc_host, CANNED_IP)
        self.assertEqual(xds_client.maintenance_port, CANNED_RPC_PORT)


if __name__ == "__main__":
    absltest.main()

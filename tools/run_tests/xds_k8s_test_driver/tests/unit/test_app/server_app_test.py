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

from framework.test_app import server_app

# Alias
XdsTestServer = server_app.XdsTestServer

# Test values.
CANNED_IP: str = "10.0.0.43"
CANNED_RPC_PORT: int = 2222
CANNED_HOSTNAME: str = "test-server.local"
CANNED_XDS_HOST: str = "xds-test-server"
CANNED_XDS_PORT: int = 42


class ServerAppTest(absltest.TestCase):
    """Unit test for the XdsTestServer."""

    def test_constructor(self):
        xds_server = XdsTestServer(
            ip=CANNED_IP,
            rpc_port=CANNED_RPC_PORT,
            hostname=CANNED_HOSTNAME,
        )
        # Channels list empty.
        self.assertEmpty(xds_server.channels)

        # Test fields set as is.
        self.assertEqual(xds_server.ip, CANNED_IP)
        self.assertEqual(xds_server.rpc_port, CANNED_RPC_PORT)
        self.assertEqual(xds_server.hostname, CANNED_HOSTNAME)

        # Test optional argument defaults.
        self.assertEqual(xds_server.rpc_host, CANNED_IP)
        self.assertEqual(xds_server.maintenance_port, CANNED_RPC_PORT)
        self.assertEqual(xds_server.secure_mode, False)

    def test_xds_address(self):
        """Verifies the behavior of set_xds_address(), xds_address, xds_uri."""
        xds_server = XdsTestServer(
            ip=CANNED_IP,
            rpc_port=CANNED_RPC_PORT,
            hostname=CANNED_HOSTNAME,
        )
        self.assertEqual(xds_server.xds_uri, "", msg="Must be empty when unset")

        xds_server.set_xds_address(CANNED_XDS_HOST, CANNED_XDS_PORT)
        self.assertEqual(xds_server.xds_uri, "xds:///xds-test-server:42")

        xds_server.set_xds_address(CANNED_XDS_HOST, None)
        self.assertEqual(
            xds_server.xds_uri,
            "xds:///xds-test-server",
            msg="Must not contain ':port' when the port is not set",
        )

        xds_server.set_xds_address(None, None)
        self.assertEqual(xds_server.xds_uri, "", msg="Must be empty when reset")

        xds_server.set_xds_address(None, CANNED_XDS_PORT)
        self.assertEqual(
            xds_server.xds_uri,
            "",
            msg="Must be empty when only port is set",
        )


if __name__ == "__main__":
    absltest.main()

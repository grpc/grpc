# Copyright 2020 gRPC authors.
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

from absl import flags
from absl.testing import absltest

from framework import xds_k8s_testcase

logger = logging.getLogger(__name__)
flags.adopt_module_key_flags(xds_k8s_testcase)
SKIP_REASON = 'Work in progress'

# Type aliases
_XdsTestServer = xds_k8s_testcase.XdsTestServer
_XdsTestClient = xds_k8s_testcase.XdsTestClient
_SecurityMode = xds_k8s_testcase.SecurityXdsKubernetesTestCase.SecurityMode


class SecurityTest(xds_k8s_testcase.SecurityXdsKubernetesTestCase):

    def test_mtls(self):
        """mTLS test.

        Both client and server configured to use TLS and mTLS.
        """
        self.setupTrafficDirectorGrpc()
        self.setupSecurityPolicies(server_tls=True,
                                   server_mtls=True,
                                   client_tls=True,
                                   client_mtls=True)

        test_server: _XdsTestServer = self.startSecureTestServer()
        self.setupServerBackends()
        test_client: _XdsTestClient = self.startSecureTestClient(test_server)

        self.assertTestAppSecurity(_SecurityMode.MTLS, test_client, test_server)
        self.assertSuccessfulRpcs(test_client)

    def test_tls(self):
        """TLS test.

        Both client and server configured to use TLS and not use mTLS.
        """
        self.setupTrafficDirectorGrpc()
        self.setupSecurityPolicies(server_tls=True,
                                   server_mtls=False,
                                   client_tls=True,
                                   client_mtls=False)

        test_server: _XdsTestServer = self.startSecureTestServer()
        self.setupServerBackends()
        test_client: _XdsTestClient = self.startSecureTestClient(test_server)

        self.assertTestAppSecurity(_SecurityMode.TLS, test_client, test_server)
        self.assertSuccessfulRpcs(test_client)

    def test_plaintext_fallback(self):
        """Plain-text fallback test.

        Control plane provides no security config so both client and server
        fallback to plaintext based on fallback-credentials.
        """
        self.setupTrafficDirectorGrpc()
        self.setupSecurityPolicies(server_tls=False,
                                   server_mtls=False,
                                   client_tls=False,
                                   client_mtls=False)

        test_server: _XdsTestServer = self.startSecureTestServer()
        self.setupServerBackends()
        test_client: _XdsTestClient = self.startSecureTestClient(test_server)

        self.assertTestAppSecurity(_SecurityMode.PLAINTEXT, test_client,
                                   test_server)
        self.assertSuccessfulRpcs(test_client)

    def test_mtls_error(self):
        """Negative test: mTLS Error.

        Server expects client mTLS cert, but client configured only for TLS.

        Note: because this is a negative test we need to make sure the mTLS
        failure happens after receiving the correct configuration at the
        client. To ensure that we will perform the following steps in that
        sequence:

        - Creation of a backendService, and attaching the backend (NEG)
        - Creation of the Server mTLS Policy, and attaching to the ECS
        - Creation of the Client TLS Policy, and attaching to the backendService
        - Creation of the urlMap, targetProxy, and forwardingRule

        With this sequence we are sure that when the client receives the
        endpoints of the backendService the security-config would also have
        been received as confirmed by the TD team.
        """
        # Create backend service
        self.td.setup_backend_for_grpc()

        # Start server and attach its NEGs to the backend service
        test_server: _XdsTestServer = self.startSecureTestServer()
        self.setupServerBackends(wait_for_healthy_status=False)

        # Setup policies and attach them.
        self.setupSecurityPolicies(server_tls=True,
                                   server_mtls=True,
                                   client_tls=True,
                                   client_mtls=False)

        # Create the routing rule map
        self.td.setup_routing_rule_map_for_grpc(self.server_xds_host,
                                                self.server_xds_port)
        # Wait for backends healthy after url map is created
        self.td.wait_for_backends_healthy_status()

        # Start the client.
        test_client: _XdsTestClient = self.startSecureTestClient(
            test_server, wait_for_active_server_channel=False)

        # With negative tests we can't be absolutely certain expected
        # failure state is not caused by something else.
        # To mitigate for this, we repeat the checks a few times in case
        # the channel eventually stabilizes and RPCs pass.
        # TODO(sergiitk): use tenacity retryer, move nums to constants
        wait_sec = 10
        checks = 3
        for check in range(1, checks + 1):
            self.assertMtlsErrorSetup(test_client)
            self.assertFailedRpcs(test_client)
            if check != checks:
                logger.info(
                    'Check %s successful, waiting %s sec before the next check',
                    check, wait_sec)
                time.sleep(wait_sec)

    @absltest.skip(SKIP_REASON)
    def test_server_authz_error(self):
        """Negative test: AuthZ error.

        Client does not authorize server because of mismatched SAN name.
        """


if __name__ == '__main__':
    absltest.main()

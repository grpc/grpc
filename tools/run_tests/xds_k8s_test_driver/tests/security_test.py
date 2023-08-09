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

from absl import flags
from absl.testing import absltest

from framework import xds_k8s_testcase
from framework.helpers import rand
from framework.helpers import skips

logger = logging.getLogger(__name__)
flags.adopt_module_key_flags(xds_k8s_testcase)

# Type aliases
_XdsTestServer = xds_k8s_testcase.XdsTestServer
_XdsTestClient = xds_k8s_testcase.XdsTestClient
_SecurityMode = xds_k8s_testcase.SecurityXdsKubernetesTestCase.SecurityMode
_Lang = skips.Lang


class SecurityTest(xds_k8s_testcase.SecurityXdsKubernetesTestCase):
    @staticmethod
    def is_supported(config: skips.TestConfig) -> bool:
        if config.client_lang in (
            _Lang.CPP | _Lang.GO | _Lang.JAVA | _Lang.PYTHON
        ):
            # Versions prior to v1.41.x don't support PSM Security.
            # https://github.com/grpc/grpc/blob/master/doc/grpc_xds_features.md
            return config.version_gte("v1.41.x")
        elif config.client_lang == _Lang.NODE:
            return False
        return True

    def test_mtls(self):
        """mTLS test.

        Both client and server configured to use TLS and mTLS.
        """
        self.setupTrafficDirectorGrpc()
        self.setupSecurityPolicies(
            server_tls=True, server_mtls=True, client_tls=True, client_mtls=True
        )

        test_server: _XdsTestServer = self.startSecureTestServer()
        self.setupServerBackends()
        test_client: _XdsTestClient = self.startSecureTestClient(test_server)

        self.assertTestAppSecurity(_SecurityMode.MTLS, test_client, test_server)
        self.assertSuccessfulRpcs(test_client)
        logger.info("[SUCCESS] mTLS security mode confirmed.")

    def test_tls(self):
        """TLS test.

        Both client and server configured to use TLS and not use mTLS.
        """
        self.setupTrafficDirectorGrpc()
        self.setupSecurityPolicies(
            server_tls=True,
            server_mtls=False,
            client_tls=True,
            client_mtls=False,
        )

        test_server: _XdsTestServer = self.startSecureTestServer()
        self.setupServerBackends()
        test_client: _XdsTestClient = self.startSecureTestClient(test_server)

        self.assertTestAppSecurity(_SecurityMode.TLS, test_client, test_server)
        self.assertSuccessfulRpcs(test_client)
        logger.info("[SUCCESS] TLS security mode confirmed.")

    def test_plaintext_fallback(self):
        """Plain-text fallback test.

        Control plane provides no security config so both client and server
        fallback to plaintext based on fallback-credentials.
        """
        self.setupTrafficDirectorGrpc()
        self.setupSecurityPolicies(
            server_tls=False,
            server_mtls=False,
            client_tls=False,
            client_mtls=False,
        )

        test_server: _XdsTestServer = self.startSecureTestServer()
        self.setupServerBackends()
        test_client: _XdsTestClient = self.startSecureTestClient(test_server)

        self.assertTestAppSecurity(
            _SecurityMode.PLAINTEXT, test_client, test_server
        )
        self.assertSuccessfulRpcs(test_client)
        logger.info("[SUCCESS] Plaintext security mode confirmed.")

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
        self.td.setup_backend_for_grpc(
            health_check_port=self.server_maintenance_port
        )

        # Start server and attach its NEGs to the backend service, but
        # until they become healthy.
        test_server: _XdsTestServer = self.startSecureTestServer()
        self.setupServerBackends(wait_for_healthy_status=False)

        # Setup policies and attach them.
        self.setupSecurityPolicies(
            server_tls=True,
            server_mtls=True,
            client_tls=True,
            client_mtls=False,
        )

        # Create the routing rule map.
        self.td.setup_routing_rule_map_for_grpc(
            self.server_xds_host, self.server_xds_port
        )
        # Now that TD setup is complete, Backend Service can be populated
        # with healthy backends (NEGs).
        self.td.wait_for_backends_healthy_status()

        # Start the client, but don't wait for it to report a healthy channel.
        test_client: _XdsTestClient = self.startSecureTestClient(
            test_server, wait_for_active_server_channel=False
        )

        self.assertClientCannotReachServerRepeatedly(test_client)
        logger.info(
            "[SUCCESS] Client's connectivity state is consistent with a mTLS "
            "error caused by not presenting mTLS certificate to the server."
        )
        self.assertEqual(2, 3)

    def test_server_authz_error(self):
        """Negative test: AuthZ error.

        Client does not authorize server because of mismatched SAN name.
        The order of operations is the same as in `test_mtls_error`.
        """
        # Create backend service
        self.td.setup_backend_for_grpc(
            health_check_port=self.server_maintenance_port
        )

        # Start server and attach its NEGs to the backend service, but
        # until they become healthy.
        test_server: _XdsTestServer = self.startSecureTestServer()
        self.setupServerBackends(wait_for_healthy_status=False)

        # Regular TLS setup, but with client policy configured using
        # intentionality incorrect server_namespace.
        self.td.setup_server_security(
            server_namespace=self.server_namespace,
            server_name=self.server_name,
            server_port=self.server_port,
            tls=True,
            mtls=False,
        )
        incorrect_namespace = f"incorrect-namespace-{rand.rand_string()}"
        self.td.setup_client_security(
            server_namespace=incorrect_namespace,
            server_name=self.server_name,
            tls=True,
            mtls=False,
        )

        # Create the routing rule map.
        self.td.setup_routing_rule_map_for_grpc(
            self.server_xds_host, self.server_xds_port
        )
        # Now that TD setup is complete, Backend Service can be populated
        # with healthy backends (NEGs).
        self.td.wait_for_backends_healthy_status()

        # Start the client, but don't wait for it to report a healthy channel.
        test_client: _XdsTestClient = self.startSecureTestClient(
            test_server, wait_for_active_server_channel=False
        )

        self.assertClientCannotReachServerRepeatedly(test_client)
        logger.info(
            "[SUCCESS] Client's connectivity state is consistent with "
            "AuthZ error caused by server presenting incorrect SAN."
        )


if __name__ == "__main__":
    absltest.main()

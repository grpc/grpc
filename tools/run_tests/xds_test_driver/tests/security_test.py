#  Copyright 2020 gRPC authors.
#
#  Licensed under the Apache License, Version 2.0 (the "License");
#  you may not use this file except in compliance with the License.
#  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.
import logging

from absl import flags
from absl.testing import absltest

from framework import xds_k8s_testcase

logger = logging.getLogger(__name__)
flags.adopt_module_key_flags(xds_k8s_testcase)
SKIP_REASON = 'Work in progress'

# Type aliases
XdsTestServer = xds_k8s_testcase.XdsTestServer
XdsTestClient = xds_k8s_testcase.XdsTestClient
SecurityMode = xds_k8s_testcase.SecurityXdsKubernetesTestCase.SecurityMode


class SecurityTest(xds_k8s_testcase.SecurityXdsKubernetesTestCase):
    def test_mtls(self):
        self.setupTrafficDirectorGrpc()
        self.setupSecurityPolicies(server_tls=True, server_mtls=True,
                                   client_tls=True, client_mtls=True)

        test_server: XdsTestServer = self.startSecureTestServer()
        self.setupServerBackends()
        test_client: XdsTestClient = self.startSecureTestClient(test_server)

        self.assertTestAppSecurity(SecurityMode.MTLS, test_client, test_server)
        self.assertSuccessfulRpcs(test_client)

    def test_tls(self):
        self.setupTrafficDirectorGrpc()
        self.setupSecurityPolicies(server_tls=True, server_mtls=False,
                                   client_tls=True, client_mtls=False)

        test_server: XdsTestServer = self.startSecureTestServer()
        self.setupServerBackends()
        test_client: XdsTestClient = self.startSecureTestClient(test_server)

        self.assertTestAppSecurity(SecurityMode.TLS, test_client, test_server)
        self.assertSuccessfulRpcs(test_client)

    def test_plaintext_fallback(self):
        self.setupTrafficDirectorGrpc()
        self.setupSecurityPolicies(server_tls=False, server_mtls=False,
                                   client_tls=False, client_mtls=False)

        test_server: XdsTestServer = self.startSecureTestServer()
        self.setupServerBackends()
        test_client: XdsTestClient = self.startSecureTestClient(test_server)

        self.assertTestAppSecurity(
            SecurityMode.PLAINTEXT, test_client, test_server)
        self.assertSuccessfulRpcs(test_client)

    @absltest.skip(SKIP_REASON)
    def test_mtls_error(self):
        pass

    @absltest.skip(SKIP_REASON)
    def test_server_authz_error(self):
        pass


if __name__ == '__main__':
    absltest.main()

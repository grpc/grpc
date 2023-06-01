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

import datetime
import time
from typing import Optional

from absl import flags
from absl.testing import absltest
import grpc

from framework import xds_k8s_testcase
from framework.helpers import skips

flags.adopt_module_key_flags(xds_k8s_testcase)

# Type aliases
_XdsTestServer = xds_k8s_testcase.XdsTestServer
_XdsTestClient = xds_k8s_testcase.XdsTestClient
_SecurityMode = xds_k8s_testcase.SecurityXdsKubernetesTestCase.SecurityMode
_Lang = skips.Lang

# The client generates QPS even when it is still loading information from xDS.
# Once it finally connects there will be an outpouring of the bufferred RPCs and
# the server needs time to chew through the backlog, especially since it is
# still a new process and so probably interpreted. The server on one run
# processed 225 RPCs a second, so with the client configured for 25 qps this is
# 40 seconds worth of buffering before starting to drain the backlog.
_SETTLE_DURATION = datetime.timedelta(seconds=5)
_SAMPLE_DURATION = datetime.timedelta(seconds=0.5)


class AuthzTest(xds_k8s_testcase.SecurityXdsKubernetesTestCase):
    RPC_TYPE_CYCLE = {
        'UNARY_CALL': 'EMPTY_CALL',
        'EMPTY_CALL': 'UNARY_CALL',
    }

    @staticmethod
    def is_supported(config: skips.TestConfig) -> bool:
        # Per "Authorization (RBAC)" in
        # https://github.com/grpc/grpc/blob/master/doc/grpc_xds_features.md
        if config.client_lang in _Lang.CPP | _Lang.PYTHON:
            return config.version_gte('v1.47.x')
        elif config.client_lang in _Lang.GO | _Lang.JAVA:
            return config.version_gte('v1.42.x')
        elif config.client_lang == _Lang.NODE:
            return False
        return True

    def setUp(self):
        super().setUp()
        self.next_rpc_type: Optional[int] = None

    def authz_rules(self):
        return [
            {
                "destinations": {
                    "hosts": [f"*:{self.server_xds_port}"],
                    "ports": [self.server_port],
                    "httpHeaderMatch": {
                        "headerName": "test",
                        "regexMatch": "host-wildcard",
                    },
                },
            },
            {
                "destinations": {
                    "hosts": [f"*:{self.server_xds_port}"],
                    "ports": [self.server_port],
                    "httpHeaderMatch": {
                        "headerName": "test",
                        "regexMatch": "header-regex-a+",
                    },
                },
            },
            {
                "destinations": [{
                    "hosts": [f"{self.server_xds_host}:{self.server_xds_port}"],
                    "ports": [self.server_port],
                    "httpHeaderMatch": {
                        "headerName": "test",
                        "regexMatch": "host-match1",
                    },
                }, {
                    "hosts": [
                        f"a-not-it.com:{self.server_xds_port}",
                        f"{self.server_xds_host}:{self.server_xds_port}",
                        "z-not-it.com:1",
                    ],
                    "ports": [1, self.server_port, 65535],
                    "httpHeaderMatch": {
                        "headerName": "test",
                        "regexMatch": "host-match2",
                    },
                }],
            },
            {
                "destinations": {
                    "hosts": [
                        f"not-the-host:{self.server_xds_port}",
                        "not-the-host",
                    ],
                    "ports": [self.server_port],
                    "httpHeaderMatch": {
                        "headerName": "test",
                        "regexMatch": "never-match-host",
                    },
                },
            },
            {
                "destinations": {
                    "hosts": [f"*:{self.server_xds_port}"],
                    "ports": [1],
                    "httpHeaderMatch": {
                        "headerName": "test",
                        "regexMatch": "never-match-port",
                    },
                },
            },
            # b/202058316. The wildcard principal is generating invalid config
            # {
            #     "sources": {
            #         "principals": ["*"],
            #     },
            #     "destinations": {
            #         "hosts": [f"*:{self.server_xds_port}"],
            #         "ports": [self.server_port],
            #         "httpHeaderMatch": {
            #             "headerName": "test",
            #             "regexMatch": "principal-present",
            #         },
            #     },
            # },
            {
                "sources": [{
                    "principals": [
                        f"spiffe://{self.project}.svc.id.goog/not/the/client",
                    ],
                }, {
                    "principals": [
                        f"spiffe://{self.project}.svc.id.goog/not/the/client",
                        f"spiffe://{self.project}.svc.id.goog/ns/"
                        f"{self.client_namespace}/sa/{self.client_name}",
                    ],
                }],
                "destinations": {
                    "hosts": [f"*:{self.server_xds_port}"],
                    "ports": [self.server_port],
                    "httpHeaderMatch": {
                        "headerName": "test",
                        "regexMatch": "match-principal",
                    },
                },
            },
            {
                "sources": {
                    "principals": [
                        f"spiffe://{self.project}.svc.id.goog/not/the/client",
                    ],
                },
                "destinations": {
                    "hosts": [f"*:{self.server_xds_port}"],
                    "ports": [self.server_port],
                    "httpHeaderMatch": {
                        "headerName": "test",
                        "regexMatch": "never-match-principal",
                    },
                },
            },
        ]

    def configure_and_assert(self, test_client: _XdsTestClient,
                             test_metadata_val: Optional[str],
                             status_code: grpc.StatusCode) -> None:
        # Swap method type every sub-test to avoid mixing results
        rpc_type = self.next_rpc_type
        if rpc_type is None:
            stats = test_client.get_load_balancer_accumulated_stats()
            for t in self.RPC_TYPE_CYCLE:
                if not stats.stats_per_method[t].rpcs_started:
                    rpc_type = t
            self.assertIsNotNone(rpc_type, "All RPC types already used")
        self.next_rpc_type = self.RPC_TYPE_CYCLE[rpc_type]

        metadata = None
        if test_metadata_val is not None:
            metadata = ((rpc_type, "test", test_metadata_val),)
        test_client.update_config.configure(rpc_types=[rpc_type],
                                            metadata=metadata)
        # b/228743575 Python has as race. Give us time to fix it.
        stray_rpc_limit = 1 if self.lang_spec.client_lang == _Lang.PYTHON else 0
        self.assertRpcStatusCodes(test_client,
                                  expected_status=status_code,
                                  duration=_SAMPLE_DURATION,
                                  method=rpc_type,
                                  stray_rpc_limit=stray_rpc_limit)

    def test_plaintext_allow(self) -> None:
        self.setupTrafficDirectorGrpc()
        self.td.create_authz_policy(action='ALLOW', rules=self.authz_rules())
        self.setupSecurityPolicies(server_tls=False,
                                   server_mtls=False,
                                   client_tls=False,
                                   client_mtls=False)

        test_server: _XdsTestServer = self.startSecureTestServer()
        self.setupServerBackends()
        test_client: _XdsTestClient = self.startSecureTestClient(test_server)
        time.sleep(_SETTLE_DURATION.total_seconds())

        with self.subTest('01_host_wildcard'):
            self.configure_and_assert(test_client, 'host-wildcard',
                                      grpc.StatusCode.OK)

        with self.subTest('02_no_match'):
            self.configure_and_assert(test_client, 'no-such-rule',
                                      grpc.StatusCode.PERMISSION_DENIED)
            self.configure_and_assert(test_client, None,
                                      grpc.StatusCode.PERMISSION_DENIED)

        with self.subTest('03_header_regex'):
            self.configure_and_assert(test_client, 'header-regex-a',
                                      grpc.StatusCode.OK)
            self.configure_and_assert(test_client, 'header-regex-aa',
                                      grpc.StatusCode.OK)
            self.configure_and_assert(test_client, 'header-regex-',
                                      grpc.StatusCode.PERMISSION_DENIED)
            self.configure_and_assert(test_client, 'header-regex-ab',
                                      grpc.StatusCode.PERMISSION_DENIED)
            self.configure_and_assert(test_client, 'aheader-regex-a',
                                      grpc.StatusCode.PERMISSION_DENIED)

        with self.subTest('04_host_match'):
            self.configure_and_assert(test_client, 'host-match1',
                                      grpc.StatusCode.OK)
            self.configure_and_assert(test_client, 'host-match2',
                                      grpc.StatusCode.OK)

        with self.subTest('05_never_match_host'):
            self.configure_and_assert(test_client, 'never-match-host',
                                      grpc.StatusCode.PERMISSION_DENIED)

        with self.subTest('06_never_match_port'):
            self.configure_and_assert(test_client, 'never-match-port',
                                      grpc.StatusCode.PERMISSION_DENIED)

        # b/202058316
        # with self.subTest('07_principal_present'):
        #     self.configure_and_assert(test_client, 'principal-present',
        #                               grpc.StatusCode.PERMISSION_DENIED)

    def test_tls_allow(self) -> None:
        self.setupTrafficDirectorGrpc()
        self.td.create_authz_policy(action='ALLOW', rules=self.authz_rules())
        self.setupSecurityPolicies(server_tls=True,
                                   server_mtls=False,
                                   client_tls=True,
                                   client_mtls=False)

        test_server: _XdsTestServer = self.startSecureTestServer()
        self.setupServerBackends()
        test_client: _XdsTestClient = self.startSecureTestClient(test_server)
        time.sleep(_SETTLE_DURATION.total_seconds())

        with self.subTest('01_host_wildcard'):
            self.configure_and_assert(test_client, 'host-wildcard',
                                      grpc.StatusCode.OK)

        with self.subTest('02_no_match'):
            self.configure_and_assert(test_client, None,
                                      grpc.StatusCode.PERMISSION_DENIED)

        # b/202058316
        # with self.subTest('03_principal_present'):
        #     self.configure_and_assert(test_client, 'principal-present',
        #                               grpc.StatusCode.PERMISSION_DENIED)

    def test_mtls_allow(self) -> None:
        self.setupTrafficDirectorGrpc()
        self.td.create_authz_policy(action='ALLOW', rules=self.authz_rules())
        self.setupSecurityPolicies(server_tls=True,
                                   server_mtls=True,
                                   client_tls=True,
                                   client_mtls=True)

        test_server: _XdsTestServer = self.startSecureTestServer()
        self.setupServerBackends()
        test_client: _XdsTestClient = self.startSecureTestClient(test_server)
        time.sleep(_SETTLE_DURATION.total_seconds())

        with self.subTest('01_host_wildcard'):
            self.configure_and_assert(test_client, 'host-wildcard',
                                      grpc.StatusCode.OK)

        with self.subTest('02_no_match'):
            self.configure_and_assert(test_client, None,
                                      grpc.StatusCode.PERMISSION_DENIED)

        # b/202058316
        # with self.subTest('03_principal_present'):
        #     self.configure_and_assert(test_client, 'principal-present',
        #                               grpc.StatusCode.OK)

        with self.subTest('04_match_principal'):
            self.configure_and_assert(test_client, 'match-principal',
                                      grpc.StatusCode.OK)

        with self.subTest('05_never_match_principal'):
            self.configure_and_assert(test_client, 'never-match-principal',
                                      grpc.StatusCode.PERMISSION_DENIED)

    def test_plaintext_deny(self) -> None:
        self.setupTrafficDirectorGrpc()
        self.td.create_authz_policy(action='DENY', rules=self.authz_rules())
        self.setupSecurityPolicies(server_tls=False,
                                   server_mtls=False,
                                   client_tls=False,
                                   client_mtls=False)

        test_server: _XdsTestServer = self.startSecureTestServer()
        self.setupServerBackends()
        test_client: _XdsTestClient = self.startSecureTestClient(test_server)
        time.sleep(_SETTLE_DURATION.total_seconds())

        with self.subTest('01_host_wildcard'):
            self.configure_and_assert(test_client, 'host-wildcard',
                                      grpc.StatusCode.PERMISSION_DENIED)

        with self.subTest('02_no_match'):
            self.configure_and_assert(test_client, None, grpc.StatusCode.OK)


if __name__ == '__main__':
    absltest.main()

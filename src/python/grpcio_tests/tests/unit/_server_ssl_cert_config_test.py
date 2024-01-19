# Copyright 2017 gRPC authors.
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
"""Tests server certificate rotation.

Here we test various aspects of gRPC Python, and in some cases gRPC
Core by extension, support for server certificate rotation.

* ServerSSLCertReloadTestWithClientAuth: test ability to rotate
  server's SSL cert for use in future channels with clients while not
  affecting any existing channel. The server requires client
  authentication.

* ServerSSLCertReloadTestWithoutClientAuth: like
  ServerSSLCertReloadTestWithClientAuth except that the server does
  not authenticate the client.

* ServerSSLCertReloadTestCertConfigReuse: tests gRPC Python's ability
  to deal with user's reuse of ServerCertificateConfiguration instances.
"""

import abc
import collections
from concurrent import futures
import logging
import os
import threading
import unittest

import grpc

from tests.testing import _application_common
from tests.testing import _server_application
from tests.testing.proto import services_pb2_grpc
from tests.unit import resources
from tests.unit import test_common

CA_1_PEM = resources.cert_hier_1_root_ca_cert()
CA_2_PEM = resources.cert_hier_2_root_ca_cert()

CLIENT_KEY_1_PEM = resources.cert_hier_1_client_1_key()
CLIENT_CERT_CHAIN_1_PEM = (
    resources.cert_hier_1_client_1_cert()
    + resources.cert_hier_1_intermediate_ca_cert()
)

CLIENT_KEY_2_PEM = resources.cert_hier_2_client_1_key()
CLIENT_CERT_CHAIN_2_PEM = (
    resources.cert_hier_2_client_1_cert()
    + resources.cert_hier_2_intermediate_ca_cert()
)

SERVER_KEY_1_PEM = resources.cert_hier_1_server_1_key()
SERVER_CERT_CHAIN_1_PEM = (
    resources.cert_hier_1_server_1_cert()
    + resources.cert_hier_1_intermediate_ca_cert()
)

SERVER_KEY_2_PEM = resources.cert_hier_2_server_1_key()
SERVER_CERT_CHAIN_2_PEM = (
    resources.cert_hier_2_server_1_cert()
    + resources.cert_hier_2_intermediate_ca_cert()
)

# for use with the CertConfigFetcher. Roughly a simple custom mock
# implementation
Call = collections.namedtuple("Call", ["did_raise", "returned_cert_config"])


def _create_channel(port, credentials):
    return grpc.secure_channel("localhost:{}".format(port), credentials)


def _create_client_stub(channel, expect_success):
    if expect_success:
        # per Nathaniel: there's some robustness issue if we start
        # using a channel without waiting for it to be actually ready
        grpc.channel_ready_future(channel).result(timeout=10)
    return services_pb2_grpc.FirstServiceStub(channel)


class CertConfigFetcher(object):
    def __init__(self):
        self._lock = threading.Lock()
        self._calls = []
        self._should_raise = False
        self._cert_config = None

    def reset(self):
        with self._lock:
            self._calls = []
            self._should_raise = False
            self._cert_config = None

    def configure(self, should_raise, cert_config):
        assert not (should_raise and cert_config), (
            "should not specify both should_raise and a cert_config at the same"
            " time"
        )
        with self._lock:
            self._should_raise = should_raise
            self._cert_config = cert_config

    def getCalls(self):
        with self._lock:
            return self._calls

    def __call__(self):
        with self._lock:
            if self._should_raise:
                self._calls.append(Call(True, None))
                raise ValueError("just for fun, should not affect the test")
            else:
                self._calls.append(Call(False, self._cert_config))
                return self._cert_config


class _ServerSSLCertReloadTest(unittest.TestCase, metaclass=abc.ABCMeta):
    def __init__(self, *args, **kwargs):
        super(_ServerSSLCertReloadTest, self).__init__(*args, **kwargs)
        self.server = None
        self.port = None

    @abc.abstractmethod
    def require_client_auth(self):
        raise NotImplementedError()

    def setUp(self):
        self.server = test_common.test_server()
        services_pb2_grpc.add_FirstServiceServicer_to_server(
            _server_application.FirstServiceServicer(), self.server
        )
        switch_cert_on_client_num = 10
        initial_cert_config = grpc.ssl_server_certificate_configuration(
            [(SERVER_KEY_1_PEM, SERVER_CERT_CHAIN_1_PEM)],
            root_certificates=CA_2_PEM,
        )
        self.cert_config_fetcher = CertConfigFetcher()
        server_credentials = grpc.dynamic_ssl_server_credentials(
            initial_cert_config,
            self.cert_config_fetcher,
            require_client_authentication=self.require_client_auth(),
        )
        self.port = self.server.add_secure_port("[::]:0", server_credentials)
        self.server.start()

    def tearDown(self):
        if self.server:
            self.server.stop(None)

    def _perform_rpc(self, client_stub, expect_success):
        # we don't care about the actual response of the rpc; only
        # whether we can perform it or not, and if not, the status
        # code must be UNAVAILABLE
        request = _application_common.UNARY_UNARY_REQUEST
        if expect_success:
            response = client_stub.UnUn(request)
            self.assertEqual(response, _application_common.UNARY_UNARY_RESPONSE)
        else:
            with self.assertRaises(grpc.RpcError) as exception_context:
                client_stub.UnUn(request)
            # If TLS 1.2 is used, then the client receives an alert message
            # before the handshake is complete, so the status is UNAVAILABLE. If
            # TLS 1.3 is used, then the client receives the alert message after
            # the handshake is complete, so the TSI handshaker returns the
            # TSI_PROTOCOL_FAILURE result. This result does not have a
            # corresponding status code, so this yields an UNKNOWN status.
            self.assertTrue(
                exception_context.exception.code()
                in [grpc.StatusCode.UNAVAILABLE, grpc.StatusCode.UNKNOWN]
            )

    def _do_one_shot_client_rpc(
        self,
        expect_success,
        root_certificates=None,
        private_key=None,
        certificate_chain=None,
    ):
        credentials = grpc.ssl_channel_credentials(
            root_certificates=root_certificates,
            private_key=private_key,
            certificate_chain=certificate_chain,
        )
        with _create_channel(self.port, credentials) as client_channel:
            client_stub = _create_client_stub(client_channel, expect_success)
            self._perform_rpc(client_stub, expect_success)

    def _test(self):
        # things should work...
        self.cert_config_fetcher.configure(False, None)
        self._do_one_shot_client_rpc(
            True,
            root_certificates=CA_1_PEM,
            private_key=CLIENT_KEY_2_PEM,
            certificate_chain=CLIENT_CERT_CHAIN_2_PEM,
        )
        actual_calls = self.cert_config_fetcher.getCalls()
        self.assertEqual(len(actual_calls), 1)
        self.assertFalse(actual_calls[0].did_raise)
        self.assertIsNone(actual_calls[0].returned_cert_config)

        # client should reject server...
        # fails because client trusts ca2 and so will reject server
        self.cert_config_fetcher.reset()
        self.cert_config_fetcher.configure(False, None)
        self._do_one_shot_client_rpc(
            False,
            root_certificates=CA_2_PEM,
            private_key=CLIENT_KEY_2_PEM,
            certificate_chain=CLIENT_CERT_CHAIN_2_PEM,
        )
        actual_calls = self.cert_config_fetcher.getCalls()
        self.assertGreaterEqual(len(actual_calls), 1)
        self.assertFalse(actual_calls[0].did_raise)
        for i, call in enumerate(actual_calls):
            self.assertFalse(call.did_raise, "i= {}".format(i))
            self.assertIsNone(call.returned_cert_config, "i= {}".format(i))

        # should work again...
        self.cert_config_fetcher.reset()
        self.cert_config_fetcher.configure(True, None)
        self._do_one_shot_client_rpc(
            True,
            root_certificates=CA_1_PEM,
            private_key=CLIENT_KEY_2_PEM,
            certificate_chain=CLIENT_CERT_CHAIN_2_PEM,
        )
        actual_calls = self.cert_config_fetcher.getCalls()
        self.assertEqual(len(actual_calls), 1)
        self.assertTrue(actual_calls[0].did_raise)
        self.assertIsNone(actual_calls[0].returned_cert_config)

        # if with_client_auth, then client should be rejected by
        # server because client uses key/cert1, but server trusts ca2,
        # so server will reject
        self.cert_config_fetcher.reset()
        self.cert_config_fetcher.configure(False, None)
        self._do_one_shot_client_rpc(
            not self.require_client_auth(),
            root_certificates=CA_1_PEM,
            private_key=CLIENT_KEY_1_PEM,
            certificate_chain=CLIENT_CERT_CHAIN_1_PEM,
        )
        actual_calls = self.cert_config_fetcher.getCalls()
        self.assertGreaterEqual(len(actual_calls), 1)
        for i, call in enumerate(actual_calls):
            self.assertFalse(call.did_raise, "i= {}".format(i))
            self.assertIsNone(call.returned_cert_config, "i= {}".format(i))

        # should work again...
        self.cert_config_fetcher.reset()
        self.cert_config_fetcher.configure(False, None)
        self._do_one_shot_client_rpc(
            True,
            root_certificates=CA_1_PEM,
            private_key=CLIENT_KEY_2_PEM,
            certificate_chain=CLIENT_CERT_CHAIN_2_PEM,
        )
        actual_calls = self.cert_config_fetcher.getCalls()
        self.assertEqual(len(actual_calls), 1)
        self.assertFalse(actual_calls[0].did_raise)
        self.assertIsNone(actual_calls[0].returned_cert_config)

        # now create the "persistent" clients
        self.cert_config_fetcher.reset()
        self.cert_config_fetcher.configure(False, None)
        channel_A = _create_channel(
            self.port,
            grpc.ssl_channel_credentials(
                root_certificates=CA_1_PEM,
                private_key=CLIENT_KEY_2_PEM,
                certificate_chain=CLIENT_CERT_CHAIN_2_PEM,
            ),
        )
        persistent_client_stub_A = _create_client_stub(channel_A, True)
        self._perform_rpc(persistent_client_stub_A, True)
        actual_calls = self.cert_config_fetcher.getCalls()
        self.assertEqual(len(actual_calls), 1)
        self.assertFalse(actual_calls[0].did_raise)
        self.assertIsNone(actual_calls[0].returned_cert_config)

        self.cert_config_fetcher.reset()
        self.cert_config_fetcher.configure(False, None)
        channel_B = _create_channel(
            self.port,
            grpc.ssl_channel_credentials(
                root_certificates=CA_1_PEM,
                private_key=CLIENT_KEY_2_PEM,
                certificate_chain=CLIENT_CERT_CHAIN_2_PEM,
            ),
        )
        persistent_client_stub_B = _create_client_stub(channel_B, True)
        self._perform_rpc(persistent_client_stub_B, True)
        actual_calls = self.cert_config_fetcher.getCalls()
        self.assertEqual(len(actual_calls), 1)
        self.assertFalse(actual_calls[0].did_raise)
        self.assertIsNone(actual_calls[0].returned_cert_config)

        # moment of truth!! client should reject server because the
        # server switch cert...
        cert_config = grpc.ssl_server_certificate_configuration(
            [(SERVER_KEY_2_PEM, SERVER_CERT_CHAIN_2_PEM)],
            root_certificates=CA_1_PEM,
        )
        self.cert_config_fetcher.reset()
        self.cert_config_fetcher.configure(False, cert_config)
        self._do_one_shot_client_rpc(
            False,
            root_certificates=CA_1_PEM,
            private_key=CLIENT_KEY_2_PEM,
            certificate_chain=CLIENT_CERT_CHAIN_2_PEM,
        )
        actual_calls = self.cert_config_fetcher.getCalls()
        self.assertGreaterEqual(len(actual_calls), 1)
        self.assertFalse(actual_calls[0].did_raise)
        for i, call in enumerate(actual_calls):
            self.assertFalse(call.did_raise, "i= {}".format(i))
            self.assertEqual(
                call.returned_cert_config, cert_config, "i= {}".format(i)
            )

        # now should work again...
        self.cert_config_fetcher.reset()
        self.cert_config_fetcher.configure(False, None)
        self._do_one_shot_client_rpc(
            True,
            root_certificates=CA_2_PEM,
            private_key=CLIENT_KEY_1_PEM,
            certificate_chain=CLIENT_CERT_CHAIN_1_PEM,
        )
        actual_calls = self.cert_config_fetcher.getCalls()
        self.assertEqual(len(actual_calls), 1)
        self.assertFalse(actual_calls[0].did_raise)
        self.assertIsNone(actual_calls[0].returned_cert_config)

        # client should be rejected by server if with_client_auth
        self.cert_config_fetcher.reset()
        self.cert_config_fetcher.configure(False, None)
        self._do_one_shot_client_rpc(
            not self.require_client_auth(),
            root_certificates=CA_2_PEM,
            private_key=CLIENT_KEY_2_PEM,
            certificate_chain=CLIENT_CERT_CHAIN_2_PEM,
        )
        actual_calls = self.cert_config_fetcher.getCalls()
        self.assertGreaterEqual(len(actual_calls), 1)
        for i, call in enumerate(actual_calls):
            self.assertFalse(call.did_raise, "i= {}".format(i))
            self.assertIsNone(call.returned_cert_config, "i= {}".format(i))

        # here client should reject server...
        self.cert_config_fetcher.reset()
        self.cert_config_fetcher.configure(False, None)
        self._do_one_shot_client_rpc(
            False,
            root_certificates=CA_1_PEM,
            private_key=CLIENT_KEY_2_PEM,
            certificate_chain=CLIENT_CERT_CHAIN_2_PEM,
        )
        actual_calls = self.cert_config_fetcher.getCalls()
        self.assertGreaterEqual(len(actual_calls), 1)
        for i, call in enumerate(actual_calls):
            self.assertFalse(call.did_raise, "i= {}".format(i))
            self.assertIsNone(call.returned_cert_config, "i= {}".format(i))

        # persistent clients should continue to work
        self.cert_config_fetcher.reset()
        self.cert_config_fetcher.configure(False, None)
        self._perform_rpc(persistent_client_stub_A, True)
        actual_calls = self.cert_config_fetcher.getCalls()
        self.assertEqual(len(actual_calls), 0)

        self.cert_config_fetcher.reset()
        self.cert_config_fetcher.configure(False, None)
        self._perform_rpc(persistent_client_stub_B, True)
        actual_calls = self.cert_config_fetcher.getCalls()
        self.assertEqual(len(actual_calls), 0)

        channel_A.close()
        channel_B.close()


class ServerSSLCertConfigFetcherParamsChecks(unittest.TestCase):
    def test_check_on_initial_config(self):
        with self.assertRaises(TypeError):
            grpc.dynamic_ssl_server_credentials(None, str)
        with self.assertRaises(TypeError):
            grpc.dynamic_ssl_server_credentials(1, str)

    def test_check_on_config_fetcher(self):
        cert_config = grpc.ssl_server_certificate_configuration(
            [(SERVER_KEY_2_PEM, SERVER_CERT_CHAIN_2_PEM)],
            root_certificates=CA_1_PEM,
        )
        with self.assertRaises(TypeError):
            grpc.dynamic_ssl_server_credentials(cert_config, None)
        with self.assertRaises(TypeError):
            grpc.dynamic_ssl_server_credentials(cert_config, 1)


class ServerSSLCertReloadTestWithClientAuth(_ServerSSLCertReloadTest):
    def require_client_auth(self):
        return True

    test = _ServerSSLCertReloadTest._test


class ServerSSLCertReloadTestWithoutClientAuth(_ServerSSLCertReloadTest):
    def require_client_auth(self):
        return False

    test = _ServerSSLCertReloadTest._test


class ServerSSLCertReloadTestCertConfigReuse(_ServerSSLCertReloadTest):
    """Ensures that `ServerCertificateConfiguration` instances can be reused.

    Because gRPC Core takes ownership of the
    `grpc_ssl_server_certificate_config` encapsulated by
    `ServerCertificateConfiguration`, this test reuses the same
    `ServerCertificateConfiguration` instances multiple times to make sure
    gRPC Python takes care of maintaining the validity of
    `ServerCertificateConfiguration` instances, so that such instances can be
    re-used by user application.
    """

    def require_client_auth(self):
        return True

    def setUp(self):
        self.server = test_common.test_server()
        services_pb2_grpc.add_FirstServiceServicer_to_server(
            _server_application.FirstServiceServicer(), self.server
        )
        self.cert_config_A = grpc.ssl_server_certificate_configuration(
            [(SERVER_KEY_1_PEM, SERVER_CERT_CHAIN_1_PEM)],
            root_certificates=CA_2_PEM,
        )
        self.cert_config_B = grpc.ssl_server_certificate_configuration(
            [(SERVER_KEY_2_PEM, SERVER_CERT_CHAIN_2_PEM)],
            root_certificates=CA_1_PEM,
        )
        self.cert_config_fetcher = CertConfigFetcher()
        server_credentials = grpc.dynamic_ssl_server_credentials(
            self.cert_config_A,
            self.cert_config_fetcher,
            require_client_authentication=True,
        )
        self.port = self.server.add_secure_port("[::]:0", server_credentials)
        self.server.start()

    def test_cert_config_reuse(self):
        # succeed with A
        self.cert_config_fetcher.reset()
        self.cert_config_fetcher.configure(False, self.cert_config_A)
        self._do_one_shot_client_rpc(
            True,
            root_certificates=CA_1_PEM,
            private_key=CLIENT_KEY_2_PEM,
            certificate_chain=CLIENT_CERT_CHAIN_2_PEM,
        )
        actual_calls = self.cert_config_fetcher.getCalls()
        self.assertEqual(len(actual_calls), 1)
        self.assertFalse(actual_calls[0].did_raise)
        self.assertEqual(
            actual_calls[0].returned_cert_config, self.cert_config_A
        )

        # fail with A
        self.cert_config_fetcher.reset()
        self.cert_config_fetcher.configure(False, self.cert_config_A)
        self._do_one_shot_client_rpc(
            False,
            root_certificates=CA_2_PEM,
            private_key=CLIENT_KEY_1_PEM,
            certificate_chain=CLIENT_CERT_CHAIN_1_PEM,
        )
        actual_calls = self.cert_config_fetcher.getCalls()
        self.assertGreaterEqual(len(actual_calls), 1)
        self.assertFalse(actual_calls[0].did_raise)
        for i, call in enumerate(actual_calls):
            self.assertFalse(call.did_raise, "i= {}".format(i))
            self.assertEqual(
                call.returned_cert_config, self.cert_config_A, "i= {}".format(i)
            )

        # succeed again with A
        self.cert_config_fetcher.reset()
        self.cert_config_fetcher.configure(False, self.cert_config_A)
        self._do_one_shot_client_rpc(
            True,
            root_certificates=CA_1_PEM,
            private_key=CLIENT_KEY_2_PEM,
            certificate_chain=CLIENT_CERT_CHAIN_2_PEM,
        )
        actual_calls = self.cert_config_fetcher.getCalls()
        self.assertEqual(len(actual_calls), 1)
        self.assertFalse(actual_calls[0].did_raise)
        self.assertEqual(
            actual_calls[0].returned_cert_config, self.cert_config_A
        )

        # succeed with B
        self.cert_config_fetcher.reset()
        self.cert_config_fetcher.configure(False, self.cert_config_B)
        self._do_one_shot_client_rpc(
            True,
            root_certificates=CA_2_PEM,
            private_key=CLIENT_KEY_1_PEM,
            certificate_chain=CLIENT_CERT_CHAIN_1_PEM,
        )
        actual_calls = self.cert_config_fetcher.getCalls()
        self.assertEqual(len(actual_calls), 1)
        self.assertFalse(actual_calls[0].did_raise)
        self.assertEqual(
            actual_calls[0].returned_cert_config, self.cert_config_B
        )

        # fail with B
        self.cert_config_fetcher.reset()
        self.cert_config_fetcher.configure(False, self.cert_config_B)
        self._do_one_shot_client_rpc(
            False,
            root_certificates=CA_1_PEM,
            private_key=CLIENT_KEY_2_PEM,
            certificate_chain=CLIENT_CERT_CHAIN_2_PEM,
        )
        actual_calls = self.cert_config_fetcher.getCalls()
        self.assertGreaterEqual(len(actual_calls), 1)
        self.assertFalse(actual_calls[0].did_raise)
        for i, call in enumerate(actual_calls):
            self.assertFalse(call.did_raise, "i= {}".format(i))
            self.assertEqual(
                call.returned_cert_config, self.cert_config_B, "i= {}".format(i)
            )

        # succeed again with B
        self.cert_config_fetcher.reset()
        self.cert_config_fetcher.configure(False, self.cert_config_B)
        self._do_one_shot_client_rpc(
            True,
            root_certificates=CA_2_PEM,
            private_key=CLIENT_KEY_1_PEM,
            certificate_chain=CLIENT_CERT_CHAIN_1_PEM,
        )
        actual_calls = self.cert_config_fetcher.getCalls()
        self.assertEqual(len(actual_calls), 1)
        self.assertFalse(actual_calls[0].did_raise)
        self.assertEqual(
            actual_calls[0].returned_cert_config, self.cert_config_B
        )


if __name__ == "__main__":
    logging.basicConfig()
    unittest.main(verbosity=2)

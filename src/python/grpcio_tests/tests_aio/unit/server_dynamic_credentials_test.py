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

More documentations see [_server_ssl_cert_config_test.py].
"""

import abc
import asyncio
import collections
import os
import six
import threading
import unittest
import logging

import grpc
from grpc.experimental import aio
from tests.unit import resources
from tests_aio.unit._test_base import AioTestBase

_CA_1_PEM = resources.cert_hier_1_root_ca_cert()
_CA_2_PEM = resources.cert_hier_2_root_ca_cert()

_CLIENT_KEY_1_PEM = resources.cert_hier_1_client_1_key()
_CLIENT_CERT_CHAIN_1_PEM = (resources.cert_hier_1_client_1_cert() +
                            resources.cert_hier_1_intermediate_ca_cert())

_CLIENT_KEY_2_PEM = resources.cert_hier_2_client_1_key()
_CLIENT_CERT_CHAIN_2_PEM = (resources.cert_hier_2_client_1_cert() +
                            resources.cert_hier_2_intermediate_ca_cert())

_SERVER_KEY_1_PEM = resources.cert_hier_1_server_1_key()
_SERVER_CERT_CHAIN_1_PEM = (resources.cert_hier_1_server_1_cert() +
                            resources.cert_hier_1_intermediate_ca_cert())

_SERVER_KEY_2_PEM = resources.cert_hier_2_server_1_key()
_SERVER_CERT_CHAIN_2_PEM = (resources.cert_hier_2_server_1_cert() +
                            resources.cert_hier_2_intermediate_ca_cert())

# for use with the _CertConfigFetcher. Roughly a simple custom mock
# implementation
Call = collections.namedtuple('Call', ['did_raise', 'returned_cert_config'])

_SIMPLE_UNARY_UNARY = '/test/SimpleUnaryUnary'
_REQUEST = b'\x04\x05\x06'
_RESPONSE = b'\x07\x08'

_CLIENT_TRY_TO_CONNECT_TIMEOUT_S = 10


async def _simple_unary_unary(request, unused_context):
    assert _REQUEST == request
    return _RESPONSE


class _GenericHandler(grpc.GenericRpcHandler):

    @staticmethod
    def service(handler_call_details):
        if handler_call_details.method == _SIMPLE_UNARY_UNARY:
            return grpc.unary_unary_rpc_method_handler(_simple_unary_unary)
        else:
            return None


def _create_channel(port, channel_credentials):
    return aio.secure_channel('localhost:{}'.format(port), channel_credentials)


class _CertConfigFetcher(object):

    def __init__(self):
        self._calls = []
        self._should_raise = False
        self._cert_config = None

    def reset(self):
        self._calls = []
        self._should_raise = False
        self._cert_config = None

    def configure(self, should_raise, cert_config):
        assert not (should_raise and cert_config), (
            "should not specify both should_raise and a cert_config at the same time"
        )
        self._should_raise = should_raise
        self._cert_config = cert_config

    def getCalls(self):
        return self._calls

    def __call__(self):
        if self._should_raise:
            self._calls.append(Call(True, None))
            raise ValueError('just for fun, should not affect the test')
        else:
            self._calls.append(Call(False, self._cert_config))
            return self._cert_config


class _ServerSSLCertReloadTest(AioTestBase, abc.ABC):

    @abc.abstractmethod
    def require_client_auth(self):
        raise NotImplementedError()

    async def setUp(self):
        self._server = aio.server()
        self._server.add_generic_rpc_handlers((_GenericHandler(),))

        initial_cert_config = grpc.ssl_server_certificate_configuration(
            [(_SERVER_KEY_1_PEM, _SERVER_CERT_CHAIN_1_PEM)],
            root_certificates=_CA_2_PEM)
        self._cert_config_fetcher = _CertConfigFetcher()

        server_credentials = grpc.dynamic_ssl_server_credentials(
            initial_cert_config,
            self._cert_config_fetcher,
            require_client_authentication=self.require_client_auth())
        self._port = self._server.add_secure_port('[::]:0', server_credentials)

        await self._server.start()

    async def tearDown(self):
        await self._server.stop(None)

    async def _perform_rpc(self, channel, expect_success):
        # We don't care about the actual response of the rpc; only
        # whether we can perform it or not, and if not, the status
        # code must be UNAVAILABLE
        multicallable = channel.unary_unary(_SIMPLE_UNARY_UNARY)
        if expect_success:
            response = await multicallable(_REQUEST)
            self.assertEqual(_RESPONSE, response)
        else:
            with self.assertRaises(aio.AioRpcError) as exception_context:
                await multicallable(_REQUEST)
            self.assertEqual(grpc.StatusCode.UNAVAILABLE,
                             exception_context.exception.code())

    async def _do_one_shot_client_rpc(self,
                                      expect_success,
                                      root_certificates=None,
                                      private_key=None,
                                      certificate_chain=None):
        client_credentials = grpc.ssl_channel_credentials(
            root_certificates=root_certificates,
            private_key=private_key,
            certificate_chain=certificate_chain)

        async with _create_channel(self._port,
                                   client_credentials) as client_channel:
            if expect_success:
                await asyncio.wait_for(client_channel.channel_ready(),
                                       _CLIENT_TRY_TO_CONNECT_TIMEOUT_S)
            await self._perform_rpc(client_channel, expect_success)

    async def _test(self):
        # things should work...
        self._cert_config_fetcher.configure(should_raise=False,
                                            cert_config=None)
        await self._do_one_shot_client_rpc(
            True,
            root_certificates=_CA_1_PEM,
            private_key=_CLIENT_KEY_2_PEM,
            certificate_chain=_CLIENT_CERT_CHAIN_2_PEM)
        actual_calls = self._cert_config_fetcher.getCalls()
        self.assertEqual(len(actual_calls), 1)
        self.assertFalse(actual_calls[0].did_raise)
        self.assertIsNone(actual_calls[0].returned_cert_config)

        # client should reject server...
        # fails because client trusts ca2 and so will reject server
        self._cert_config_fetcher.reset()
        self._cert_config_fetcher.configure(should_raise=False,
                                            cert_config=None)
        await self._do_one_shot_client_rpc(
            False,
            root_certificates=_CA_2_PEM,
            private_key=_CLIENT_KEY_2_PEM,
            certificate_chain=_CLIENT_CERT_CHAIN_2_PEM)
        actual_calls = self._cert_config_fetcher.getCalls()
        self.assertGreaterEqual(len(actual_calls), 1)
        self.assertFalse(actual_calls[0].did_raise)
        for i, call in enumerate(actual_calls):
            self.assertFalse(call.did_raise, 'i= {}'.format(i))
            self.assertIsNone(call.returned_cert_config, 'i= {}'.format(i))

        # should work again...
        self._cert_config_fetcher.reset()
        self._cert_config_fetcher.configure(should_raise=True, cert_config=None)
        await self._do_one_shot_client_rpc(
            True,
            root_certificates=_CA_1_PEM,
            private_key=_CLIENT_KEY_2_PEM,
            certificate_chain=_CLIENT_CERT_CHAIN_2_PEM)
        actual_calls = self._cert_config_fetcher.getCalls()
        self.assertEqual(len(actual_calls), 1)
        self.assertTrue(actual_calls[0].did_raise)
        self.assertIsNone(actual_calls[0].returned_cert_config)

        # if with_client_auth, then client should be rejected by
        # server because client uses key/cert1, but server trusts ca2,
        # so server will reject
        self._cert_config_fetcher.reset()
        self._cert_config_fetcher.configure(should_raise=False,
                                            cert_config=None)
        await self._do_one_shot_client_rpc(
            not self.require_client_auth(),
            root_certificates=_CA_1_PEM,
            private_key=_CLIENT_KEY_1_PEM,
            certificate_chain=_CLIENT_CERT_CHAIN_1_PEM)
        actual_calls = self._cert_config_fetcher.getCalls()
        self.assertGreaterEqual(len(actual_calls), 1)
        for i, call in enumerate(actual_calls):
            self.assertFalse(call.did_raise, 'i= {}'.format(i))
            self.assertIsNone(call.returned_cert_config, 'i= {}'.format(i))

        # should work again...
        self._cert_config_fetcher.reset()
        self._cert_config_fetcher.configure(should_raise=False,
                                            cert_config=None)
        await self._do_one_shot_client_rpc(
            True,
            root_certificates=_CA_1_PEM,
            private_key=_CLIENT_KEY_2_PEM,
            certificate_chain=_CLIENT_CERT_CHAIN_2_PEM)
        actual_calls = self._cert_config_fetcher.getCalls()
        self.assertEqual(len(actual_calls), 1)
        self.assertFalse(actual_calls[0].did_raise)
        self.assertIsNone(actual_calls[0].returned_cert_config)

        # now create the "persistent" clients
        self._cert_config_fetcher.reset()
        self._cert_config_fetcher.configure(should_raise=False,
                                            cert_config=None)
        channel_A = _create_channel(
            self._port,
            grpc.ssl_channel_credentials(
                root_certificates=_CA_1_PEM,
                private_key=_CLIENT_KEY_2_PEM,
                certificate_chain=_CLIENT_CERT_CHAIN_2_PEM))
        await self._perform_rpc(channel_A, True)
        actual_calls = self._cert_config_fetcher.getCalls()
        self.assertEqual(len(actual_calls), 1)
        self.assertFalse(actual_calls[0].did_raise)
        self.assertIsNone(actual_calls[0].returned_cert_config)

        self._cert_config_fetcher.reset()
        self._cert_config_fetcher.configure(should_raise=False,
                                            cert_config=None)
        channel_B = _create_channel(
            self._port,
            grpc.ssl_channel_credentials(
                root_certificates=_CA_1_PEM,
                private_key=_CLIENT_KEY_2_PEM,
                certificate_chain=_CLIENT_CERT_CHAIN_2_PEM))
        await self._perform_rpc(channel_B, True)
        actual_calls = self._cert_config_fetcher.getCalls()
        self.assertEqual(len(actual_calls), 1)
        self.assertFalse(actual_calls[0].did_raise)
        self.assertIsNone(actual_calls[0].returned_cert_config)

        # moment of truth!! client should reject server because the
        # server switch cert...
        cert_config = grpc.ssl_server_certificate_configuration(
            [(_SERVER_KEY_2_PEM, _SERVER_CERT_CHAIN_2_PEM)],
            root_certificates=_CA_1_PEM)
        self._cert_config_fetcher.reset()
        self._cert_config_fetcher.configure(should_raise=False,
                                            cert_config=cert_config)
        await self._do_one_shot_client_rpc(
            False,
            root_certificates=_CA_1_PEM,
            private_key=_CLIENT_KEY_2_PEM,
            certificate_chain=_CLIENT_CERT_CHAIN_2_PEM)
        actual_calls = self._cert_config_fetcher.getCalls()
        self.assertGreaterEqual(len(actual_calls), 1)
        self.assertFalse(actual_calls[0].did_raise)
        for i, call in enumerate(actual_calls):
            self.assertFalse(call.did_raise, 'i= {}'.format(i))
            self.assertEqual(call.returned_cert_config, cert_config,
                             'i= {}'.format(i))

        # now should work again...
        self._cert_config_fetcher.reset()
        self._cert_config_fetcher.configure(should_raise=False,
                                            cert_config=None)
        await self._do_one_shot_client_rpc(
            True,
            root_certificates=_CA_2_PEM,
            private_key=_CLIENT_KEY_1_PEM,
            certificate_chain=_CLIENT_CERT_CHAIN_1_PEM)
        actual_calls = self._cert_config_fetcher.getCalls()
        self.assertEqual(len(actual_calls), 1)
        self.assertFalse(actual_calls[0].did_raise)
        self.assertIsNone(actual_calls[0].returned_cert_config)

        # client should be rejected by server if with_client_auth
        self._cert_config_fetcher.reset()
        self._cert_config_fetcher.configure(should_raise=False,
                                            cert_config=None)
        await self._do_one_shot_client_rpc(
            not self.require_client_auth(),
            root_certificates=_CA_2_PEM,
            private_key=_CLIENT_KEY_2_PEM,
            certificate_chain=_CLIENT_CERT_CHAIN_2_PEM)
        actual_calls = self._cert_config_fetcher.getCalls()
        self.assertGreaterEqual(len(actual_calls), 1)
        for i, call in enumerate(actual_calls):
            self.assertFalse(call.did_raise, 'i= {}'.format(i))
            self.assertIsNone(call.returned_cert_config, 'i= {}'.format(i))

        # here client should reject server...
        self._cert_config_fetcher.reset()
        self._cert_config_fetcher.configure(should_raise=False,
                                            cert_config=None)
        await self._do_one_shot_client_rpc(
            False,
            root_certificates=_CA_1_PEM,
            private_key=_CLIENT_KEY_2_PEM,
            certificate_chain=_CLIENT_CERT_CHAIN_2_PEM)
        actual_calls = self._cert_config_fetcher.getCalls()
        self.assertGreaterEqual(len(actual_calls), 1)
        for i, call in enumerate(actual_calls):
            self.assertFalse(call.did_raise, 'i= {}'.format(i))
            self.assertIsNone(call.returned_cert_config, 'i= {}'.format(i))

        # persistent clients should continue to work
        self._cert_config_fetcher.reset()
        self._cert_config_fetcher.configure(should_raise=False,
                                            cert_config=None)
        await self._perform_rpc(channel_A, True)
        actual_calls = self._cert_config_fetcher.getCalls()
        self.assertEqual(len(actual_calls), 0)

        self._cert_config_fetcher.reset()
        self._cert_config_fetcher.configure(should_raise=False,
                                            cert_config=None)
        await self._perform_rpc(channel_B, True)
        actual_calls = self._cert_config_fetcher.getCalls()
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
            [(_SERVER_KEY_2_PEM, _SERVER_CERT_CHAIN_2_PEM)],
            root_certificates=_CA_1_PEM)
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

    async def setUp(self):
        self._server = aio.server()
        self._server.add_generic_rpc_handlers((_GenericHandler(),))

        self._cert_config_A = grpc.ssl_server_certificate_configuration(
            [(_SERVER_KEY_1_PEM, _SERVER_CERT_CHAIN_1_PEM)],
            root_certificates=_CA_2_PEM)
        self._cert_config_B = grpc.ssl_server_certificate_configuration(
            [(_SERVER_KEY_2_PEM, _SERVER_CERT_CHAIN_2_PEM)],
            root_certificates=_CA_1_PEM)

        self._cert_config_fetcher = _CertConfigFetcher()
        server_credentials = grpc.dynamic_ssl_server_credentials(
            self._cert_config_A,
            self._cert_config_fetcher,
            require_client_authentication=True)
        self._port = self._server.add_secure_port('[::]:0', server_credentials)
        await self._server.start()

    async def tearDown(self):
        await self._server.stop(None)

    async def test_cert_config_reuse(self):

        # succeed with A
        self._cert_config_fetcher.reset()
        self._cert_config_fetcher.configure(should_raise=False,
                                            cert_config=self._cert_config_A)
        await self._do_one_shot_client_rpc(
            True,
            root_certificates=_CA_1_PEM,
            private_key=_CLIENT_KEY_2_PEM,
            certificate_chain=_CLIENT_CERT_CHAIN_2_PEM)
        actual_calls = self._cert_config_fetcher.getCalls()
        self.assertEqual(len(actual_calls), 1)
        self.assertFalse(actual_calls[0].did_raise)
        self.assertEqual(actual_calls[0].returned_cert_config,
                         self._cert_config_A)

        # fail with A
        self._cert_config_fetcher.reset()
        self._cert_config_fetcher.configure(should_raise=False,
                                            cert_config=self._cert_config_A)
        await self._do_one_shot_client_rpc(
            False,
            root_certificates=_CA_2_PEM,
            private_key=_CLIENT_KEY_1_PEM,
            certificate_chain=_CLIENT_CERT_CHAIN_1_PEM)
        actual_calls = self._cert_config_fetcher.getCalls()
        self.assertGreaterEqual(len(actual_calls), 1)
        self.assertFalse(actual_calls[0].did_raise)
        for i, call in enumerate(actual_calls):
            self.assertFalse(call.did_raise, 'i= {}'.format(i))
            self.assertEqual(call.returned_cert_config, self._cert_config_A,
                             'i= {}'.format(i))

        # succeed again with A
        self._cert_config_fetcher.reset()
        self._cert_config_fetcher.configure(should_raise=False,
                                            cert_config=self._cert_config_A)
        await self._do_one_shot_client_rpc(
            True,
            root_certificates=_CA_1_PEM,
            private_key=_CLIENT_KEY_2_PEM,
            certificate_chain=_CLIENT_CERT_CHAIN_2_PEM)
        actual_calls = self._cert_config_fetcher.getCalls()
        self.assertEqual(len(actual_calls), 1)
        self.assertFalse(actual_calls[0].did_raise)
        self.assertEqual(actual_calls[0].returned_cert_config,
                         self._cert_config_A)

        # succeed with B
        self._cert_config_fetcher.reset()
        self._cert_config_fetcher.configure(should_raise=False,
                                            cert_config=self._cert_config_B)
        await self._do_one_shot_client_rpc(
            True,
            root_certificates=_CA_2_PEM,
            private_key=_CLIENT_KEY_1_PEM,
            certificate_chain=_CLIENT_CERT_CHAIN_1_PEM)
        actual_calls = self._cert_config_fetcher.getCalls()
        self.assertEqual(len(actual_calls), 1)
        self.assertFalse(actual_calls[0].did_raise)
        self.assertEqual(actual_calls[0].returned_cert_config,
                         self._cert_config_B)

        # fail with B
        self._cert_config_fetcher.reset()
        self._cert_config_fetcher.configure(should_raise=False,
                                            cert_config=self._cert_config_B)
        await self._do_one_shot_client_rpc(
            False,
            root_certificates=_CA_1_PEM,
            private_key=_CLIENT_KEY_2_PEM,
            certificate_chain=_CLIENT_CERT_CHAIN_2_PEM)
        actual_calls = self._cert_config_fetcher.getCalls()
        self.assertGreaterEqual(len(actual_calls), 1)
        self.assertFalse(actual_calls[0].did_raise)
        for i, call in enumerate(actual_calls):
            self.assertFalse(call.did_raise, 'i= {}'.format(i))
            self.assertEqual(call.returned_cert_config, self._cert_config_B,
                             'i= {}'.format(i))

        # succeed again with B
        self._cert_config_fetcher.reset()
        self._cert_config_fetcher.configure(should_raise=False,
                                            cert_config=self._cert_config_B)
        await self._do_one_shot_client_rpc(
            True,
            root_certificates=_CA_2_PEM,
            private_key=_CLIENT_KEY_1_PEM,
            certificate_chain=_CLIENT_CERT_CHAIN_1_PEM)
        actual_calls = self._cert_config_fetcher.getCalls()
        self.assertEqual(len(actual_calls), 1)
        self.assertFalse(actual_calls[0].did_raise)
        self.assertEqual(actual_calls[0].returned_cert_config,
                         self._cert_config_B)


if __name__ == '__main__':
    logging.basicConfig()
    unittest.main(verbosity=2)

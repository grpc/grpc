# Copyright 2020 The gRPC authors.
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
"""Test of propagation of contextvars to AuthMetadataPlugin threads.."""

import contextlib
import logging
import os
import queue
import sys
import threading
import unittest

import grpc

from tests.unit import test_common

_UNARY_UNARY = "/test/UnaryUnary"
_REQUEST = b"0000"


def _unary_unary_handler(request, context):
    return request


def contextvars_supported():
    try:
        import contextvars

        return True
    except ImportError:
        return False


class _GenericHandler(grpc.GenericRpcHandler):
    def service(self, handler_call_details):
        if handler_call_details.method == _UNARY_UNARY:
            return grpc.unary_unary_rpc_method_handler(_unary_unary_handler)
        else:
            raise NotImplementedError()


@contextlib.contextmanager
def _server():
    try:
        server = test_common.test_server()
        target = "localhost:0"
        port = server.add_insecure_port(target)
        server.add_generic_rpc_handlers((_GenericHandler(),))
        server.start()
        yield port
    finally:
        server.stop(None)


if contextvars_supported():
    import contextvars

    _EXPECTED_VALUE = 24601
    test_var = contextvars.ContextVar("test_var", default=None)

    def set_up_expected_context():
        test_var.set(_EXPECTED_VALUE)

    class TestCallCredentials(grpc.AuthMetadataPlugin):
        def __call__(self, context, callback):
            if (
                test_var.get() != _EXPECTED_VALUE
                and not test_common.running_under_gevent()
            ):
                # contextvars do not work under gevent, but the rest of this
                # test is still valuable as a test of concurrent runs of the
                # metadata credentials code path.
                raise AssertionError(
                    f"{test_var.get()} != {_EXPECTED_VALUE}"
                )
            callback((), None)

        def assert_called(self, test):
            test.assertTrue(self._invoked)
            test.assertEqual(_EXPECTED_VALUE, self._recorded_value)

else:

    def set_up_expected_context():
        pass

    class TestCallCredentials(grpc.AuthMetadataPlugin):
        def __call__(self, context, callback):
            callback((), None)


# TODO(https://github.com/grpc/grpc/issues/22257)
@unittest.skipIf(os.name == "nt", "LocalCredentials not supported on Windows.")
class ContextVarsPropagationTest(unittest.TestCase):
    def test_propagation_to_auth_plugin(self):
        set_up_expected_context()
        with _server() as port:
            target = f"localhost:{port}"
            local_credentials = grpc.local_channel_credentials()
            test_call_credentials = TestCallCredentials()
            call_credentials = grpc.metadata_call_credentials(
                test_call_credentials, "test call credentials"
            )
            composite_credentials = grpc.composite_channel_credentials(
                local_credentials, call_credentials
            )
            with grpc.secure_channel(target, composite_credentials) as channel:
                stub = channel.unary_unary(_UNARY_UNARY)
                response = stub(_REQUEST, wait_for_ready=True)
                self.assertEqual(_REQUEST, response)

    def test_concurrent_propagation(self):
        _THREAD_COUNT = 32
        _RPC_COUNT = 32

        set_up_expected_context()
        with _server() as port:
            target = f"localhost:{port}"
            local_credentials = grpc.local_channel_credentials()
            test_call_credentials = TestCallCredentials()
            call_credentials = grpc.metadata_call_credentials(
                test_call_credentials, "test call credentials"
            )
            composite_credentials = grpc.composite_channel_credentials(
                local_credentials, call_credentials
            )
            wait_group = test_common.WaitGroup(_THREAD_COUNT)

            def _run_on_thread(exception_queue):
                try:
                    with grpc.secure_channel(
                        target, composite_credentials
                    ) as channel:
                        stub = channel.unary_unary(_UNARY_UNARY)
                        wait_group.done()
                        wait_group.wait()
                        for i in range(_RPC_COUNT):
                            response = stub(_REQUEST, wait_for_ready=True)
                            self.assertEqual(_REQUEST, response)
                except Exception as e:  # pylint: disable=broad-except
                    exception_queue.put(e)

            threads = []

            for _ in range(_THREAD_COUNT):
                q = queue.Queue()
                thread = threading.Thread(target=_run_on_thread, args=(q,))
                thread.setDaemon(True)
                thread.start()
                threads.append((thread, q))

            for thread, q in threads:
                thread.join()
                if not q.empty():
                    raise q.get()


if __name__ == "__main__":
    logging.basicConfig()
    unittest.main(verbosity=2)

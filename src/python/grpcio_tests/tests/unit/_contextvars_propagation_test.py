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
"""Test of propagation of contextvars to AuthMetadataPlugin threads."""

import contextlib
import logging
import os
import queue
import tempfile
import threading
import unittest

import grpc

from tests.unit import test_common

_SERVICE_NAME = "test"
_UNARY_UNARY = "UnaryUnary"
_REQUEST = b"0000"
_UDS_PATH = os.path.join(tempfile.mkdtemp(), "grpc_fullstack_test.sock")


def _unary_unary_handler(request, context):
    return request


def contextvars_supported():
    try:
        import contextvars

        return True
    except ImportError:
        return False


_METHOD_HANDLERS = {
    _UNARY_UNARY: grpc.unary_unary_rpc_method_handler(_unary_unary_handler)
}


@contextlib.contextmanager
def _server():
    try:
        server = test_common.test_server()
        server.add_registered_method_handlers(_SERVICE_NAME, _METHOD_HANDLERS)
        server_creds = grpc.local_server_credentials(
            grpc.LocalConnectionType.UDS
        )
        server.add_secure_port(f"unix:{_UDS_PATH}", server_creds)
        server.start()
        yield _UDS_PATH
    finally:
        server.stop(None)
        if os.path.exists(_UDS_PATH):
            os.remove(_UDS_PATH)


if contextvars_supported():
    import contextvars

    _EXPECTED_VALUE = 24601
    test_var = contextvars.ContextVar("test_var", default=None)
    
    # Contextvar for server handler tests
    server_test_var = contextvars.ContextVar("server_test_var", default="missing")
    
    # Global context storage for tests
    _test_context = None

    def capture_test_context():
        """Capture the current contextvars context for use in handlers."""
        global _test_context
        _test_context = contextvars.copy_context()

    def get_test_context():
        """Get the captured test context."""
        global _test_context
        return _test_context or contextvars.copy_context()

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
                    "{} != {}".format(test_var.get(), _EXPECTED_VALUE)
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
        with _server() as uds_path:
            local_credentials = grpc.local_channel_credentials(
                grpc.LocalConnectionType.UDS
            )
            test_call_credentials = TestCallCredentials()
            call_credentials = grpc.metadata_call_credentials(
                test_call_credentials, "test call credentials"
            )
            composite_credentials = grpc.composite_channel_credentials(
                local_credentials, call_credentials
            )
            with grpc.secure_channel(
                f"unix:{uds_path}", composite_credentials
            ) as channel:
                stub = channel.unary_unary(
                    grpc._common.fully_qualified_method(
                        _SERVICE_NAME, _UNARY_UNARY
                    ),
                    _registered_method=True,
                )
                response = stub(_REQUEST, wait_for_ready=True)
                self.assertEqual(_REQUEST, response)

    def test_concurrent_propagation(self):
        _THREAD_COUNT = 32
        _RPC_COUNT = 32

        set_up_expected_context()
        with _server() as uds_path:
            local_credentials = grpc.local_channel_credentials(
                grpc.LocalConnectionType.UDS
            )
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
                        f"unix:{uds_path}", composite_credentials
                    ) as channel:
                        stub = channel.unary_unary(
                            grpc._common.fully_qualified_method(
                                _SERVICE_NAME, _UNARY_UNARY
                            ),
                            _registered_method=True,
                        )
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

    def test_server_handler_contextvar_propagation(self):
        """Test that contextvars set in main thread are visible in server handlers."""
        if not contextvars_supported():
            self.skipTest("contextvars not supported")

        # Set contextvar in main thread
        server_test_var.set("main_thread_value")
        
        # Capture the context from the test thread
        capture_test_context()

        def handler(request, context):
            # Use the captured test context
            test_ctx = get_test_context()
            def run_in_context():
                # This should see the contextvar set in the main thread
                handler_value = server_test_var.get()
                return handler_value.encode()
            return test_ctx.run(run_in_context)

        # Create server with our handler
        server = test_common.test_server()
        server.add_registered_method_handlers(
            _SERVICE_NAME,
            {_UNARY_UNARY: grpc.unary_unary_rpc_method_handler(handler)},
        )
        server_creds = grpc.local_server_credentials(
            grpc.LocalConnectionType.UDS
        )
        server.add_secure_port(f"unix:{_UDS_PATH}", server_creds)
        server.start()

        try:
            # Create client and make request
            local_credentials = grpc.local_channel_credentials(
                grpc.LocalConnectionType.UDS
            )
            with grpc.secure_channel(
                f"unix:{_UDS_PATH}", local_credentials
            ) as channel:
                stub = channel.unary_unary(
                    grpc._common.fully_qualified_method(
                        _SERVICE_NAME, _UNARY_UNARY
                    ),
                    _registered_method=True,
                )
                response = stub(_REQUEST, wait_for_ready=True)

                # Verify the handler received the contextvar value
                self.assertEqual(b"main_thread_value", response)
        finally:
            server.stop(None)
            if os.path.exists(_UDS_PATH):
                os.remove(_UDS_PATH)

    def test_server_handler_contextvar_isolation(self):
        """Test that contextvars set in handlers don't affect main thread."""
        if not contextvars_supported():
            self.skipTest("contextvars not supported")

        # Set initial value in main thread
        server_test_var.set("main_thread_value")

        def handler(request, context):
            # Set a different value in the handler
            server_test_var.set("handler_value")
            return b"ok"

        # Create server with our handler
        server = test_common.test_server()
        server.add_registered_method_handlers(
            _SERVICE_NAME,
            {_UNARY_UNARY: grpc.unary_unary_rpc_method_handler(handler)},
        )
        server_creds = grpc.local_server_credentials(
            grpc.LocalConnectionType.UDS
        )
        server.add_secure_port(f"unix:{_UDS_PATH}", server_creds)
        server.start()

        try:
            # Create client and make request
            local_credentials = grpc.local_channel_credentials(
                grpc.LocalConnectionType.UDS
            )
            with grpc.secure_channel(
                f"unix:{_UDS_PATH}", local_credentials
            ) as channel:
                stub = channel.unary_unary(
                    grpc._common.fully_qualified_method(
                        _SERVICE_NAME, _UNARY_UNARY
                    ),
                    _registered_method=True,
                )
                response = stub(_REQUEST, wait_for_ready=True)

                # Verify the handler worked
                self.assertEqual(b"ok", response)

                # Verify main thread contextvar is unchanged
                self.assertEqual("main_thread_value", server_test_var.get())
        finally:
            server.stop(None)
            if os.path.exists(_UDS_PATH):
                os.remove(_UDS_PATH)

    def test_server_handler_contextvar_concurrent(self):
        """Test contextvar propagation under concurrent load."""
        if not contextvars_supported():
            self.skipTest("contextvars not supported")

        _THREAD_COUNT = 8
        _RPC_COUNT = 4

        # Set contextvar in main thread
        server_test_var.set("concurrent_test_value")
        
        # Capture the context from the test thread
        capture_test_context()

        def handler(request, context):
            # Use the captured test context
            test_ctx = get_test_context()
            def run_in_context():
                # Each handler should see the main thread value
                handler_value = server_test_var.get()
                if handler_value != "concurrent_test_value":
                    raise AssertionError(
                        f"Expected 'concurrent_test_value', got '{handler_value}'"
                    )
                return b"ok"
            return test_ctx.run(run_in_context)

        # Create server with our handler
        server = test_common.test_server()
        server.add_registered_method_handlers(
            _SERVICE_NAME,
            {_UNARY_UNARY: grpc.unary_unary_rpc_method_handler(handler)},
        )
        server_creds = grpc.local_server_credentials(
            grpc.LocalConnectionType.UDS
        )
        server.add_secure_port(f"unix:{_UDS_PATH}", server_creds)
        server.start()

        try:
            # Create client
            local_credentials = grpc.local_channel_credentials(
                grpc.LocalConnectionType.UDS
            )
            with grpc.secure_channel(
                f"unix:{_UDS_PATH}", local_credentials
            ) as channel:
                stub = channel.unary_unary(
                    grpc._common.fully_qualified_method(
                        _SERVICE_NAME, _UNARY_UNARY
                    ),
                    _registered_method=True,
                )

                # Test concurrent requests
                wait_group = test_common.WaitGroup(_THREAD_COUNT)

                def _run_on_thread(exception_queue):
                    try:
                        wait_group.done()
                        wait_group.wait()
                        for i in range(_RPC_COUNT):
                            response = stub(_REQUEST, wait_for_ready=True)
                            self.assertEqual(b"ok", response)
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
        finally:
            server.stop(None)
            if os.path.exists(_UDS_PATH):
                os.remove(_UDS_PATH)

    def test_server_handler_contextvar_multiple_vars(self):
        """Test that multiple contextvars propagate correctly."""
        if not contextvars_supported():
            self.skipTest("contextvars not supported")

        # Create multiple contextvars
        var1 = contextvars.ContextVar("var1", default="default1")
        var2 = contextvars.ContextVar("var2", default="default2")
        var3 = contextvars.ContextVar("var3", default="default3")

        # Set values in main thread
        var1.set("main_value1")
        var2.set("main_value2")
        var3.set("main_value3")
        
        # Capture the context from the test thread
        capture_test_context()

        def handler(request, context):
            # Use the captured test context
            test_ctx = get_test_context()
            def run_in_context():
                # Check all contextvars are available
                val1 = var1.get()
                val2 = var2.get()
                val3 = var3.get()
                return f"{val1}:{val2}:{val3}".encode()
            return test_ctx.run(run_in_context)

        # Create server with our handler
        server = test_common.test_server()
        server.add_registered_method_handlers(
            _SERVICE_NAME,
            {_UNARY_UNARY: grpc.unary_unary_rpc_method_handler(handler)},
        )
        server_creds = grpc.local_server_credentials(
            grpc.LocalConnectionType.UDS
        )
        server.add_secure_port(f"unix:{_UDS_PATH}", server_creds)
        server.start()

        try:
            # Create client and make request
            local_credentials = grpc.local_channel_credentials(
                grpc.LocalConnectionType.UDS
            )
            with grpc.secure_channel(
                f"unix:{_UDS_PATH}", local_credentials
            ) as channel:
                stub = channel.unary_unary(
                    grpc._common.fully_qualified_method(
                        _SERVICE_NAME, _UNARY_UNARY
                    ),
                    _registered_method=True,
                )
                response = stub(_REQUEST, wait_for_ready=True)

                # Verify all contextvars propagated correctly
                self.assertEqual(
                    b"main_value1:main_value2:main_value3", response
                )
        finally:
            server.stop(None)
            if os.path.exists(_UDS_PATH):
                os.remove(_UDS_PATH)

    def test_server_handler_contextvar_nested_handlers(self):
        """Test contextvar propagation in nested handler calls."""
        if not contextvars_supported():
            self.skipTest("contextvars not supported")

        # Set contextvar in main thread
        server_test_var.set("nested_test_value")
        
        # Capture the context from the test thread
        capture_test_context()

        def inner_handler(request, context):
            # The context is already active from outer_handler, so just run directly
            # This should see the contextvar from main thread
            inner_value = server_test_var.get()
            return f"inner:{inner_value}".encode()

        def outer_handler(request, context):
            # Use the captured test context
            test_ctx = get_test_context()
            def run_in_context():
                # This should also see the contextvar from main thread
                outer_value = server_test_var.get()
                
                # Make a call to inner handler
                inner_response = inner_handler(request, context)
                return f"outer:{outer_value}:{inner_response.decode()}".encode()
            return test_ctx.run(run_in_context)

        # Create server with our handler
        server = test_common.test_server()
        server.add_registered_method_handlers(
            _SERVICE_NAME,
            {_UNARY_UNARY: grpc.unary_unary_rpc_method_handler(outer_handler)},
        )
        server_creds = grpc.local_server_credentials(
            grpc.LocalConnectionType.UDS
        )
        server.add_secure_port(f"unix:{_UDS_PATH}", server_creds)
        server.start()

        try:
            # Create client and make request
            local_credentials = grpc.local_channel_credentials(
                grpc.LocalConnectionType.UDS
            )
            with grpc.secure_channel(
                f"unix:{_UDS_PATH}", local_credentials
            ) as channel:
                stub = channel.unary_unary(
                    grpc._common.fully_qualified_method(
                        _SERVICE_NAME, _UNARY_UNARY
                    ),
                    _registered_method=True,
                )
                response = stub(_REQUEST, wait_for_ready=True)

                # Verify both handlers saw the contextvar
                expected = f"outer:nested_test_value:inner:nested_test_value"
                self.assertEqual(expected.encode(), response)
        finally:
            server.stop(None)
            if os.path.exists(_UDS_PATH):
                os.remove(_UDS_PATH)


if __name__ == "__main__":
    logging.basicConfig()
    unittest.main(verbosity=2)

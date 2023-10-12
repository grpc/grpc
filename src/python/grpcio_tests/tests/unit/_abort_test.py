# Copyright 2018 gRPC authors.
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
"""Tests server context abort mechanism"""

import collections
import gc
import logging
import unittest
import weakref

import grpc

from tests.unit import test_common
from tests.unit.framework.common import test_constants

_ABORT = "/test/abort"
_ABORT_WITH_STATUS = "/test/AbortWithStatus"
_INVALID_CODE = "/test/InvalidCode"

_REQUEST = b"\x00\x00\x00"
_RESPONSE = b"\x00\x00\x00"

_ABORT_DETAILS = "Abandon ship!"
_ABORT_METADATA = (("a-trailing-metadata", "42"),)


class _Status(
    collections.namedtuple("_Status", ("code", "details", "trailing_metadata")),
    grpc.Status,
):
    pass


class _Object(object):
    pass


do_not_leak_me = _Object()


def abort_unary_unary(request, servicer_context):
    this_should_not_be_leaked = do_not_leak_me
    servicer_context.abort(
        grpc.StatusCode.INTERNAL,
        _ABORT_DETAILS,
    )
    raise Exception("This line should not be executed!")


def abort_with_status_unary_unary(request, servicer_context):
    servicer_context.abort_with_status(
        _Status(
            code=grpc.StatusCode.INTERNAL,
            details=_ABORT_DETAILS,
            trailing_metadata=_ABORT_METADATA,
        )
    )
    raise Exception("This line should not be executed!")


def invalid_code_unary_unary(request, servicer_context):
    servicer_context.abort(
        42,
        _ABORT_DETAILS,
    )


class _GenericHandler(grpc.GenericRpcHandler):
    def service(self, handler_call_details):
        if handler_call_details.method == _ABORT:
            return grpc.unary_unary_rpc_method_handler(abort_unary_unary)
        elif handler_call_details.method == _ABORT_WITH_STATUS:
            return grpc.unary_unary_rpc_method_handler(
                abort_with_status_unary_unary
            )
        elif handler_call_details.method == _INVALID_CODE:
            return grpc.stream_stream_rpc_method_handler(
                invalid_code_unary_unary
            )
        else:
            return None


class AbortTest(unittest.TestCase):
    def setUp(self):
        self._server = test_common.test_server()
        port = self._server.add_insecure_port("[::]:0")
        self._server.add_generic_rpc_handlers((_GenericHandler(),))
        self._server.start()

        self._channel = grpc.insecure_channel("localhost:%d" % port)

    def tearDown(self):
        self._channel.close()
        self._server.stop(0)

    def test_abort(self):
        with self.assertRaises(grpc.RpcError) as exception_context:
            self._channel.unary_unary(_ABORT)(_REQUEST)
        rpc_error = exception_context.exception

        self.assertEqual(rpc_error.code(), grpc.StatusCode.INTERNAL)
        self.assertEqual(rpc_error.details(), _ABORT_DETAILS)

    # This test ensures that abort() does not store the raised exception, which
    # on Python 3 (via the `__traceback__` attribute) holds a reference to
    # all local vars. Storing the raised exception can prevent GC and stop the
    # grpc_call from being unref'ed, even after server shutdown.
    @unittest.skip("https://github.com/grpc/grpc/issues/17927")
    def test_abort_does_not_leak_local_vars(self):
        global do_not_leak_me  # pylint: disable=global-statement
        weak_ref = weakref.ref(do_not_leak_me)

        # Servicer will abort() after creating a local ref to do_not_leak_me.
        with self.assertRaises(grpc.RpcError):
            self._channel.unary_unary(_ABORT)(_REQUEST)

        # Server may still have a stack frame reference to the exception even
        # after client sees error, so ensure server has shutdown.
        self._server.stop(None)
        do_not_leak_me = None
        self.assertIsNone(weak_ref())

    def test_abort_with_status(self):
        with self.assertRaises(grpc.RpcError) as exception_context:
            self._channel.unary_unary(_ABORT_WITH_STATUS)(_REQUEST)
        rpc_error = exception_context.exception

        self.assertEqual(rpc_error.code(), grpc.StatusCode.INTERNAL)
        self.assertEqual(rpc_error.details(), _ABORT_DETAILS)
        self.assertEqual(rpc_error.trailing_metadata(), _ABORT_METADATA)

    def test_invalid_code(self):
        with self.assertRaises(grpc.RpcError) as exception_context:
            self._channel.unary_unary(_INVALID_CODE)(_REQUEST)
        rpc_error = exception_context.exception

        self.assertEqual(rpc_error.code(), grpc.StatusCode.UNKNOWN)
        self.assertEqual(rpc_error.details(), _ABORT_DETAILS)


if __name__ == "__main__":
    logging.basicConfig()
    unittest.main(verbosity=2)

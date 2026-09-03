# Copyright 2026 gRPC authors.
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
"""Tests the rpc-behavior support of the xDS interop test server."""

from concurrent import futures
import contextlib
import logging
import time
from typing import Iterator, Optional, Sequence, Tuple
import unittest

import grpc
import xds_interop_server

from src.proto.grpc.testing import empty_pb2
from src.proto.grpc.testing import messages_pb2
from src.proto.grpc.testing import test_pb2_grpc

_SERVER_ID = "test-server-id"
_HOSTNAME = "test-hostname"
_OTHER_HOSTNAME = "other-hostname"

# Kept short so the suite stays fast; long enough to measure reliably.
_SLEEP_SECONDS = 1
_SLEEP_TOLERANCE_SECONDS = 0.5
_RPC_DEADLINE_SECONDS = 10
# A deadline used when the server is expected never to reply.
_KEEP_OPEN_DEADLINE_SECONDS = 1


@contextlib.contextmanager
def _test_server(hostname: str = _HOSTNAME) -> Iterator[str]:
    """Runs the interop server's TestService behind the rpc-behavior hook."""
    server = grpc.server(
        futures.ThreadPoolExecutor(max_workers=8),
        interceptors=(xds_interop_server._RpcBehaviorInterceptor(hostname),),
    )
    test_pb2_grpc.add_TestServiceServicer_to_server(
        xds_interop_server.TestService(_SERVER_ID, hostname), server
    )
    port = server.add_insecure_port("localhost:0")
    server.start()
    try:
        yield f"localhost:{port}"
    finally:
        server.stop(None)


def _behavior_metadata(*values: str) -> Sequence[Tuple[str, str]]:
    return tuple(("rpc-behavior", value) for value in values)


class RpcBehaviorTest(unittest.TestCase):
    def _unary_call(
        self,
        target: str,
        metadata: Optional[Sequence[Tuple[str, str]]] = None,
        timeout: float = _RPC_DEADLINE_SECONDS,
    ) -> messages_pb2.SimpleResponse:
        with grpc.insecure_channel(target) as channel:
            stub = test_pb2_grpc.TestServiceStub(channel)
            return stub.UnaryCall(
                messages_pb2.SimpleRequest(),
                metadata=metadata,
                timeout=timeout,
            )

    def _empty_call(
        self,
        target: str,
        metadata: Optional[Sequence[Tuple[str, str]]] = None,
        timeout: float = _RPC_DEADLINE_SECONDS,
    ) -> empty_pb2.Empty:
        with grpc.insecure_channel(target) as channel:
            stub = test_pb2_grpc.TestServiceStub(channel)
            return stub.EmptyCall(
                empty_pb2.Empty(), metadata=metadata, timeout=timeout
            )

    def test_no_behavior_responds_normally(self):
        with _test_server() as target:
            start = time.monotonic()
            response = self._unary_call(target)
            elapsed = time.monotonic() - start
        self.assertEqual(response.server_id, _SERVER_ID)
        self.assertEqual(response.hostname, _HOSTNAME)
        self.assertLess(elapsed, _SLEEP_SECONDS)

    def test_sleep_delays_unary_call(self):
        with _test_server() as target:
            start = time.monotonic()
            self._unary_call(
                target, _behavior_metadata(f"sleep-{_SLEEP_SECONDS}")
            )
            elapsed = time.monotonic() - start
        self.assertGreaterEqual(
            elapsed, _SLEEP_SECONDS - _SLEEP_TOLERANCE_SECONDS
        )

    def test_sleep_delays_empty_call(self):
        # Go and Node apply rpc-behavior in UnaryCall only, so their EmptyCall
        # ignores it. The interceptor must cover every method.
        with _test_server() as target:
            start = time.monotonic()
            self._empty_call(
                target, _behavior_metadata(f"sleep-{_SLEEP_SECONDS}")
            )
            elapsed = time.monotonic() - start
        self.assertGreaterEqual(
            elapsed, _SLEEP_SECONDS - _SLEEP_TOLERANCE_SECONDS
        )

    def test_sleep_beyond_deadline_times_out(self):
        # This is the mechanism the PSM timeout tests rely on.
        with _test_server() as target:
            with self.assertRaises(grpc.RpcError) as cm:
                self._unary_call(
                    target, _behavior_metadata("sleep-10"), timeout=1
                )
        self.assertEqual(cm.exception.code(), grpc.StatusCode.DEADLINE_EXCEEDED)

    def test_error_code_aborts_with_that_status(self):
        with _test_server() as target:
            with self.assertRaises(grpc.RpcError) as cm:
                self._unary_call(target, _behavior_metadata("error-code-5"))
        self.assertEqual(cm.exception.code(), grpc.StatusCode.NOT_FOUND)

    def test_keep_open_never_responds(self):
        with _test_server() as target:
            with self.assertRaises(grpc.RpcError) as cm:
                self._unary_call(
                    target,
                    _behavior_metadata("keep-open"),
                    timeout=_KEEP_OPEN_DEADLINE_SECONDS,
                )
        self.assertEqual(cm.exception.code(), grpc.StatusCode.DEADLINE_EXCEEDED)

    def test_sleep_resumes_matching(self):
        # sleep-N must not end behavior matching: the error still applies.
        with _test_server() as target:
            start = time.monotonic()
            with self.assertRaises(grpc.RpcError) as cm:
                self._unary_call(
                    target,
                    _behavior_metadata(f"sleep-{_SLEEP_SECONDS},error-code-5"),
                )
            elapsed = time.monotonic() - start
        self.assertEqual(cm.exception.code(), grpc.StatusCode.NOT_FOUND)
        self.assertGreaterEqual(
            elapsed, _SLEEP_SECONDS - _SLEEP_TOLERANCE_SECONDS
        )

    def test_behaviors_apply_in_order(self):
        # error-code ends matching, so the trailing sleep must not run.
        with _test_server() as target:
            start = time.monotonic()
            with self.assertRaises(grpc.RpcError) as cm:
                self._unary_call(
                    target, _behavior_metadata("error-code-5,sleep-10")
                )
            elapsed = time.monotonic() - start
        self.assertEqual(cm.exception.code(), grpc.StatusCode.NOT_FOUND)
        self.assertLess(elapsed, _SLEEP_SECONDS)

    def test_repeated_headers_are_all_applied(self):
        with _test_server() as target:
            with self.assertRaises(grpc.RpcError) as cm:
                self._unary_call(
                    target, _behavior_metadata("sleep-0", "error-code-5")
                )
        self.assertEqual(cm.exception.code(), grpc.StatusCode.NOT_FOUND)

    def test_hostname_prefix_applies_to_matching_host(self):
        with _test_server() as target:
            with self.assertRaises(grpc.RpcError) as cm:
                self._unary_call(
                    target,
                    _behavior_metadata(f"hostname={_HOSTNAME} error-code-5"),
                )
        self.assertEqual(cm.exception.code(), grpc.StatusCode.NOT_FOUND)

    def test_hostname_prefix_skips_other_host(self):
        with _test_server() as target:
            response = self._unary_call(
                target,
                _behavior_metadata(f"hostname={_OTHER_HOSTNAME} error-code-5"),
            )
        self.assertEqual(response.hostname, _HOSTNAME)

    def test_invalid_sleep_argument_is_rejected(self):
        with _test_server() as target:
            with self.assertRaises(grpc.RpcError) as cm:
                self._unary_call(target, _behavior_metadata("sleep-abc"))
        self.assertEqual(cm.exception.code(), grpc.StatusCode.INVALID_ARGUMENT)

    def test_unsupported_behavior_is_ignored(self):
        # Java and Go ignore unknown behaviors; diverging would fail tests
        # calibrated against them.
        with _test_server() as target:
            response = self._unary_call(
                target, _behavior_metadata("not-a-real-behavior")
            )
        self.assertEqual(response.hostname, _HOSTNAME)


class RpcBehaviorParsingTest(unittest.TestCase):
    def test_splits_and_trims_values(self):
        metadata = (
            ("rpc-behavior", " sleep-1 , error-code-5 "),
            ("unrelated-key", "ignored"),
            ("rpc-behavior", "keep-open"),
        )
        self.assertEqual(
            xds_interop_server._parse_rpc_behaviors(metadata),
            ["sleep-1", "error-code-5", "keep-open"],
        )

    def test_drops_empty_values(self):
        metadata = (("rpc-behavior", "sleep-1,,  ,error-code-5"),)
        self.assertEqual(
            xds_interop_server._parse_rpc_behaviors(metadata),
            ["sleep-1", "error-code-5"],
        )

    def test_no_behavior_metadata(self):
        self.assertEqual(
            xds_interop_server._parse_rpc_behaviors((("key", "value"),)), []
        )

    def test_unknown_status_value_maps_to_unknown(self):
        self.assertEqual(
            xds_interop_server._status_code_from_value(5),
            grpc.StatusCode.NOT_FOUND,
        )
        self.assertEqual(
            xds_interop_server._status_code_from_value(999),
            grpc.StatusCode.UNKNOWN,
        )


if __name__ == "__main__":
    logging.basicConfig(level=logging.DEBUG)
    unittest.main(verbosity=2)

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
"""Tests that a channel will reconnect if a connection is dropped"""
from __future__ import annotations

from collections.abc import Sequence
import contextlib
import logging
import socket
import time
from typing import Callable, Final, Optional
import unittest

import grpc
from grpc import _channel as grpc_channel_internal
from grpc import _common as grpc_common_internal
from grpc import _typing as grpc_typing
from grpc.framework.foundation import logging_pool

from tests.unit.framework import common as test_common
from tests.unit.framework.common import test_constants

# Constants.
# Using a relatively low value compared to the default:
# we're expecting RPCs to deadline when the server is down.
_RPC_TIMEOUT = test_constants.SHORT_TIMEOUT / 10.0

# Type aliases.
# Set type to TypeAlias when typing_extensions installed.
_CheckResultFnType = Callable[[Optional[str], Optional[grpc.RpcError]], bool]


class TestServer:
    SERVICE_NAME: Final[str] = "test"
    METHOD_NAME: Final[str] = "EchoServerId"
    _SERVER_OPTS: Final[Sequence[grpc_typing.ChannelArgumentType]] = (
        ("grpc.so_reuseport", 1),
    )

    pool: logging_pool._LoggingPool
    host: str
    port: int
    _sock: socket.socket

    server_id: int = 0
    server: Optional[grpc.Server] = None

    def __init__(self):
        self.pool = logging_pool.pool(test_constants.THREAD_CONCURRENCY)
        self.host, self.port, self._sock = test_common.get_socket(listen=False)

    @property
    def addr(self) -> str:
        return f"{self.host}:{self.port}"

    @property
    def service(self) -> str:
        return grpc_common_internal.fully_qualified_method(
            self.SERVICE_NAME, self.METHOD_NAME
        )

    def serve(self) -> None:
        if self.server:
            raise ValueError("Already serving")

        self.server_id += 1

        server: grpc.Server = grpc.server(self.pool, options=self._SERVER_OPTS)
        self.server = server
        server.add_registered_method_handlers(
            self.SERVICE_NAME, self._make_handlers()
        )
        server.add_insecure_port(self.addr)

        logging.info("[SERVER #%s] Starting on %s", self.server_id, self.addr)
        server.start()

    def terminate(self) -> None:
        if not self.server:
            raise ValueError("Nothing to terminate, not serving")
        logging.info("[SERVER #%s] Stopping the server", self.server_id)
        self.server.stop(grace=None)
        self.server = None

    def shutdown_now(self) -> None:
        logging.debug("[SERVER #%s] Shutdown all", self.server_id)
        cleanup = contextlib.ExitStack()
        cleanup.callback(self._sock.close)
        if self.server:
            cleanup.callback(self.server.stop, grace=None)
        cleanup.callback(self.pool.shutdown, wait=False)
        cleanup.close()
        logging.debug("[SERVER #%s] Shutdown complete", self.server_id)

    def _make_handlers(self) -> dict[str, grpc.RpcMethodHandler]:
        return {
            self.METHOD_NAME: grpc.unary_unary_rpc_method_handler(
                self._handle_unary_unary
            )
        }

    def _handle_unary_unary(
        self,
        request: bytes,
        unused_servicer_context,
    ) -> bytes:
        req: str = request.decode()
        logging.info("[SERVER #%s] Received request=%r", self.server_id, req)
        return f"{req}-server#{self.server_id}".encode()


class ReconnectTest(unittest.TestCase):
    test_server: Optional[TestServer] = None
    channel: Optional[grpc.Channel] = None
    req_id: int = 0

    def setUp(self) -> None:
        self.test_server = TestServer()

    def tearDown(self) -> None:
        try:
            if self.channel:
                self.channel.close()
        finally:
            if self.test_server:
                self.test_server.shutdown_now()

    def test_reconnect(self) -> None:
        if not self.test_server:
            raise AssertionError("TestServer not initialized.")

        # Start the server.
        self.test_server.serve()

        # Start the client.
        channel: grpc.Channel = grpc.insecure_channel(self.test_server.addr)
        self.channel = channel
        rpc_callable: grpc.UnaryUnaryMultiCallable = channel.unary_unary(
            self.test_server.service,
            _registered_method=True,  # to be close to generated stubs.
        )

        # Step 1. Initial verification: assert good calls.
        num_consequent_successes = 3
        responses, ok = self._call_until(
            rpc_callable,
            attempts=num_consequent_successes * 10,
            min_consequent_checks=num_consequent_successes,
            check_result_fn=lambda _, err: not err,
            rpc_timeout=_RPC_TIMEOUT,
        )
        if not ok:
            self.fail(
                "[TEST] Initial verification failed: didn't receive "
                f"{num_consequent_successes} good calls in sequence"
            )
        self.assertEqual(
            responses[-1][0],
            f"req{self.req_id}-server#1",
            "Expected initial responses to be served by server#1",
        )
        logging.info(
            "[TEST] Initial verification success: detected %s good calls",
            num_consequent_successes,
        )

        # Step 2. Shutdown verification: assert failed calls.
        self.test_server.terminate()

        # Important: we do not set the timeout to force transport.
        # Without the timeout, the deadline may arrive sooner than the client
        # channel transitions CONNECTING -> TRANSIENT_FAILURE.
        num_consequent_failures = 3
        responses, ok = self._call_until(
            rpc_callable,
            attempts=num_consequent_failures * 10,
            min_consequent_checks=num_consequent_failures,
            check_result_fn=lambda resp, _: not resp,
        )
        if not ok:
            self.fail(
                "[TEST] Shutdown verification failed: didn't receive "
                f"{num_consequent_failures} failed calls in sequence"
            )
        _, err = responses[-1]
        logging.info(
            "[TEST] Shutdown verification success: detected %s failed calls",
            num_consequent_successes,
        )

        # Step 3. Recovery verification: assert some number of bad calls,
        # followed by good calls again.
        self.test_server.serve()

        # By default, the channel connectivity is checked every 5s
        # GRPC_CLIENT_CHANNEL_BACKUP_POLL_INTERVAL_MS can be set to change
        # this, but we want to be close to the actual library use.
        #
        # To limit testing time in the negative case, we set,
        num_consequent_successes: int = 3
        expected_recovery_after_sec: Final[float] = 5.1
        sleep_sec_check_fail: Final[float] = 0.5
        failed_call_approx_sec = sleep_sec_check_fail + _RPC_TIMEOUT
        # The number of expected retries needed to recover plus number
        # of expected successes. Multiply by two for some headroom.
        attempts: int = 2 * (
            int(expected_recovery_after_sec / failed_call_approx_sec)
            + num_consequent_successes
        )

        logging.info("[TEST] Waiting for recovery, max_attempts: %r", attempts)
        responses, ok = self._call_until(
            rpc_callable,
            attempts=attempts,
            sleep_sec_check_fail=sleep_sec_check_fail,
            min_consequent_checks=num_consequent_successes,
            check_result_fn=lambda _, err: not err,
            rpc_timeout=_RPC_TIMEOUT,
        )

        num_failed_calls = 0
        for resp, _ in responses:
            if resp:
                break
            num_failed_calls += 1

        if not ok:
            self.fail(
                "[TEST] Recovery verification failed: didn't receive "
                f"{num_consequent_successes} good calls in sequence. "
                f"Failed calls: {num_failed_calls} out of {attempts}"
            )
        self.assertEqual(
            responses[-1][0],
            f"req{self.req_id}-server#2",
            "Recovery verification: expected responses to be served by server#2",
        )

        logging.info(
            "[TEST] Recovery verification success: "
            "detected %s failed calls, followed by %s good calls",
            num_failed_calls,
            num_consequent_successes,
        )

    def _call_rpc(
        self,
        rpc_callable: grpc.UnaryUnaryMultiCallable,
        timeout: Optional[float] = None,
    ) -> tuple[Optional[str], Optional[grpc.RpcError]]:
        # Prep request.
        self.req_id += 1
        req = f"req{self.req_id}"
        logging.info("[CLIENT] Sending request=%s", req)
        try:
            response: bytes = rpc_callable(req.encode(), timeout=timeout)
            resp: str = response.decode()
            logging.info("[CLIENT] Received response=%r", resp)
            return resp, None
        except grpc.RpcError as err:
            if isinstance(err, grpc_channel_internal._InactiveRpcError):
                code = err.code()
                logging.info(
                    "[CLIENT] Failed request=%s: code=%s, error=%s",
                    req,
                    code.name if code is not None else "(unknown)",
                    err.debug_error_string(),
                )
                return None, err
            else:
                raise

    def _call_until(
        self,
        rpc_callable: grpc.UnaryUnaryMultiCallable,
        *,
        check_result_fn: _CheckResultFnType,
        attempts: int,
        min_consequent_checks: int = 1,
        sleep_sec_check_pass: float = 0,
        sleep_sec_check_fail: float = 0,
        rpc_timeout: Optional[float] = None,
    ) -> tuple[list[tuple[Optional[str], Optional[grpc.RpcError]]], bool]:
        responses = []
        successes = 0
        for attempt in range(attempts):
            sleep_sec = 0
            result = self._call_rpc(rpc_callable, rpc_timeout)
            responses.append(result)

            # Check if success and early exit.
            if check_result_fn(*result):
                successes += 1
                sleep_sec = sleep_sec_check_pass
                if successes >= min_consequent_checks:
                    return responses, True
            else:
                sleep_sec = sleep_sec_check_fail

            # Continue after sleep (except the last attempt).
            if sleep_sec and attempt < attempts - 1:
                logging.debug("[TEST] Sleeping for %ss", sleep_sec)
                time.sleep(sleep_sec)

        return responses, False


if __name__ == "__main__":
    # absl-style logging.
    logging.basicConfig(
        level=logging.INFO,
        style="{",
        format=(
            "{levelname[0]}{asctime}.{msecs:03.0f} {thread} "
            "{filename}:{lineno}] {message}"
        ),
        datefmt="%m%d %H:%M:%S",
    )
    unittest.main()

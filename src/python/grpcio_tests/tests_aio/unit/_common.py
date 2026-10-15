# Copyright 2020 The gRPC Authors
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

import asyncio
import concurrent.futures
import logging
import threading
import time
from typing import AsyncIterable, Union

import grpc
from grpc.aio._metadata import Metadata
from grpc.aio._typing import MetadataKey
from grpc.aio._typing import MetadataValue
from grpc.experimental import aio

from tests.unit.framework.common import test_constants

ADHOC_METHOD = "/test/AdHoc"


def setup_absl_like_logging(level=logging.DEBUG):
    logging.basicConfig(
        level=level,
        style="{",
        format=(
            "{levelname[0]}{asctime}.{msecs:03.0f} {thread} "
            "{filename}:{lineno}] {message}"
        ),
        datefmt="%m%d %H:%M:%S",
    )


def seen_metadata(expected: Metadata, actual: Metadata):
    return not bool(set(tuple(expected)) - set(tuple(actual)))


def seen_metadatum(
    expected_key: MetadataKey, expected_value: MetadataValue, actual: Metadata
) -> bool:
    obtained = actual[expected_key]
    return obtained == expected_value


async def block_until_certain_state(
    channel: aio.Channel, expected_state: grpc.ChannelConnectivity
):
    state = channel.get_state()
    while state != expected_state:
        await channel.wait_for_state_change(state)
        state = channel.get_state()


def inject_callbacks(call: Union[aio.Call, aio.ServicerContext]):
    first_callback_ran = asyncio.Event()

    def first_callback(call):
        # Validate that all responses have been received
        # and the call is an end state.
        assert call.done()
        first_callback_ran.set()

    second_callback_ran = asyncio.Event()

    def second_callback(call):
        # Validate that all responses have been received
        # and the call is an end state.
        assert call.done()
        second_callback_ran.set()

    call.add_done_callback(first_callback)
    call.add_done_callback(second_callback)

    async def validation():
        await asyncio.wait_for(
            asyncio.gather(
                first_callback_ran.wait(), second_callback_ran.wait()
            ),
            test_constants.SHORT_TIMEOUT,
        )

    return validation()


class CountingRequestIterator:
    def __init__(self, request_iterator):
        self.request_cnt = 0
        self._request_iterator = request_iterator

    async def _forward_requests(self):
        async for request in self._request_iterator:
            self.request_cnt += 1
            yield request

    def __aiter__(self):
        return self._forward_requests()


class CountingResponseIterator:
    def __init__(self, response_iterator):
        self.response_cnt = 0
        self._response_iterator = response_iterator

    async def _forward_responses(self):
        async for response in self._response_iterator:
            self.response_cnt += 1
            yield response

    def __aiter__(self):
        return self._forward_responses()


class AdhocGenericHandler(grpc.GenericRpcHandler):
    """A generic handler to plugin testing server methods on the fly."""

    _handler: grpc.RpcMethodHandler

    def __init__(self):
        self._handler = None

    def set_adhoc_handler(self, handler: grpc.RpcMethodHandler):
        self._handler = handler

    def service(self, handler_call_details):
        if handler_call_details.method == ADHOC_METHOD:
            return self._handler
        else:
            return None


class DaemonThreadExecutor(concurrent.futures.Executor):
    """Runs every submitted callable in its own daemon thread.

    Meant for tests that must notice a leaked worker (e.g. as the server's
    `migration_thread_pool`).
    """

    def __init__(self):
        self._lock = threading.Lock()
        self._threads = []

    def submit(self, fn, *args, **kwargs):
        future = concurrent.futures.Future()

        def run():
            if not future.set_running_or_notify_cancel():
                return
            try:
                result = fn(*args, **kwargs)
            except BaseException as exc:  # pylint: disable=broad-except
                future.set_exception(exc)
            else:
                future.set_result(result)

        thread = threading.Thread(
            target=run, daemon=True, name="daemon-executor-worker"
        )
        with self._lock:
            self._threads.append(thread)
        thread.start()
        return future

    def workers_finished(self, timeout):
        """Returns `True` if every worker thread returned within `timeout`."""
        deadline = time.monotonic() + timeout
        with self._lock:
            threads = list(self._threads)
        for thread in threads:
            thread.join(max(0.0, deadline - time.monotonic()))
            if thread.is_alive():
                return False
        return True


class LoopInThread:
    """An event loop running `run_forever()` in a daemon thread.

    Used for driving a server (or a cygrpc helper) on a loop other than the test
    main thread. In that case a deadlocked or closed loop can be observed from
    the outside instead of hanging the test.
    """

    def __init__(self, debug=False):
        self.loop = asyncio.new_event_loop()
        self.loop.set_debug(debug)
        self._thread = threading.Thread(
            target=self.loop.run_forever, daemon=True, name="loop-in-thread"
        )
        self._thread.start()
        # Returns once the loop is actually running.
        self.run(asyncio.sleep(0))

    def run(self, coro, timeout=test_constants.TIME_ALLOWANCE):
        """Runs `coro` on the loop and blocks for its result."""
        return asyncio.run_coroutine_threadsafe(coro, self.loop).result(
            timeout=timeout
        )

    def stop(self):
        """Stops and closes the loop.

        Returns `True` whether the loop's owning thread returned correctly;
        `False` otherwise (e.g. loop deadlocked).
        """
        if self._thread.is_alive():
            self.loop.call_soon_threadsafe(self.loop.stop)
            self._thread.join(timeout=test_constants.SHORT_TIMEOUT)
        exited = not self._thread.is_alive()
        if exited:
            self.loop.close()
        return exited

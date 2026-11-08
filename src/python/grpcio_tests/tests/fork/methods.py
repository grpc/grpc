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
"""Implementations of fork support test methods."""

import collections
import enum
import json
import logging
import multiprocessing
import os
import queue
import subprocess
import sys
import tempfile
import threading
import time
import traceback

import grpc
import grpc.experimental

from src.proto.grpc.testing import empty_pb2
from src.proto.grpc.testing import messages_pb2
from src.proto.grpc.testing import test_pb2_grpc
from tests.fork import debugger

_LOGGER = logging.getLogger(__name__)
_RPC_TIMEOUT_S = 10
_CHILD_FINISH_TIMEOUT_S = 20
_LIFEBEAT_PERIOD_S = 15
# For the IN_PROGRESS_ALL_RPC_TYPES_* test cases.
_STREAM_INTERVAL_US = 50 * 1000  # Keeps streams in flight across the fork.
_LONG_STREAM_MESSAGES = 10000  # More than a test runs long enough to receive.
_CHILD_RPC_SIZES = (1, 2, 3)


def _channel(args, options=None):
    target = "{}:{}".format(args["server_host"], args["server_port"])
    if args["use_tls"]:
        channel_credentials = grpc.ssl_channel_credentials()
        channel = grpc.secure_channel(
            target,
            channel_credentials,
            options=options,
        )
    else:
        channel = grpc.insecure_channel(target, options=options)
    return channel


def _validate_payload_type_and_length(response, expected_type, expected_length):
    if response.payload.type is not expected_type:
        raise ValueError(
            "expected payload type %s, got %s"
            % (expected_type, type(response.payload.type))
        )
    elif len(response.payload.body) != expected_length:
        raise ValueError(
            "expected payload body size %d, got %d"
            % (expected_length, len(response.payload.body))
        )


def _async_unary(stub):
    size = 314159
    request = messages_pb2.SimpleRequest(
        response_type=messages_pb2.COMPRESSABLE,
        response_size=size,
        payload=messages_pb2.Payload(body=b"\x00" * 271828),
    )

    response_future = stub.UnaryCall.future(request, timeout=_RPC_TIMEOUT_S)
    response = response_future.result()
    _validate_payload_type_and_length(response, messages_pb2.COMPRESSABLE, size)


def _blocking_unary(stub):
    size = 314159
    request = messages_pb2.SimpleRequest(
        response_type=messages_pb2.COMPRESSABLE,
        response_size=size,
        payload=messages_pb2.Payload(body=b"\x00" * 271828),
    )
    response = stub.UnaryCall(request, timeout=_RPC_TIMEOUT_S)
    _validate_payload_type_and_length(response, messages_pb2.COMPRESSABLE, size)


class _Pipe:
    def __init__(self):
        self._condition = threading.Condition()
        self._values = []
        self._open = True

    def __iter__(self):
        return self

    def __next__(self):
        return self.next()

    def next(self):
        with self._condition:
            while not self._values and self._open:
                self._condition.wait()
            if self._values:
                return self._values.pop(0)
            else:
                raise StopIteration()

    def add(self, value):
        with self._condition:
            self._values.append(value)
            self._condition.notify()

    def close(self):
        with self._condition:
            self._open = False
            self._condition.notify()

    def __enter__(self):
        return self

    def __exit__(self, type, value, traceback):
        self.close()


class _ChildProcess:
    def __init__(self, task, args=None):
        if args is None:
            args = ()
        self._exceptions = multiprocessing.Queue()
        self._stdout_path = tempfile.mkstemp()[1]
        self._stderr_path = tempfile.mkstemp()[1]
        self._child_pid = None
        self._rc = None
        self._args = args
        self._task = task

    def _child_main(self):
        import faulthandler

        faulthandler.enable(all_threads=True)

        try:
            self._task(*self._args)
        except grpc.RpcError as rpc_error:
            traceback.print_exc()
            self._exceptions.put("RpcError: %s" % rpc_error)
        except Exception as e:  # pylint: disable=broad-except
            traceback.print_exc()
            self._exceptions.put(e)
        sys.exit(0)

    def _orchestrate_child_gdb(self):
        cmd = [
            "gdb",
            "-ex",
            "set confirm off",
            "-ex",
            "attach {}".format(os.getpid()),
            "-ex",
            "set follow-fork-mode child",
            "-ex",
            "continue",
            "-ex",
            "bt",
        ]
        streams = tuple(tempfile.TemporaryFile() for _ in range(2))
        sys.stderr.write("Invoking gdb\n")
        sys.stderr.flush()
        process = subprocess.Popen(cmd, stdout=sys.stderr, stderr=sys.stderr)
        time.sleep(5)

    def start(self):
        # NOTE: Try uncommenting the following line if the child is segfaulting.
        # self._orchestrate_child_gdb()
        ret = os.fork()
        if ret == 0:
            self._child_main()
        else:
            self._child_pid = ret
            sys.stderr.write("Child %d started\n" % ret)

    def wait(self, timeout):
        total = 0.0
        wait_interval = 1.0
        next_lifebeat = 0
        while total < timeout:
            ret, termination = os.waitpid(self._child_pid, os.WNOHANG)
            if ret == self._child_pid:
                self._rc = termination
                return True
            time.sleep(wait_interval)
            total += wait_interval
            if total > next_lifebeat:
                sys.stderr.write(
                    "Child %d is still running\n" % self._child_pid
                )
                next_lifebeat = total + _LIFEBEAT_PERIOD_S
        else:
            return False

    def finish(self):
        terminated = self.wait(_CHILD_FINISH_TIMEOUT_S)

        if not terminated:
            sys.stderr.write("Finishing child %d\n" % self._child_pid)
            debugger.print_backtraces(self._child_pid)
            raise RuntimeError(
                "Child process %d did not terminate" % self._child_pid
            )

        waitstatus = self._rc
        exit_code = os.waitstatus_to_exitcode(waitstatus)
        sys.stderr.write(f"Exit code: {exit_code} ({waitstatus=})\n")

        if self._rc != 0:
            raise ValueError(
                f"Child process {self._child_pid} failed"
                f" with {exit_code=} ({waitstatus=})"
            )
        try:
            exception = self._exceptions.get(block=False)
            raise ValueError(
                'Child process %d failed: "%s": "%s"'
                % (self._child_pid, repr(exception), exception)
            )
        except queue.Empty:
            pass


def _async_unary_same_channel(channel):
    def child_target():
        try:
            _async_unary(stub)
        except grpc.RpcError as rpc_error:
            if rpc_error.code() not in (
                grpc.StatusCode.UNAVAILABLE,
                grpc.StatusCode.CANCELLED,
            ):
                raise ValueError("Unexpected status code") from rpc_error
            _async_unary(stub)

    stub = test_pb2_grpc.TestServiceStub(channel)
    _async_unary(stub)
    child_process = _ChildProcess(child_target)
    child_process.start()
    _async_unary(stub)
    child_process.finish()


def _async_unary_new_channel(channel, args):
    def child_target():
        with _channel(args) as child_channel:
            child_stub = test_pb2_grpc.TestServiceStub(child_channel)
            _async_unary(child_stub)

            child_channel.close()

    stub = test_pb2_grpc.TestServiceStub(channel)
    _async_unary(stub)
    child_process = _ChildProcess(child_target)
    child_process.start()
    _async_unary(stub)
    child_process.finish()


def _blocking_unary_same_channel(channel):
    def child_target():
        try:
            _blocking_unary(stub)
        except grpc.RpcError as rpc_error:
            if rpc_error.code() not in (
                grpc.StatusCode.UNAVAILABLE,
                grpc.StatusCode.CANCELLED,
            ):
                raise ValueError("Unexpected status code") from rpc_error
            _async_unary(stub)

    stub = test_pb2_grpc.TestServiceStub(channel)
    _blocking_unary(stub)
    child_process = _ChildProcess(child_target)
    child_process.start()
    child_process.finish()


def _blocking_unary_new_channel(channel, args):
    def child_target():
        with _channel(args) as child_channel:
            child_stub = test_pb2_grpc.TestServiceStub(child_channel)
            _blocking_unary(child_stub)

    stub = test_pb2_grpc.TestServiceStub(channel)
    _blocking_unary(stub)
    child_process = _ChildProcess(child_target)
    child_process.start()
    _blocking_unary(stub)
    child_process.finish()


# Verify that the fork channel registry can handle already closed channels
def _close_channel_before_fork(channel, args):
    def child_target():
        new_channel.close()
        with _channel(args) as child_channel:
            child_stub = test_pb2_grpc.TestServiceStub(child_channel)
            _blocking_unary(child_stub)

    stub = test_pb2_grpc.TestServiceStub(channel)
    _blocking_unary(stub)
    channel.close()

    with _channel(args) as new_channel:
        new_stub = test_pb2_grpc.TestServiceStub(new_channel)
        child_process = _ChildProcess(child_target)
        child_process.start()
        _blocking_unary(new_stub)
        child_process.finish()


def _connectivity_watch(channel, args):
    parent_states = []
    parent_channel_ready_event = threading.Event()

    def child_target():
        child_channel_ready_event = threading.Event()

        def child_connectivity_callback(state):
            if state is grpc.ChannelConnectivity.READY:
                child_channel_ready_event.set()

        with _channel(args) as child_channel:
            child_stub = test_pb2_grpc.TestServiceStub(child_channel)
            child_channel.subscribe(child_connectivity_callback)
            _async_unary(child_stub)
            _async_unary(stub)
            if not child_channel_ready_event.wait(timeout=_RPC_TIMEOUT_S):
                raise ValueError("Child channel did not move to READY")
            if not parent_channel_ready_event.wait(timeout=_RPC_TIMEOUT_S):
                raise ValueError("Parent channel did not move to READY")
            child_channel.unsubscribe(child_connectivity_callback)
            channel.unsubscribe(parent_connectivity_callback)

    def parent_connectivity_callback(state):
        parent_states.append(state)
        if state is grpc.ChannelConnectivity.READY:
            parent_channel_ready_event.set()

    channel.subscribe(parent_connectivity_callback)
    stub = test_pb2_grpc.TestServiceStub(channel)
    child_process = _ChildProcess(child_target)
    child_process.start()
    _async_unary(stub)
    if not parent_channel_ready_event.wait(timeout=_RPC_TIMEOUT_S):
        raise ValueError("Channel did not move to READY")
    channel.unsubscribe(parent_connectivity_callback)
    child_process.finish()


def _ping_pong_with_child_processes_after_first_response(
    channel, args, child_target, run_after_close=True
):
    request_response_sizes = (
        31415,
        9,
        2653,
        58979,
    )
    request_payload_sizes = (
        27182,
        8,
        1828,
        45904,
    )
    stub = test_pb2_grpc.TestServiceStub(channel)
    pipe = _Pipe()
    parent_bidi_call = stub.FullDuplexCall(pipe)
    child_processes = []
    first_message_received = False
    for response_size, payload_size in zip(
        request_response_sizes, request_payload_sizes
    ):
        request = messages_pb2.StreamingOutputCallRequest(
            response_type=messages_pb2.COMPRESSABLE,
            response_parameters=(
                messages_pb2.ResponseParameters(size=response_size),
            ),
            payload=messages_pb2.Payload(body=b"\x00" * payload_size),
        )
        pipe.add(request)
        if first_message_received:
            child_process = _ChildProcess(
                child_target, (parent_bidi_call, channel, args)
            )
            child_process.start()
            child_processes.append(child_process)
        response = next(parent_bidi_call)
        first_message_received = True
        child_process = _ChildProcess(
            child_target, (parent_bidi_call, channel, args)
        )
        child_process.start()
        child_processes.append(child_process)
        _validate_payload_type_and_length(
            response, messages_pb2.COMPRESSABLE, response_size
        )
    pipe.close()
    if run_after_close:
        child_process = _ChildProcess(
            child_target, (parent_bidi_call, channel, args)
        )
        child_process.start()
        child_processes.append(child_process)
    for child_process in child_processes:
        child_process.finish()


def _in_progress_bidi_continue_call(channel):
    def child_target(parent_bidi_call, parent_channel, args):
        stub = test_pb2_grpc.TestServiceStub(parent_channel)
        try:
            _async_unary(stub)
        except grpc.RpcError as rpc_error:
            if rpc_error.code() not in (
                grpc.StatusCode.UNAVAILABLE,
                grpc.StatusCode.CANCELLED,
            ):
                raise ValueError("Unexpected status code") from rpc_error
            _async_unary(stub)
        inherited_code = parent_bidi_call.code()
        if inherited_code != grpc.StatusCode.CANCELLED:
            raise ValueError(
                "Expected inherited code CANCELLED, "
                f"got {inherited_code} (details: {parent_bidi_call.details()})"
            )

    # Don't run child_target after closing the parent call, as the call may have
    # received a status from the  server before fork occurs.
    _ping_pong_with_child_processes_after_first_response(
        channel, None, child_target, run_after_close=False
    )


def _in_progress_bidi_same_channel_async_call(channel):
    def child_target(parent_bidi_call, parent_channel, args):
        stub = test_pb2_grpc.TestServiceStub(parent_channel)
        try:
            _async_unary(stub)
        except grpc.RpcError as rpc_error:
            if rpc_error.code() not in (
                grpc.StatusCode.UNAVAILABLE,
                grpc.StatusCode.CANCELLED,
            ):
                raise ValueError("Unexpected status code") from rpc_error
            _async_unary(stub)

    _ping_pong_with_child_processes_after_first_response(
        channel, None, child_target
    )


def _in_progress_bidi_same_channel_blocking_call(channel):
    def child_target(parent_bidi_call, parent_channel, args):
        stub = test_pb2_grpc.TestServiceStub(parent_channel)
        try:
            _blocking_unary(stub)
        except grpc.RpcError as rpc_error:
            if rpc_error.code() not in (
                grpc.StatusCode.UNAVAILABLE,
                grpc.StatusCode.CANCELLED,
            ):
                raise ValueError("Unexpected status code") from rpc_error
            _async_unary(stub)

    _ping_pong_with_child_processes_after_first_response(
        channel, None, child_target
    )


def _in_progress_bidi_new_channel_async_call(channel, args):
    def child_target(parent_bidi_call, parent_channel, args):
        with _channel(args) as channel:
            stub = test_pb2_grpc.TestServiceStub(channel)
            _async_unary(stub)

    _ping_pong_with_child_processes_after_first_response(
        channel, args, child_target
    )


def _in_progress_bidi_new_channel_blocking_call(channel, args):
    def child_target(parent_bidi_call, parent_channel, args):
        with _channel(args) as channel:
            stub = test_pb2_grpc.TestServiceStub(channel)
            _blocking_unary(stub)

    _ping_pong_with_child_processes_after_first_response(
        channel, args, child_target
    )


def _streaming_output_request(sizes, interval_us=0):
    return messages_pb2.StreamingOutputCallRequest(
        response_type=messages_pb2.COMPRESSABLE,
        response_parameters=[
            messages_pb2.ResponseParameters(size=size, interval_us=interval_us)
            for size in sizes
        ],
    )


def _client_stream_requests(sizes, interval_s=0):
    for size in sizes:
        time.sleep(interval_s)
        yield messages_pb2.StreamingInputCallRequest(
            payload=messages_pb2.Payload(body=b"\x00" * size)
        )


def _validate_streamed_sizes(responses, sizes):
    responses = list(responses)
    if len(responses) != len(sizes):
        raise ValueError(
            "expected %d responses, got %d" % (len(sizes), len(responses))
        )
    for response, size in zip(responses, sizes):
        _validate_payload_type_and_length(
            response, messages_pb2.COMPRESSABLE, size
        )


def _validate_aggregated_size(response, sizes):
    if response.aggregated_payload_size != sum(sizes):
        raise ValueError(
            "expected aggregated payload size %d, got %d"
            % (sum(sizes), response.aggregated_payload_size)
        )


def _all_rpc_types(stub):
    """Makes one call of each of the four RPC types, checking each response."""
    sizes = _CHILD_RPC_SIZES
    options = {"timeout": _RPC_TIMEOUT_S, "wait_for_ready": True}
    request = messages_pb2.SimpleRequest(
        response_type=messages_pb2.COMPRESSABLE, response_size=sizes[0]
    )
    _validate_payload_type_and_length(
        stub.UnaryCall(request, **options), messages_pb2.COMPRESSABLE, sizes[0]
    )
    _validate_streamed_sizes(
        stub.StreamingOutputCall(_streaming_output_request(sizes), **options),
        sizes,
    )
    _validate_aggregated_size(
        stub.StreamingInputCall(_client_stream_requests(sizes), **options),
        sizes,
    )
    _validate_streamed_sizes(
        stub.FullDuplexCall(
            iter([_streaming_output_request((size,)) for size in sizes]),
            **options,
        ),
        sizes,
    )


class _InProgressCalls:
    """Keeps calls of all four RPC types in flight from background threads.

    Every response is checked. Blocking unary, blocking client-streaming and
    single-threaded server-streaming calls are segregated: each completes on
    its own completion queue, polled by the calling thread. The others are
    integrated: they complete on the channel's shared completion queue, polled
    by a gRPC thread.

    Fork support requires that no other application thread is calling into
    gRPC when fork() is called. So pause() makes each background thread wait
    at its next safe point, between calls or between the messages of a
    stream, until resume(). The long-lived streams stay in flight meanwhile.
    """

    def __init__(self, channel, single_threaded_channel):
        self._stub = test_pb2_grpc.TestServiceStub(channel)
        self._single_threaded_stub = test_pb2_grpc.TestServiceStub(
            single_threaded_channel
        )
        self._calls = {
            "unary": self._unary,
            "unary_future": self._unary_future,
            "server_stream": self._server_stream,
            "server_stream_single_threaded": (
                self._server_stream_single_threaded
            ),
            "client_stream": self._client_stream,
            "client_stream_future": self._client_stream_future,
            "bidi": self._bidi,
            "bidi_short": self._bidi_short,
        }
        self._stop = threading.Event()
        self._errors = []
        self._checked = collections.Counter({name: 0 for name in self._calls})
        self._threads = {}
        self.bidi_call = None  # The long-lived bidi call, set by start().
        self._gate = threading.Condition()  # Guards the three fields below.
        self._pause_requested = False
        self._paused = 0  # Threads waiting at a safe point.
        self._running = len(self._calls)  # Threads that have not finished.

    def start(self):
        """Starts the calls; returns once each has checked a response."""
        ready = {name: threading.Event() for name in self._calls}
        for name, call in self._calls.items():
            self._threads[name] = threading.Thread(
                target=self._run, args=(name, call, ready[name]), daemon=True
            )
            self._threads[name].start()
        for name, event in ready.items():
            if not event.wait(_RPC_TIMEOUT_S):
                raise ValueError(
                    "%s got no response: %s" % (name, self._errors)
                )
        self.raise_if_failed()

    def raise_if_failed(self):
        if self._errors:
            raise ValueError("Parent call failed: %s" % self._errors[0])

    def pause(self):
        """Waits until every background thread waits at a safe point.

        Returns the responses checked so far, per call.
        """
        with self._gate:
            self._pause_requested = True
            paused = self._gate.wait_for(
                lambda: self._paused == self._running, _RPC_TIMEOUT_S
            )
        self.raise_if_failed()
        if not paused:
            raise ValueError("Background calls did not pause")
        return collections.Counter(self._checked)

    def resume(self):
        with self._gate:
            self._pause_requested = False
            self._gate.notify_all()

    def stop(self, checked_before_fork):
        """Stops the calls. Each must have checked responses after the fork."""
        self._stop.set()
        for name, thread in self._threads.items():
            thread.join(_RPC_TIMEOUT_S)
            if thread.is_alive():
                raise ValueError("%s did not finish" % name)
        self.raise_if_failed()
        stalled = [
            name
            for name in self._calls
            if self._checked[name] <= checked_before_fork[name]
        ]
        if stalled:
            raise ValueError("No responses after the fork for %s" % stalled)

    def _run(self, name, call, ready):
        def checked():
            self._checked[name] += 1
            ready.set()

        try:
            call(checked)
        except Exception as e:  # pylint: disable=broad-except
            self._errors.append("%s: %r" % (name, e))
        with self._gate:  # So that pause() does not wait for this thread.
            self._running -= 1
            self._gate.notify_all()

    def _safe_point(self):
        """Waits here, outside of gRPC, while a pause is requested."""
        with self._gate:
            if self._pause_requested:
                self._paused += 1
                self._gate.notify_all()
                self._gate.wait_for(lambda: not self._pause_requested)
                self._paused -= 1

    def _unary(self, checked):
        request = messages_pb2.SimpleRequest(
            response_type=messages_pb2.COMPRESSABLE, response_size=1
        )
        while not self._stop.is_set():
            self._safe_point()
            response = self._stub.UnaryCall(request, timeout=_RPC_TIMEOUT_S)
            _validate_payload_type_and_length(
                response, messages_pb2.COMPRESSABLE, 1
            )
            checked()

    def _unary_future(self, checked):
        request = messages_pb2.SimpleRequest(
            response_type=messages_pb2.COMPRESSABLE, response_size=1
        )
        while not self._stop.is_set():
            self._safe_point()
            future = self._stub.UnaryCall.future(
                request, timeout=_RPC_TIMEOUT_S
            )
            _validate_payload_type_and_length(
                future.result(), messages_pb2.COMPRESSABLE, 1
            )
            checked()

    def _server_stream(self, checked):
        """One long-lived server stream; its messages must arrive in order."""
        sizes = [n % 64 + 1 for n in range(_LONG_STREAM_MESSAGES)]
        responses = self._stub.StreamingOutputCall(
            _streaming_output_request(sizes, _STREAM_INTERVAL_US)
        )
        for size, response in zip(sizes, responses):
            _validate_payload_type_and_length(
                response, messages_pb2.COMPRESSABLE, size
            )
            checked()
            if self._stop.is_set():
                responses.cancel()  # It would not end on its own.
                return
            self._safe_point()

    def _server_stream_single_threaded(self, checked):
        while not self._stop.is_set():
            responses = self._single_threaded_stub.StreamingOutputCall(
                _streaming_output_request(
                    _CHILD_RPC_SIZES, _STREAM_INTERVAL_US
                ),
                timeout=_RPC_TIMEOUT_S,
            )
            received = []
            for response in responses:
                received.append(response)
                self._safe_point()  # The call is still in flight here.
            _validate_streamed_sizes(received, _CHILD_RPC_SIZES)
            checked()

    def _client_stream(self, checked):
        while not self._stop.is_set():
            self._safe_point()
            response = self._stub.StreamingInputCall(
                _client_stream_requests(
                    _CHILD_RPC_SIZES, _STREAM_INTERVAL_US / 1e6
                ),
                timeout=_RPC_TIMEOUT_S,
            )
            _validate_aggregated_size(response, _CHILD_RPC_SIZES)
            checked()

    def _client_stream_future(self, checked):
        while not self._stop.is_set():
            self._safe_point()
            future = self._stub.StreamingInputCall.future(
                _client_stream_requests(
                    _CHILD_RPC_SIZES, _STREAM_INTERVAL_US / 1e6
                ),
                timeout=_RPC_TIMEOUT_S,
            )
            _validate_aggregated_size(future.result(), _CHILD_RPC_SIZES)
            checked()

    def _bidi(self, checked):
        """One long-lived ping-pong stream: send a request, check its reply."""
        pipe = _Pipe()
        sent = 0
        pipe.add(_streaming_output_request((1,), _STREAM_INTERVAL_US))
        self.bidi_call = self._stub.FullDuplexCall(pipe)
        for response in self.bidi_call:
            _validate_payload_type_and_length(
                response, messages_pb2.COMPRESSABLE, sent % 64 + 1
            )
            checked()
            if self._stop.is_set():
                pipe.close()  # Half-close: the stream then ends with OK.
            else:
                # Before the next request, so no message is sent while paused.
                self._safe_point()
                sent += 1
                pipe.add(
                    _streaming_output_request(
                        (sent % 64 + 1,), _STREAM_INTERVAL_US
                    )
                )

    def _bidi_short(self, checked):
        while not self._stop.is_set():
            self._safe_point()
            requests = [
                _streaming_output_request((size,), _STREAM_INTERVAL_US)
                for size in _CHILD_RPC_SIZES
            ]
            responses = self._stub.FullDuplexCall(
                iter(requests), timeout=_RPC_TIMEOUT_S
            )
            _validate_streamed_sizes(responses, _CHILD_RPC_SIZES)
            checked()


def _in_progress_all_rpc_types(channel, args, child_rpcs):
    """Forks a child while calls of all four RPC types are in flight.

    Background threads keep calls of every kind in flight. This thread pauses
    them, forks a child, and resumes them while the child runs. The child
    checks that gRPC cancelled the long-lived bidi call it inherited, then
    runs child_rpcs().
    """
    single_threaded_channel = _channel(
        args,
        options=(
            (grpc.experimental.ChannelOptions.SingleThreadedUnaryStream, 1),
        ),
    )
    calls = _InProgressCalls(channel, single_threaded_channel)

    def child_target():
        # gRPC must end the calls the child inherited: cancelled by its
        # post-fork handler, or failed first when the child's I/O was reset.
        inherited_code = calls.bidi_call.code()
        if inherited_code not in (
            grpc.StatusCode.CANCELLED,
            grpc.StatusCode.UNAVAILABLE,
        ):
            raise ValueError(
                "Expected inherited code CANCELLED or UNAVAILABLE, got %s"
                % inherited_code
            )
        child_rpcs()

    calls.start()
    child_process = _ChildProcess(child_target)
    checked_before_fork = calls.pause()
    child_process.start()
    calls.resume()
    child_process.finish()
    calls.stop(checked_before_fork)
    single_threaded_channel.close()


def _in_progress_all_rpc_types_same_channel(channel, args):
    def child_rpcs():
        try:
            _all_rpc_types(stub)
        except grpc.RpcError as rpc_error:
            if rpc_error.code() not in (
                grpc.StatusCode.UNAVAILABLE,
                grpc.StatusCode.CANCELLED,
            ):
                raise ValueError("Unexpected status code") from rpc_error
            _all_rpc_types(stub)

    stub = test_pb2_grpc.TestServiceStub(channel)
    _in_progress_all_rpc_types(channel, args, child_rpcs)


def _in_progress_all_rpc_types_new_channel(channel, args):
    def child_rpcs():
        with _channel(args) as child_channel:
            _all_rpc_types(test_pb2_grpc.TestServiceStub(child_channel))

    _in_progress_all_rpc_types(channel, args, child_rpcs)


@enum.unique
class TestCase(enum.Enum):
    CONNECTIVITY_WATCH = "connectivity_watch"
    CLOSE_CHANNEL_BEFORE_FORK = "close_channel_before_fork"
    ASYNC_UNARY_SAME_CHANNEL = "async_unary_same_channel"
    ASYNC_UNARY_NEW_CHANNEL = "async_unary_new_channel"
    BLOCKING_UNARY_SAME_CHANNEL = "blocking_unary_same_channel"
    BLOCKING_UNARY_NEW_CHANNEL = "blocking_unary_new_channel"
    IN_PROGRESS_BIDI_CONTINUE_CALL = "in_progress_bidi_continue_call"
    IN_PROGRESS_BIDI_SAME_CHANNEL_ASYNC_CALL = (
        "in_progress_bidi_same_channel_async_call"
    )
    IN_PROGRESS_BIDI_SAME_CHANNEL_BLOCKING_CALL = (
        "in_progress_bidi_same_channel_blocking_call"
    )
    IN_PROGRESS_BIDI_NEW_CHANNEL_ASYNC_CALL = (
        "in_progress_bidi_new_channel_async_call"
    )
    IN_PROGRESS_BIDI_NEW_CHANNEL_BLOCKING_CALL = (
        "in_progress_bidi_new_channel_blocking_call"
    )
    IN_PROGRESS_ALL_RPC_TYPES_SAME_CHANNEL = (
        "in_progress_all_rpc_types_same_channel"
    )
    IN_PROGRESS_ALL_RPC_TYPES_NEW_CHANNEL = (
        "in_progress_all_rpc_types_new_channel"
    )

    def run_test(self, args):
        _LOGGER.info("Running %s", self)
        channel = _channel(args)
        if self is TestCase.ASYNC_UNARY_SAME_CHANNEL:
            _async_unary_same_channel(channel)
        elif self is TestCase.ASYNC_UNARY_NEW_CHANNEL:
            _async_unary_new_channel(channel, args)
        elif self is TestCase.BLOCKING_UNARY_SAME_CHANNEL:
            _blocking_unary_same_channel(channel)
        elif self is TestCase.BLOCKING_UNARY_NEW_CHANNEL:
            _blocking_unary_new_channel(channel, args)
        elif self is TestCase.CLOSE_CHANNEL_BEFORE_FORK:
            _close_channel_before_fork(channel, args)
        elif self is TestCase.CONNECTIVITY_WATCH:
            _connectivity_watch(channel, args)
        elif self is TestCase.IN_PROGRESS_BIDI_CONTINUE_CALL:
            _in_progress_bidi_continue_call(channel)
        elif self is TestCase.IN_PROGRESS_BIDI_SAME_CHANNEL_ASYNC_CALL:
            _in_progress_bidi_same_channel_async_call(channel)
        elif self is TestCase.IN_PROGRESS_BIDI_SAME_CHANNEL_BLOCKING_CALL:
            _in_progress_bidi_same_channel_blocking_call(channel)
        elif self is TestCase.IN_PROGRESS_BIDI_NEW_CHANNEL_ASYNC_CALL:
            _in_progress_bidi_new_channel_async_call(channel, args)
        elif self is TestCase.IN_PROGRESS_BIDI_NEW_CHANNEL_BLOCKING_CALL:
            _in_progress_bidi_new_channel_blocking_call(channel, args)
        elif self is TestCase.IN_PROGRESS_ALL_RPC_TYPES_SAME_CHANNEL:
            _in_progress_all_rpc_types_same_channel(channel, args)
        elif self is TestCase.IN_PROGRESS_ALL_RPC_TYPES_NEW_CHANNEL:
            _in_progress_all_rpc_types_new_channel(channel, args)
        else:
            raise NotImplementedError(
                'Test case "%s" not implemented!' % self.name
            )
        channel.close()


# Useful if needing to find a block of code from an address in an SO.
def dump_object_map():
    with open("/proc/self/maps", "r") as f:
        sys.stderr.write("=============== /proc/self/maps ===============\n")
        sys.stderr.write(f.read())
        sys.stderr.write("\n")
        sys.stderr.flush()


def setup_logger():
    logging.basicConfig(
        level=logging.INFO,
        style="{",
        format=(
            "{levelname[0]}{asctime}.{msecs:03.0f} {process}/{thread} "
            "{filename}:{lineno}] {message}"
        ),
        datefmt="%m%d %H:%M:%S",
    )

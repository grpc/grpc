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

from src.proto.grpc.testing import empty_pb2
from src.proto.grpc.testing import messages_pb2
from src.proto.grpc.testing import test_pb2_grpc

_LOGGER = logging.getLogger(__name__)
_RPC_TIMEOUT_S = 10
_CHILD_FINISH_TIMEOUT_S = 20
_GDB_TIMEOUT_S = 60


def _channel(args):
    target = f"{args['server_host']}:{args['server_port']}"
    if args["use_tls"]:
        channel_credentials = grpc.ssl_channel_credentials()
        channel = grpc.secure_channel(target, channel_credentials)
    else:
        channel = grpc.insecure_channel(target)
    return channel


def _validate_payload_type_and_length(response, expected_type, expected_length):
    if response.payload.type is not expected_type:
        raise ValueError(
            f"expected payload type {expected_type}, got {type(response.payload.type)}"
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


class _Pipe(object):
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


class _ChildProcess(object):
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
            self._exceptions.put(f"RpcError: {rpc_error}")
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
            f"attach {os.getpid()}",
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

    def wait(self, timeout):
        total = 0.0
        wait_interval = 1.0
        while total < timeout:
            ret, termination = os.waitpid(self._child_pid, os.WNOHANG)
            if ret == self._child_pid:
                self._rc = termination
                return True
            time.sleep(wait_interval)
            total += wait_interval
        else:
            return False

    def _print_backtraces(self):
        cmd = [
            "gdb",
            "-ex",
            "set confirm off",
            "-ex",
            "echo attaching",
            "-ex",
            f"attach {self._child_pid}",
            "-ex",
            "echo print_backtrace",
            "-ex",
            "thread apply all bt",
            "-ex",
            "echo printed_backtrace",
            "-ex",
            "quit",
        ]
        streams = tuple(tempfile.TemporaryFile() for _ in range(2))
        sys.stderr.write("Invoking gdb\n")
        sys.stderr.flush()
        process = subprocess.Popen(cmd, stdout=streams[0], stderr=streams[1])
        try:
            process.wait(timeout=_GDB_TIMEOUT_S)
        except subprocess.TimeoutExpired:
            sys.stderr.write("gdb stacktrace generation timed out.\n")
        finally:
            for stream_name, stream in zip(("STDOUT", "STDERR"), streams):
                stream.seek(0)
                sys.stderr.write(
                    f"gdb {stream_name}:\n{stream.read().decode('ascii')}\n"
                )
                stream.close()
            sys.stderr.flush()

    def finish(self):
        terminated = self.wait(_CHILD_FINISH_TIMEOUT_S)
        sys.stderr.write(f"Exit code: {self._rc}\n")
        if not terminated:
            self._print_backtraces()
            raise RuntimeError("Child process did not terminate")
        if self._rc != 0:
            raise ValueError("Child process failed with exitcode %d" % self._rc)
        try:
            exception = self._exceptions.get(block=False)
            raise ValueError(
                f'Child process failed: "{repr(exception)}": "{exception}"'
            )
        except queue.Empty:
            pass


def _async_unary_same_channel(channel):
    def child_target():
        try:
            _async_unary(stub)
            raise Exception(
                "Child should not be able to re-use channel after fork"
            )
        except ValueError as expected_value_error:
            pass

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
            raise Exception(
                "Child should not be able to re-use channel after fork"
            )
        except ValueError as expected_value_error:
            pass

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
            if not child_channel_ready_event.wait(timeout=_RPC_TIMEOUT_S):
                raise ValueError("Channel did not move to READY")
            if len(parent_states) > 1:
                raise ValueError(
                    "Received connectivity updates on parent callback",
                    parent_states,
                )
            child_channel.unsubscribe(child_connectivity_callback)

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
            raise Exception(
                "Child should not be able to re-use channel after fork"
            )
        except ValueError as expected_value_error:
            pass
        inherited_code = parent_bidi_call.code()
        inherited_details = parent_bidi_call.details()
        if inherited_code != grpc.StatusCode.CANCELLED:
            raise ValueError(
                f"Expected inherited code CANCELLED, got {inherited_code}"
            )
        if inherited_details != "Channel closed due to fork":
            raise ValueError(
                "Expected inherited details Channel closed due to fork, got %s"
                % inherited_details
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
            raise Exception(
                "Child should not be able to re-use channel after fork"
            )
        except ValueError as expected_value_error:
            pass

    _ping_pong_with_child_processes_after_first_response(
        channel, None, child_target
    )


def _in_progress_bidi_same_channel_blocking_call(channel):
    def child_target(parent_bidi_call, parent_channel, args):
        stub = test_pb2_grpc.TestServiceStub(parent_channel)
        try:
            _blocking_unary(stub)
            raise Exception(
                "Child should not be able to re-use channel after fork"
            )
        except ValueError as expected_value_error:
            pass

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
        else:
            raise NotImplementedError(
                f'Test case "{self.name}" not implemented!'
            )
        channel.close()


# Useful if needing to find a block of code from an address in an SO.
def dump_object_map():
    with open("/proc/self/maps", "r") as f:
        sys.stderr.write("=============== /proc/self/maps ===============\n")
        sys.stderr.write(f.read())
        sys.stderr.write("\n")
        sys.stderr.flush()

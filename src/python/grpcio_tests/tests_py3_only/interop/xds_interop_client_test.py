import os
import sys
import subprocess
import tempfile
import contextlib

import xds_interop_client
import xds_interop_server

import grpc.experimental

from src.proto.grpc.testing import test_pb2
from src.proto.grpc.testing import test_pb2_grpc
from src.proto.grpc.testing import messages_pb2

from typing import List, Tuple

_CLIENT_PATH = os.path.abspath(os.path.realpath(xds_interop_client.__file__))
_SERVER_PATH = os.path.abspath(os.path.realpath(xds_interop_server.__file__))

_METHODS = (
    (messages_pb2.ClientConfigureRequest.UNARY_CALL, "UNARY_CALL"),
    (messages_pb2.ClientConfigureRequest.EMPTY_CALL, "EMPTY_CALL"),
)


_QPS = 100
_NUM_CHANNELS = 2

@contextlib.contextmanager
def _start_python_with_args(file: str, args: List[str]) -> Tuple[subprocess.Popen, tempfile.TemporaryFile, tempfile.TemporaryFile]:
    with tempfile.TemporaryFile(mode='r') as stdout:
        with tempfile.TemporaryFile(mode='r') as stderr:
            # TODO: Allocate a non-static port for it.
            proc = subprocess.Popen((sys.executable, file) + tuple(args), stdout=stdout, stderr=stderr)
            yield proc, stdout, stderr


def _dump_stream(process_name: str, stream_name: str, stream: tempfile.TemporaryFile):
    sys.stderr.write(f"{process_name} {stream_name}:\n")
    stream.seek(0)
    sys.stderr.write(stream.read())


def _dump_streams(process_name: str, stdout: tempfile.TemporaryFile, stderr: tempfile.TemporaryFile):
    _dump_stream(process_name, "stdout", stdout)
    _dump_stream(process_name, "stderr", stderr)


def _test_client(qps: int, num_channels: int):
    duration = 2
    settings = {
            "target": "localhost:8081",
            "insecure": True,
    }
    for i in range(10):
        target_method, target_method_str = _METHODS[i % len(_METHODS)]
        test_pb2_grpc.XdsUpdateClientConfigureService.Configure(
                messages_pb2.ClientConfigureRequest(types=[target_method]),
                **settings)
        import time; time.sleep(2)
        response = test_pb2_grpc.LoadBalancerStatsService.GetClientAccumulatedStats(
                messages_pb2.LoadBalancerAccumulatedStatsRequest(),
                **settings)
        before = {}
        for _, method_str in _METHODS:
            before[method_str] = response.stats_per_method[method_str].result[0]
        time.sleep(duration)
        response = test_pb2_grpc.LoadBalancerStatsService.GetClientAccumulatedStats(
                messages_pb2.LoadBalancerAccumulatedStatsRequest(),
                **settings)
        after = {}
        delta = {}
        for _, method_str in _METHODS:
            after[method_str] = response.stats_per_method[method_str].result[0]
            delta[method_str] = after[method_str] - before[method_str]
        sys.stderr.write("Delta: {}\n".format(delta))
        for _, method_str in _METHODS:
            if method_str == target_method_str:
                assert delta[method_str] == qps * duration * num_channels
            else:
                assert delta[method_str] == 0



# TODO: Allocate a non-static port for it.
with _start_python_with_args(_SERVER_PATH, ['--port=8080']) as (server, server_stdout, server_stderr):
    with _start_python_with_args(_CLIENT_PATH, ["--server=localhost:8080", "--stats_port=8081", f"--qps={_QPS}", f"--num_channels={_NUM_CHANNELS}"]) as (client, client_stdout, client_stderr):
        try:
            _test_client(_QPS, _NUM_CHANNELS)
        finally:
            pass
            # _dump_streams("server", server_stdout, server_stderr)
            # _dump_streams("client", client_stdout, client_stderr)


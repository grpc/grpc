import os
import sys
import subprocess
import tempfile
import contextlib
import time
import unittest

import xds_interop_client
import xds_interop_server

import grpc.experimental

from src.proto.grpc.testing import test_pb2
from src.proto.grpc.testing import empty_pb2
from src.proto.grpc.testing import test_pb2_grpc
from src.proto.grpc.testing import messages_pb2

import src.python.grpcio_tests.tests.unit.framework.common as framework_common

from typing import List, Tuple

import logging

_CLIENT_PATH = os.path.abspath(os.path.realpath(xds_interop_client.__file__))
_SERVER_PATH = os.path.abspath(os.path.realpath(xds_interop_server.__file__))

_METHODS = (
    (messages_pb2.ClientConfigureRequest.UNARY_CALL, "UNARY_CALL"),
    (messages_pb2.ClientConfigureRequest.EMPTY_CALL, "EMPTY_CALL"),
)

_QPS = 100
_NUM_CHANNELS = 20

_TEST_ITERATIONS = 10
_ITERATION_DURATION_SECONDS = 1
_SUBPROCESS_TIMEOUT_SECONDS = 2


@contextlib.contextmanager
def _start_python_with_args(
    file: str, args: List[str]
) -> Tuple[subprocess.Popen, tempfile.TemporaryFile, tempfile.TemporaryFile]:
    with tempfile.TemporaryFile(mode='r') as stdout:
        with tempfile.TemporaryFile(mode='r') as stderr:
            proc = subprocess.Popen((sys.executable, file) + tuple(args),
                                    stdout=stdout,
                                    stderr=stderr)
            yield proc, stdout, stderr


def _dump_stream(process_name: str, stream_name: str,
                 stream: tempfile.TemporaryFile):
    sys.stderr.write(f"{process_name} {stream_name}:\n")
    stream.seek(0)
    sys.stderr.write(stream.read())


def _dump_streams(process_name: str, stdout: tempfile.TemporaryFile,
                  stderr: tempfile.TemporaryFile):
    _dump_stream(process_name, "stdout", stdout)
    _dump_stream(process_name, "stderr", stderr)
    sys.stderr.write(f"End {process_name} output.\n")


def _test_client(server_port: int, stats_port: int, qps: int,
                 num_channels: int):
    # Send RPC to server to make sure it's running.
    test_pb2_grpc.TestService.EmptyCall(empty_pb2.Empty(),
                                        f"localhost:{server_port}",
                                        insecure=True,
                                        wait_for_ready=True)
    logging.info("Successfully sent RPC to server.")
    settings = {
        "target": f"localhost:{stats_port}",
        "insecure": True,
    }
    for i in range(_TEST_ITERATIONS):
        target_method, target_method_str = _METHODS[i % len(_METHODS)]
        test_pb2_grpc.XdsUpdateClientConfigureService.Configure(
            messages_pb2.ClientConfigureRequest(types=[target_method]),
            **settings)
        response = test_pb2_grpc.LoadBalancerStatsService.GetClientAccumulatedStats(
            messages_pb2.LoadBalancerAccumulatedStatsRequest(), **settings)
        before = {}
        for _, method_str in _METHODS:
            before[method_str] = response.stats_per_method[method_str].result[0]
        time.sleep(_ITERATION_DURATION_SECONDS)
        response = test_pb2_grpc.LoadBalancerStatsService.GetClientAccumulatedStats(
            messages_pb2.LoadBalancerAccumulatedStatsRequest(), **settings)
        after = {}
        delta = {}
        for _, method_str in _METHODS:
            after[method_str] = response.stats_per_method[method_str].result[0]
            delta[method_str] = after[method_str] - before[method_str]
        logging.info("Delta: %s", delta)
        for _, method_str in _METHODS:
            if method_str == target_method_str:
                assert delta[method_str] > 0
            else:
                assert delta[method_str] == 0


class XdsInteropClientTest(unittest.TestCase):

    def test_configure_consistenc(self):
        _, server_port, socket = framework_common.get_socket()

        with _start_python_with_args(
                _SERVER_PATH,
            [f"--port={server_port}", f"--maintenance_port={server_port}"
            ]) as (server, server_stdout, server_stderr):
            socket.close()
            _, stats_port, stats_socket = framework_common.get_socket()
            with _start_python_with_args(_CLIENT_PATH, [
                    f"--server=localhost:{server_port}",
                    f"--stats_port={stats_port}", f"--qps={_QPS}",
                    f"--num_channels={_NUM_CHANNELS}"
            ]) as (client, client_stdout, client_stderr):
                stats_socket.close()
                try:
                    _test_client(server_port, stats_port, _QPS, _NUM_CHANNELS)
                except:
                    _dump_streams("server", server_stdout, server_stderr)
                    _dump_streams("client", client_stdout, client_stderr)
                    raise
                finally:
                    server.kill()
                    client.kill()
                    server.wait(timeout=_SUBPROCESS_TIMEOUT_SECONDS)
                    client.wait(timeout=_SUBPROCESS_TIMEOUT_SECONDS)


if __name__ == '__main__':
    logging.basicConfig()
    unittest.main(verbosity=2)

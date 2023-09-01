# Copyright 2022 gRPC authors.
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

import collections
import contextlib
import logging
import os
import subprocess
import sys
import tempfile
import time
from typing import Iterable, List, Mapping, Set, Tuple
import unittest

import grpc.experimental
import xds_interop_client
import xds_interop_server

from src.proto.grpc.testing import empty_pb2
from src.proto.grpc.testing import messages_pb2
from src.proto.grpc.testing import test_pb2
from src.proto.grpc.testing import test_pb2_grpc
import src.python.grpcio_tests.tests.unit.framework.common as framework_common

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


def _set_union(a: Iterable, b: Iterable) -> Set:
    c = set(a)
    c.update(b)
    return c


@contextlib.contextmanager
def _start_python_with_args(
    file: str, args: List[str]
) -> Tuple[subprocess.Popen, tempfile.TemporaryFile, tempfile.TemporaryFile]:
    with tempfile.TemporaryFile(mode="r") as stdout:
        with tempfile.TemporaryFile(mode="r") as stderr:
            proc = subprocess.Popen(
                (sys.executable, file) + tuple(args),
                stdout=stdout,
                stderr=stderr,
            )
            yield proc, stdout, stderr


def _dump_stream(
    process_name: str, stream_name: str, stream: tempfile.TemporaryFile
):
    sys.stderr.write(f"{process_name} {stream_name}:\n")
    stream.seek(0)
    sys.stderr.write(stream.read())


def _dump_streams(
    process_name: str,
    stdout: tempfile.TemporaryFile,
    stderr: tempfile.TemporaryFile,
):
    _dump_stream(process_name, "stdout", stdout)
    _dump_stream(process_name, "stderr", stderr)
    sys.stderr.write(f"End {process_name} output.\n")


def _index_accumulated_stats(
    response: messages_pb2.LoadBalancerAccumulatedStatsResponse,
) -> Mapping[str, Mapping[int, int]]:
    indexed = collections.defaultdict(lambda: collections.defaultdict(int))
    for _, method_str in _METHODS:
        for status in response.stats_per_method[method_str].result.keys():
            indexed[method_str][status] = response.stats_per_method[
                method_str
            ].result[status]
    return indexed


def _subtract_indexed_stats(
    a: Mapping[str, Mapping[int, int]], b: Mapping[str, Mapping[int, int]]
):
    c = collections.defaultdict(lambda: collections.defaultdict(int))
    all_methods = _set_union(a.keys(), b.keys())
    for method in all_methods:
        all_statuses = _set_union(a[method].keys(), b[method].keys())
        for status in all_statuses:
            c[method][status] = a[method][status] - b[method][status]
    return c


def _collect_stats(
    stats_port: int, duration: int
) -> Mapping[str, Mapping[int, int]]:
    settings = {
        "target": f"localhost:{stats_port}",
        "insecure": True,
    }
    response = test_pb2_grpc.LoadBalancerStatsService.GetClientAccumulatedStats(
        messages_pb2.LoadBalancerAccumulatedStatsRequest(), **settings
    )
    before = _index_accumulated_stats(response)
    time.sleep(duration)
    response = test_pb2_grpc.LoadBalancerStatsService.GetClientAccumulatedStats(
        messages_pb2.LoadBalancerAccumulatedStatsRequest(), **settings
    )
    after = _index_accumulated_stats(response)
    return _subtract_indexed_stats(after, before)


class XdsInteropClientTest(unittest.TestCase):
    def _assert_client_consistent(
        self, server_port: int, stats_port: int, qps: int, num_channels: int
    ):
        settings = {
            "target": f"localhost:{stats_port}",
            "insecure": True,
        }
        for i in range(_TEST_ITERATIONS):
            target_method, target_method_str = _METHODS[i % len(_METHODS)]
            test_pb2_grpc.XdsUpdateClientConfigureService.Configure(
                messages_pb2.ClientConfigureRequest(types=[target_method]),
                **settings,
            )
            delta = _collect_stats(stats_port, _ITERATION_DURATION_SECONDS)
            logging.info("Delta: %s", delta)
            for _, method_str in _METHODS:
                for status in delta[method_str]:
                    if status == 0 and method_str == target_method_str:
                        self.assertGreater(delta[method_str][status], 0, delta)
                    else:
                        self.assertEqual(delta[method_str][status], 0, delta)

    def test_configure_consistency(self):
        _, server_port, socket = framework_common.get_socket()

        with _start_python_with_args(
            _SERVER_PATH,
            [f"--port={server_port}", f"--maintenance_port={server_port}"],
        ) as (server, server_stdout, server_stderr):
            # Send RPC to server to make sure it's running.
            logging.info("Sending RPC to server.")
            test_pb2_grpc.TestService.EmptyCall(
                empty_pb2.Empty(),
                f"localhost:{server_port}",
                insecure=True,
                wait_for_ready=True,
            )
            logging.info("Server successfully started.")
            socket.close()
            _, stats_port, stats_socket = framework_common.get_socket()
            with _start_python_with_args(
                _CLIENT_PATH,
                [
                    f"--server=localhost:{server_port}",
                    f"--stats_port={stats_port}",
                    f"--qps={_QPS}",
                    f"--num_channels={_NUM_CHANNELS}",
                ],
            ) as (client, client_stdout, client_stderr):
                stats_socket.close()
                try:
                    self._assert_client_consistent(
                        server_port, stats_port, _QPS, _NUM_CHANNELS
                    )
                except:
                    _dump_streams("server", server_stdout, server_stderr)
                    _dump_streams("client", client_stdout, client_stderr)
                    raise
                finally:
                    server.kill()
                    client.kill()
                    server.wait(timeout=_SUBPROCESS_TIMEOUT_SECONDS)
                    client.wait(timeout=_SUBPROCESS_TIMEOUT_SECONDS)


if __name__ == "__main__":
    logging.basicConfig()
    unittest.main(verbosity=2)

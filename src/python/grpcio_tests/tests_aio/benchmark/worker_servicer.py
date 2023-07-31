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
import collections
import logging
import multiprocessing
import os
import sys
import time
from typing import Tuple

import grpc
from grpc.experimental import aio

from src.proto.grpc.testing import benchmark_service_pb2_grpc
from src.proto.grpc.testing import control_pb2
from src.proto.grpc.testing import stats_pb2
from src.proto.grpc.testing import worker_service_pb2_grpc
from tests.qps import histogram
from tests.unit import resources
from tests.unit.framework.common import get_socket
from tests_aio.benchmark import benchmark_client
from tests_aio.benchmark import benchmark_servicer

_NUM_CORES = multiprocessing.cpu_count()
_WORKER_ENTRY_FILE = os.path.join(
    os.path.split(os.path.abspath(__file__))[0], "worker.py"
)

_LOGGER = logging.getLogger(__name__)


class _SubWorker(
    collections.namedtuple("_SubWorker", ["process", "port", "channel", "stub"])
):
    """A data class that holds information about a child qps worker."""

    def _repr(self):
        return f"<_SubWorker pid={self.process.pid} port={self.port}>"

    def __repr__(self):
        return self._repr()

    def __str__(self):
        return self._repr()


def _get_server_status(
    start_time: float, end_time: float, port: int
) -> control_pb2.ServerStatus:
    """Creates ServerStatus proto message."""
    end_time = time.monotonic()
    elapsed_time = end_time - start_time
    # TODO(lidiz) Collect accurate time system to compute QPS/core-second.
    stats = stats_pb2.ServerStats(
        time_elapsed=elapsed_time,
        time_user=elapsed_time,
        time_system=elapsed_time,
    )
    return control_pb2.ServerStatus(stats=stats, port=port, cores=_NUM_CORES)


def _create_server(config: control_pb2.ServerConfig) -> Tuple[aio.Server, int]:
    """Creates a server object according to the ServerConfig."""
    channel_args = tuple(
        (arg.name, arg.str_value)
        if arg.HasField("str_value")
        else (arg.name, int(arg.int_value))
        for arg in config.channel_args
    )

    server = aio.server(options=channel_args + (("grpc.so_reuseport", 1),))
    if config.server_type == control_pb2.ASYNC_SERVER:
        servicer = benchmark_servicer.BenchmarkServicer()
        benchmark_service_pb2_grpc.add_BenchmarkServiceServicer_to_server(
            servicer, server
        )
    elif config.server_type == control_pb2.ASYNC_GENERIC_SERVER:
        resp_size = config.payload_config.bytebuf_params.resp_size
        servicer = benchmark_servicer.GenericBenchmarkServicer(resp_size)
        method_implementations = {
            "StreamingCall": grpc.stream_stream_rpc_method_handler(
                servicer.StreamingCall
            ),
            "UnaryCall": grpc.unary_unary_rpc_method_handler(
                servicer.UnaryCall
            ),
        }
        handler = grpc.method_handlers_generic_handler(
            "grpc.testing.BenchmarkService", method_implementations
        )
        server.add_generic_rpc_handlers((handler,))
    else:
        raise NotImplementedError(
            f"Unsupported server type {config.server_type}"
        )

    if config.HasField("security_params"):  # Use SSL
        server_creds = grpc.ssl_server_credentials(
            ((resources.private_key(), resources.certificate_chain()),)
        )
        port = server.add_secure_port(
            f"[::]:{config.port}", server_creds
        )
    else:
        port = server.add_insecure_port(f"[::]:{config.port}")

    return server, port


def _get_client_status(
    start_time: float, end_time: float, qps_data: histogram.Histogram
) -> control_pb2.ClientStatus:
    """Creates ClientStatus proto message."""
    latencies = qps_data.get_data()
    end_time = time.monotonic()
    elapsed_time = end_time - start_time
    # TODO(lidiz) Collect accurate time system to compute QPS/core-second.
    stats = stats_pb2.ClientStats(
        latencies=latencies,
        time_elapsed=elapsed_time,
        time_user=elapsed_time,
        time_system=elapsed_time,
    )
    return control_pb2.ClientStatus(stats=stats)


def _create_client(
    server: str, config: control_pb2.ClientConfig, qps_data: histogram.Histogram
) -> benchmark_client.BenchmarkClient:
    """Creates a client object according to the ClientConfig."""
    if config.load_params.WhichOneof("load") != "closed_loop":
        raise NotImplementedError(
            f"Unsupported load parameter {config.load_params}"
        )

    if config.client_type == control_pb2.ASYNC_CLIENT:
        if config.rpc_type == control_pb2.UNARY:
            client_type = benchmark_client.UnaryAsyncBenchmarkClient
        elif config.rpc_type == control_pb2.STREAMING:
            client_type = benchmark_client.StreamingAsyncBenchmarkClient
        elif config.rpc_type == control_pb2.STREAMING_FROM_SERVER:
            client_type = benchmark_client.ServerStreamingAsyncBenchmarkClient
        else:
            raise NotImplementedError(
                f"Unsupported rpc_type [{config.rpc_type}]"
            )
    else:
        raise NotImplementedError(
            f"Unsupported client type {config.client_type}"
        )

    return client_type(server, config, qps_data)


def _pick_an_unused_port() -> int:
    """Picks an unused TCP port."""
    _, port, sock = get_socket()
    sock.close()
    return port


async def _create_sub_worker() -> _SubWorker:
    """Creates a child qps worker as a subprocess."""
    port = _pick_an_unused_port()

    _LOGGER.info("Creating sub worker at port [%d]...", port)
    process = await asyncio.create_subprocess_exec(
        sys.executable, _WORKER_ENTRY_FILE, "--driver_port", str(port)
    )
    _LOGGER.info(
        "Created sub worker process for port [%d] at pid [%d]",
        port,
        process.pid,
    )
    channel = aio.insecure_channel(f"localhost:{port}")
    _LOGGER.info("Waiting for sub worker at port [%d]", port)
    await channel.channel_ready()
    stub = worker_service_pb2_grpc.WorkerServiceStub(channel)
    return _SubWorker(
        process=process,
        port=port,
        channel=channel,
        stub=stub,
    )


class WorkerServicer(worker_service_pb2_grpc.WorkerServiceServicer):
    """Python Worker Server implementation."""

    def __init__(self):
        self._loop = asyncio.get_event_loop()
        self._quit_event = asyncio.Event()

    async def _run_single_server(self, config, request_iterator, context):
        server, port = _create_server(config)
        await server.start()
        _LOGGER.info("Server started at port [%d]", port)

        start_time = time.monotonic()
        await context.write(_get_server_status(start_time, start_time, port))

        async for request in request_iterator:
            end_time = time.monotonic()
            status = _get_server_status(start_time, end_time, port)
            if request.mark.reset:
                start_time = end_time
            await context.write(status)
        await server.stop(None)

    async def RunServer(self, request_iterator, context):
        config_request = await context.read()
        config = config_request.setup
        _LOGGER.info("Received ServerConfig: %s", config)

        if config.server_processes <= 0:
            _LOGGER.info("Using server_processes == [%d]", _NUM_CORES)
            config.server_processes = _NUM_CORES

        if config.port == 0:
            config.port = _pick_an_unused_port()
        _LOGGER.info("Port picked [%d]", config.port)

        if config.server_processes == 1:
            # If server_processes == 1, start the server in this process.
            await self._run_single_server(config, request_iterator, context)
        else:
            # If server_processes > 1, offload to other processes.
            sub_workers = await asyncio.gather(
                *[_create_sub_worker() for _ in range(config.server_processes)]
            )

            calls = [worker.stub.RunServer() for worker in sub_workers]

            config_request.setup.server_processes = 1

            for call in calls:
                await call.write(config_request)
                # An empty status indicates the peer is ready
                await call.read()

            start_time = time.monotonic()
            await context.write(
                _get_server_status(
                    start_time,
                    start_time,
                    config.port,
                )
            )

            _LOGGER.info("Servers are ready to serve.")

            async for request in request_iterator:
                end_time = time.monotonic()

                for call in calls:
                    await call.write(request)
                    # Reports from sub workers doesn't matter
                    await call.read()

                status = _get_server_status(
                    start_time,
                    end_time,
                    config.port,
                )
                if request.mark.reset:
                    start_time = end_time
                await context.write(status)

            for call in calls:
                await call.done_writing()

            for worker in sub_workers:
                await worker.stub.QuitWorker(control_pb2.Void())
                await worker.channel.close()
                _LOGGER.info("Waiting for [%s] to quit...", worker)
                await worker.process.wait()

    async def _run_single_client(self, config, request_iterator, context):
        running_tasks = []
        qps_data = histogram.Histogram(
            config.histogram_params.resolution,
            config.histogram_params.max_possible,
        )
        start_time = time.monotonic()

        # Create a client for each channel as asyncio.Task
        for i in range(config.client_channels):
            server = config.server_targets[i % len(config.server_targets)]
            client = _create_client(server, config, qps_data)
            _LOGGER.info("Client created against server [%s]", server)
            running_tasks.append(self._loop.create_task(client.run()))

        end_time = time.monotonic()
        await context.write(_get_client_status(start_time, end_time, qps_data))

        # Respond to stat requests
        async for request in request_iterator:
            end_time = time.monotonic()
            status = _get_client_status(start_time, end_time, qps_data)
            if request.mark.reset:
                qps_data.reset()
                start_time = time.monotonic()
            await context.write(status)

        # Cleanup the clients
        for task in running_tasks:
            task.cancel()

    async def RunClient(self, request_iterator, context):
        config_request = await context.read()
        config = config_request.setup
        _LOGGER.info("Received ClientConfig: %s", config)

        if config.client_processes <= 0:
            _LOGGER.info(
                "client_processes can't be [%d]", config.client_processes
            )
            _LOGGER.info("Using client_processes == [%d]", _NUM_CORES)
            config.client_processes = _NUM_CORES

        if config.client_processes == 1:
            # If client_processes == 1, run the benchmark in this process.
            await self._run_single_client(config, request_iterator, context)
        else:
            # If client_processes > 1, offload the work to other processes.
            sub_workers = await asyncio.gather(
                *[_create_sub_worker() for _ in range(config.client_processes)]
            )

            calls = [worker.stub.RunClient() for worker in sub_workers]

            config_request.setup.client_processes = 1

            for call in calls:
                await call.write(config_request)
                # An empty status indicates the peer is ready
                await call.read()

            start_time = time.monotonic()
            result = histogram.Histogram(
                config.histogram_params.resolution,
                config.histogram_params.max_possible,
            )
            end_time = time.monotonic()
            await context.write(
                _get_client_status(start_time, end_time, result)
            )

            async for request in request_iterator:
                end_time = time.monotonic()

                for call in calls:
                    _LOGGER.debug("Fetching status...")
                    await call.write(request)
                    sub_status = await call.read()
                    result.merge(sub_status.stats.latencies)
                    _LOGGER.debug(
                        "Update from sub worker count=[%d]",
                        sub_status.stats.latencies.count,
                    )

                status = _get_client_status(start_time, end_time, result)
                if request.mark.reset:
                    result.reset()
                    start_time = time.monotonic()
                _LOGGER.debug(
                    "Reporting count=[%d]", status.stats.latencies.count
                )
                await context.write(status)

            for call in calls:
                await call.done_writing()

            for worker in sub_workers:
                await worker.stub.QuitWorker(control_pb2.Void())
                await worker.channel.close()
                _LOGGER.info("Waiting for sub worker [%s] to quit...", worker)
                await worker.process.wait()
                _LOGGER.info("Sub worker [%s] quit", worker)

    @staticmethod
    async def CoreCount(unused_request, unused_context):
        return control_pb2.CoreResponse(cores=_NUM_CORES)

    async def QuitWorker(self, unused_request, unused_context):
        _LOGGER.info("QuitWorker command received.")
        self._quit_event.set()
        return control_pb2.Void()

    async def wait_for_quit(self):
        await self._quit_event.wait()

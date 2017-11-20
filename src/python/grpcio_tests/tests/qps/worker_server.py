# Copyright 2016 gRPC authors.
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

import multiprocessing
import random
import threading
import time
import os
import resource

from concurrent import futures
import grpc
from src.proto.grpc.testing import control_pb2
from src.proto.grpc.testing import services_pb2_grpc
from src.proto.grpc.testing import stats_pb2

from tests.qps import benchmark_client
from tests.qps import benchmark_server
from tests.qps import client_runner
from tests.qps import histogram
from tests.unit import resources

class WorkerServer(services_pb2_grpc.WorkerServiceServicer):
    """Python Worker Server implementation."""

    def __init__(self):
        self._quit_event = threading.Event()

    def RunServer(self, request_iterator, context):
        config = next(request_iterator).setup
        server, port = self._create_server(config)
        cores = multiprocessing.cpu_count()
        server.start()
        start_time = time.time()
        start_ctime = resource.getrusage(resource.RUSAGE_SELF)
        yield self._get_server_status(start_time, start_ctime, port, cores)

        for request in request_iterator:
            status = self._get_server_status(start_time, start_ctime, port, cores)
            if request.mark.reset:
                start_time = time.time()
                start_ctime = resource.getrusage(resource.RUSAGE_SELF)
            yield status
        server.stop(None)

    def _get_server_status(self, start_time, start_ctime, port, cores):
        end_time = time.time()
        end_ctime = resource.getrusage(resource.RUSAGE_SELF)
        elapsed_time = end_time - start_time
        elapsed_stime = end_ctime.ru_stime - start_ctime.ru_stime
        elapsed_utime = end_ctime.ru_utime - start_ctime.ru_utime
        stats = stats_pb2.ServerStats(
            time_elapsed=elapsed_time,
            time_user=elapsed_utime,
            time_system=elapsed_stime)
        return control_pb2.ServerStatus(stats=stats, port=port, cores=cores)

    def _create_server(self, config):
        if config.async_server_threads == 0:
            # This is the default concurrent.futures thread pool size, but
            # None doesn't seem to work
            server_threads = multiprocessing.cpu_count() * 5
        else:
            server_threads = config.async_server_threads
        server = grpc.server(
            futures.ThreadPoolExecutor(max_workers=server_threads))
        if config.server_type == control_pb2.ASYNC_SERVER:
            servicer = benchmark_server.BenchmarkServer()
            services_pb2_grpc.add_BenchmarkServiceServicer_to_server(servicer,
                                                                     server)
        elif config.server_type == control_pb2.ASYNC_GENERIC_SERVER:
            resp_size = config.payload_config.bytebuf_params.resp_size
            servicer = benchmark_server.GenericBenchmarkServer(resp_size)
            method_implementations = {
                'StreamingCall':
                grpc.stream_stream_rpc_method_handler(servicer.StreamingCall),
                'UnaryCall':
                grpc.unary_unary_rpc_method_handler(servicer.UnaryCall),
            }
            handler = grpc.method_handlers_generic_handler(
                'grpc.testing.BenchmarkService', method_implementations)
            server.add_generic_rpc_handlers((handler,))
        else:
            raise Exception(
                'Unsupported server type {}'.format(config.server_type))

        if config.HasField('security_params'):  # Use SSL
            server_creds = grpc.ssl_server_credentials((
                (resources.private_key(), resources.certificate_chain()),))
            port = server.add_secure_port('[::]:{}'.format(config.port),
                                          server_creds)
        else:
            port = server.add_insecure_port('[::]:{}'.format(config.port))

        return (server, port)

    def run(self, conn, config, context, i):
        server = config.server_targets[i % len(config.server_targets)]
        qps_data = histogram.Histogram(config.histogram_params.resolution,
                config.histogram_params.max_possible)
        runner = self._create_client_runner(server, config, qps_data)
        start_ctime = resource.getrusage(resource.RUSAGE_SELF)
        runner.start()
        try:
            while True:
                cmd_from_server = conn.recv()
                end_ctime = resource.getrusage(resource.RUSAGE_SELF)
                if cmd_from_server == "reset" :
                    qps_data.reset()
                    start_ctime = end_ctime
                if cmd_from_server == "mark" :
                    conn.send([end_ctime.ru_utime - start_ctime.ru_utime,
                        end_ctime.ru_stime - start_ctime.ru_stime, qps_data.get_data()])
                if cmd_from_server == "quit_process" :
                    runner.stop()
                    break
        except KeyboardInterrupt:
            server.stop(0)

    def RunClient(self, request_iterator, context):
        config = next(request_iterator).setup
        client_runners = []
        qps_data = histogram.Histogram(config.histogram_params.resolution,
                                       config.histogram_params.max_possible)
        start_time = time.time()
        report_utime = 0
        report_stime = 0
        # Create a client for each channel
        client_conn = []
        for i in xrange(config.client_channels):
            client_conn.append(multiprocessing.Pipe())
            multiprocessing.Process(target=self.run,args=[client_conn[i][1], config, context, i]).start()
        end_time = time.time()
        yield self._get_client_status(start_time, report_utime, report_stime, qps_data)
        # Respond to stat requests
        for request in request_iterator:
            end_time = time.time()
            for i in xrange(config.client_channels):
                client_conn[i][0].send('mark')
                [utime, stime, client_data] = client_conn[i][0].recv()
                report_utime += utime
                report_stime += stime
                qps_data.merge_hist(client_data)
            status = self._get_client_status(start_time, report_utime, report_stime, qps_data)
            if request.mark.reset:
                qps_data.reset()
                for i in xrange(config.client_channels):
                    client_conn[i][0].send('reset')
                start_time = time.time()
            yield status
        # Cleanup the clients
        for i in xrange(config.client_channels):
            client_conn[i][0].send('quit_process')
        client_conn = []

    def _get_client_status(self, start_time, report_utime, report_stime, qps_data):
        latencies = qps_data.get_data()
        end_time = time.time()
        elapsed_time = end_time - start_time
        stats = stats_pb2.ClientStats(
            latencies=latencies,
            time_elapsed=elapsed_time,
            time_user=report_utime,
            time_system=report_stime)
        return control_pb2.ClientStatus(stats=stats)

    def _create_client_runner(self, server, config, qps_data):
        if config.client_type == control_pb2.SYNC_CLIENT:
            if config.rpc_type == control_pb2.UNARY:
                client = benchmark_client.UnarySyncBenchmarkClient(
                    server, config, qps_data)
            elif config.rpc_type == control_pb2.STREAMING:
                client = benchmark_client.StreamingSyncBenchmarkClient(
                    server, config, qps_data)
        elif config.client_type == control_pb2.ASYNC_CLIENT:
            if config.rpc_type == control_pb2.UNARY:
                client = benchmark_client.UnaryAsyncBenchmarkClient(
                    server, config, qps_data)
            else:
                raise Exception('Async streaming client not supported')
        else:
            raise Exception(
                'Unsupported client type {}'.format(config.client_type))

        # In multi-channel tests, we split the load across all channels
        load_factor = float(config.client_channels)
        if config.load_params.WhichOneof('load') == 'closed_loop':
            runner = client_runner.ClosedLoopClientRunner(
                client, config.outstanding_rpcs_per_channel)
        else:  # Open loop Poisson
            alpha = config.load_params.poisson.offered_load / load_factor

            def poisson():
                while True:
                    yield random.expovariate(alpha)

            runner = client_runner.OpenLoopClientRunner(client, poisson())

        return runner

    def CoreCount(self, request, context):
        return control_pb2.CoreResponse(cores=multiprocessing.cpu_count())

    def QuitWorker(self, request, context):
        self._quit_event.set()
        return control_pb2.Void()

    def wait_for_quit(self):
        self._quit_event.wait()

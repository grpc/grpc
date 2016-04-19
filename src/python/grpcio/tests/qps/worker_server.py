# Copyright 2015, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

import multiprocessing
import threading
import time

from grpc.beta import implementations
from tests.unit import resources

from tests.qps.benchmark_client import *
from tests.qps.benchmark_server import BenchmarkServer
from tests.qps.client_runner import ClosedLoopClientRunner, OpenLoopClientRunner
from tests.qps import intervals
from tests.qps.histogram import Histogram
from tests.qps.control_pb2 import *
from tests.qps.stats_pb2 import ServerStats, ClientStats
from tests.qps import services_pb2

""" Python Worker Server implementation.  Responsible for running servers 
and clients on demand.
"""
class WorkerServer(services_pb2.BetaWorkerServiceServicer):


  def __init__(self):
    self._quit_event = threading.Event()

  def RunServer(self, request_iterator, context):
    config = next(request_iterator).setup
    server, port = self._create_server(config)
    cores = multiprocessing.cpu_count()
    server.start()
    start_time = time.time()
    yield self._get_server_status(start_time, start_time, port, cores)
      
    for request in request_iterator:
      end_time = time.time()
      status = self._get_server_status(start_time, end_time, port, cores)
      if(request.mark.reset):
        start_time = end_time
      yield status
    server.stop(0)

  def _get_server_status(self, start_time, end_time, port, cores):
    end_time = time.time()
    elapsed_time = end_time - start_time
    stats = ServerStats(time_elapsed=elapsed_time,
        time_user=elapsed_time, time_system=elapsed_time)
    return ServerStatus(stats=stats, port=port, cores=cores)

  def _create_server(self, config):
    if config.server_type != SYNC_SERVER:
      raise Exception('Unsupported server type {}'.format(config.ServerType))

    if config.HasField("security_params"): #Use SSL
      server_creds = implementations.ssl_server_credentials(
          [(resources.private_key(), resources.certificate_chain())])
      server = services_pb2.beta_create_BenchmarkService_server(BenchmarkServer())
      port = server.add_secure_port('[::]:{}'.format(config.port), server_creds)
    else:
      server = services_pb2.beta_create_BenchmarkService_server(BenchmarkServer())
      port = server.add_insecure_port('[::]:{}'.format(config.port))

    return (server, port)

  def RunClient(self, request_iterator, context):
    config = next(request_iterator).setup
    client_runners = []
    qps_data = Histogram(config.histogram_params.resolution, 
                         config.histogram_params.max_possible)
    start_time = time.time()
      
    # Create a client for each channel
    for i in xrange(config.client_channels):
      server = config.server_targets[i % len(config.server_targets)]
      runner = self._create_client_runner(server, config, qps_data)
      client_runners.append(runner)
      runner.start()

    end_time = time.time()
    yield self._get_client_status(start_time, end_time, qps_data)

    # Respond to stat requests
    for request in request_iterator:
      end_time = time.time()
      status = self._get_client_status(start_time, end_time, qps_data)
      if request.mark.reset:
        qps_data.reset()
        start_time = end_time
      yield status
    
    # Cleanup the clients
    for runner in client_runners:
      runner.stop()

  def _get_client_status(self, start_time, end_time, qps_data):
    latencies = qps_data.get_data()
    end_time = time.time()
    elapsed_time = end_time - start_time
    stats = ClientStats(latencies=latencies, time_elapsed=elapsed_time,
        time_user=elapsed_time, time_system=elapsed_time)
    return ClientStatus(stats=stats)
  

  def _create_client_runner(self, server, config, qps_data):
    use_ssl = config.HasField("security_params")
    if config.client_type == SYNC_CLIENT:
      assert config.rpc_type == UNARY
      client = UnarySyncBenchmarkClient(server, config.payload_config, use_ssl, qps_data, config.outstanding_rpcs_per_channel)
    elif config.rpc_type == UNARY:
      client = UnaryAsyncBenchmarkClient(server, config.payload_config, use_ssl, qps_data)
    else:
      client = StreamingAsyncBenchmarkClient(server, config.payload_config, use_ssl, qps_data)

    # In multi-channel tests, we split the load across all channels
    load_factor = float(config.client_channels)
    if config.load_params.HasField("closed_loop"):
      runner = ClosedLoopClientRunner(client, config.outstanding_rpcs_per_channel)
    elif config.load_params.HasField("poisson"):
      runner = OpenLoopClientRunner(client,
          intervals.poisson(config.load_params.poisson.offered_load/load_factor))
    else:
      raise Exception("Only ClosedLoop and Poisson parameters supported")
    
    return runner
    
  def CoreCount(self, request, context):
    return CoreResponse(cores=multiprocessing.cpu_count())

  def QuitWorker(self, request, context):
    self._quit_event.set()
    return Void()

  def wait_for_quit(self):
    self._quit_event.wait()

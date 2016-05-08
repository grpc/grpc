# Copyright 2016, Google Inc.
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

"""Entry point for running stress tests."""

import argparse
import Queue
import threading

from grpc.beta import implementations
from src.proto.grpc.testing import metrics_pb2
from src.proto.grpc.testing import test_pb2

from tests.interop import methods
from tests.qps import histogram
from tests.stress import metrics_server
from tests.stress import test_runner


def _args():
  parser = argparse.ArgumentParser(description='gRPC Python stress test client')
  parser.add_argument(
      '--server_addresses',
      help='comma seperated list of hostname:port to run servers on',
      default='localhost:8080', type=str)
  parser.add_argument(
      '--test_cases',
      help='comma seperated list of testcase:weighting of tests to run',
      default='large_unary:100',
      type=str)
  parser.add_argument(
      '--test_duration_secs',
      help='number of seconds to run the stress test',
      default=-1, type=int)
  parser.add_argument(
      '--num_channels_per_server',
      help='number of channels per server',
      default=1, type=int)
  parser.add_argument(
      '--num_stubs_per_channel',
      help='number of stubs to create per channel',
      default=1, type=int)
  parser.add_argument(
      '--metrics_port',
      help='the port to listen for metrics requests on',
      default=8081, type=int)
  return parser.parse_args()


def _test_case_from_arg(test_case_arg):
  for test_case in methods.TestCase:
    if test_case_arg == test_case.value:
      return test_case
  else:
    raise ValueError('No test case {}!'.format(test_case_arg))


def _parse_weighted_test_cases(test_case_args):
  weighted_test_cases = {}
  for test_case_arg in test_case_args.split(','):
    name, weight = test_case_arg.split(':', 1)
    test_case = _test_case_from_arg(name)
    weighted_test_cases[test_case] = int(weight)
  return weighted_test_cases


def run_test(args):
  test_cases = _parse_weighted_test_cases(args.test_cases)
  test_servers = args.server_addresses.split(',')
  # Propagate any client exceptions with a queue
  exception_queue = Queue.Queue()
  stop_event = threading.Event()
  hist = histogram.Histogram(1, 1)
  runners = []

  server = metrics_pb2.beta_create_MetricsService_server(
      metrics_server.MetricsServer(hist))
  server.add_insecure_port('[::]:{}'.format(args.metrics_port))
  server.start()

  for test_server in test_servers:
    host, port = test_server.split(':', 1)
    for _ in xrange(args.num_channels_per_server):
      channel = implementations.insecure_channel(host, int(port))
      for _ in xrange(args.num_stubs_per_channel):
        stub = test_pb2.beta_create_TestService_stub(channel)
        runner = test_runner.TestRunner(stub, test_cases, hist,
                                        exception_queue, stop_event)
        runners.append(runner)

  for runner in runners:
    runner.start()
  try:
    timeout_secs = args.test_duration_secs
    if timeout_secs < 0:
      timeout_secs = None
    raise exception_queue.get(block=True, timeout=timeout_secs)
  except Queue.Empty:
    # No exceptions thrown, success
    pass
  finally:
    stop_event.set()
    for runner in runners:
      runner.join()
      runner = None
    server.stop(0)

if __name__ == '__main__':
  run_test(_args())

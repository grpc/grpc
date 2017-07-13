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
"""Entry point for running stress tests."""

import argparse
from concurrent import futures
import threading

import grpc
from six.moves import queue
from src.proto.grpc.testing import metrics_pb2_grpc
from src.proto.grpc.testing import test_pb2

from tests.interop import methods
from tests.interop import resources
from tests.qps import histogram
from tests.stress import metrics_server
from tests.stress import test_runner


def _args():
    parser = argparse.ArgumentParser(
        description='gRPC Python stress test client')
    parser.add_argument(
        '--server_addresses',
        help='comma seperated list of hostname:port to run servers on',
        default='localhost:8080',
        type=str)
    parser.add_argument(
        '--test_cases',
        help='comma seperated list of testcase:weighting of tests to run',
        default='large_unary:100',
        type=str)
    parser.add_argument(
        '--test_duration_secs',
        help='number of seconds to run the stress test',
        default=-1,
        type=int)
    parser.add_argument(
        '--num_channels_per_server',
        help='number of channels per server',
        default=1,
        type=int)
    parser.add_argument(
        '--num_stubs_per_channel',
        help='number of stubs to create per channel',
        default=1,
        type=int)
    parser.add_argument(
        '--metrics_port',
        help='the port to listen for metrics requests on',
        default=8081,
        type=int)
    parser.add_argument(
        '--use_test_ca',
        help='Whether to use our fake CA. Requires --use_tls=true',
        default=False,
        type=bool)
    parser.add_argument(
        '--use_tls', help='Whether to use TLS', default=False, type=bool)
    parser.add_argument(
        '--server_host_override',
        default="foo.test.google.fr",
        help='the server host to which to claim to connect',
        type=str)
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


def _get_channel(target, args):
    if args.use_tls:
        if args.use_test_ca:
            root_certificates = resources.test_root_certificates()
        else:
            root_certificates = None  # will load default roots.
        channel_credentials = grpc.ssl_channel_credentials(
            root_certificates=root_certificates)
        options = (('grpc.ssl_target_name_override',
                    args.server_host_override,),)
        channel = grpc.secure_channel(
            target, channel_credentials, options=options)
    else:
        channel = grpc.insecure_channel(target)

    # waits for the channel to be ready before we start sending messages
    grpc.channel_ready_future(channel).result()
    return channel


def run_test(args):
    test_cases = _parse_weighted_test_cases(args.test_cases)
    test_server_targets = args.server_addresses.split(',')
    # Propagate any client exceptions with a queue
    exception_queue = queue.Queue()
    stop_event = threading.Event()
    hist = histogram.Histogram(1, 1)
    runners = []

    server = grpc.server(futures.ThreadPoolExecutor(max_workers=25))
    metrics_pb2_grpc.add_MetricsServiceServicer_to_server(
        metrics_server.MetricsServer(hist), server)
    server.add_insecure_port('[::]:{}'.format(args.metrics_port))
    server.start()

    for test_server_target in test_server_targets:
        for _ in xrange(args.num_channels_per_server):
            channel = _get_channel(test_server_target, args)
            for _ in xrange(args.num_stubs_per_channel):
                stub = test_pb2.TestServiceStub(channel)
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
    except queue.Empty:
        # No exceptions thrown, success
        pass
    finally:
        stop_event.set()
        for runner in runners:
            runner.join()
        runner = None
        server.stop(None)


if __name__ == '__main__':
    run_test(_args())

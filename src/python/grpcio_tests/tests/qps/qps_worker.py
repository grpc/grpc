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
"""The entry point for the qps worker."""

import argparse
import time

from concurrent import futures
import grpc
from src.proto.grpc.testing import services_pb2_grpc

from tests.qps import worker_server


def run_worker_server(port):
    server = grpc.server(futures.ThreadPoolExecutor(max_workers=5))
    servicer = worker_server.WorkerServer()
    services_pb2_grpc.add_WorkerServiceServicer_to_server(servicer, server)
    server.add_insecure_port('[::]:{}'.format(port))
    server.start()
    servicer.wait_for_quit()
    server.stop(0)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(
        description='gRPC Python performance testing worker')
    parser.add_argument(
        '--driver_port',
        type=int,
        dest='port',
        help='The port the worker should listen on')
    args = parser.parse_args()

    run_worker_server(args.port)

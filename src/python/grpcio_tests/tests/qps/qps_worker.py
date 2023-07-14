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
"""The entry point for the qps worker."""

import argparse
import logging
import time

import grpc

from src.proto.grpc.testing import worker_service_pb2_grpc
from tests.qps import worker_server
from tests.unit import test_common


def run_worker_server(driver_port, server_port):
    server = test_common.test_server()
    servicer = worker_server.WorkerServer(server_port)
    worker_service_pb2_grpc.add_WorkerServiceServicer_to_server(
        servicer, server
    )
    server.add_insecure_port("[::]:{}".format(driver_port))
    server.start()
    servicer.wait_for_quit()
    server.stop(0)


if __name__ == "__main__":
    logging.basicConfig(level=logging.DEBUG)
    parser = argparse.ArgumentParser(
        description="gRPC Python performance testing worker"
    )
    parser.add_argument(
        "--driver_port",
        type=int,
        dest="driver_port",
        help="The port for the worker to expose for driver communication",
    )
    parser.add_argument(
        "--server_port",
        type=int,
        default=None,
        dest="server_port",
        help=(
            "The port for the server if not specified by server config message"
        ),
    )
    args = parser.parse_args()

    run_worker_server(args.driver_port, args.server_port)

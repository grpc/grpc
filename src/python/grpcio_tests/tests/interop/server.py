# Copyright 2015 gRPC authors.
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
"""The Python implementation of the GRPC interoperability test server."""

import os

os.environ["GRPC_BAZEL_RUNTIME"] = "1"
try:
    from tests import bazel_namespace_package_hack

    bazel_namespace_package_hack.sys_path_to_site_dir_hack()
except ImportError:
    pass

# pylint: disable=wrong-import-position
from concurrent import futures
import logging

from absl import app
from absl.flags import argparse_flags
import grpc

from src.proto.grpc.testing import test_pb2_grpc
from tests.interop import resources
from tests.interop import service
from tests.unit import test_common
# pylint: enable=wrong-import-position

logging.basicConfig()
_LOGGER = logging.getLogger(__name__)


def parse_interop_server_arguments(argv):
    parser = argparse_flags.ArgumentParser()
    parser.add_argument(
        "--port", type=int, required=True, help="the port on which to serve"
    )
    parser.add_argument(
        "--use_tls",
        default=False,
        type=resources.parse_bool,
        help="require a secure connection",
    )
    parser.add_argument(
        "--use_alts",
        default=False,
        type=resources.parse_bool,
        help="require an ALTS connection",
    )
    parser.add_argument(
        "--enable_opentelemetry",
        default=False,
        type=resources.parse_bool,
        help="enable OpenTelemetry tracing/observability",
    )
    return parser.parse_args(argv[1:])


def get_server_credentials(use_tls):
    if use_tls:
        private_key = resources.private_key()
        certificate_chain = resources.certificate_chain()
        return grpc.ssl_server_credentials(((private_key, certificate_chain),))
    else:
        return grpc.alts_server_credentials()


import signal

def _serve_internal(server):
    def _sig_handler(signum, frame):
        _LOGGER.info("Received signal %d, stopping server...", signum)
        server.stop(0)

    signal.signal(signal.SIGTERM, _sig_handler)
    signal.signal(signal.SIGINT, _sig_handler)

    server.start()
    _LOGGER.info("Server serving.")
    server.wait_for_termination()
    _LOGGER.info("Server stopped; exiting.")


def serve(args):
    server = test_common.test_server()
    test_pb2_grpc.add_TestServiceServicer_to_server(
        service.TestService(), server
    )
    if args.use_tls or args.use_alts:
        credentials = get_server_credentials(args.use_tls)
        server.add_secure_port("[::]:{}".format(args.port), credentials)
    else:
        server.add_insecure_port("[::]:{}".format(args.port))

    if args.enable_opentelemetry:
        import grpc_observability
        with grpc_observability.OpenTelemetryPlugin():
            _serve_internal(server)
    else:
        _serve_internal(server)


if __name__ == "__main__":
    app.run(serve, flags_parser=parse_interop_server_arguments)

# Copyright 2026 gRPC authors.
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

import os
import functions_framework
import grpc

from src.proto.grpc.testing import test_pb2_grpc
from src.proto.grpc.testing import messages_pb2

# Use environment variable for the port
port = os.getenv("GRPC_SERVER_PORT", "50051")
channel = grpc.insecure_channel(f"localhost:{port}")
stub = test_pb2_grpc.TestServiceStub(channel)


def initial_call():
    response = stub.UnaryCall(messages_pb2.SimpleRequest(), timeout=5)


if os.getenv("TEST_INITIAL_CALL", "0") == "1":
    initial_call()


def process_call():
    try:
        response = stub.UnaryCall(messages_pb2.SimpleRequest(), timeout=5)
    except grpc.RpcError as e:
        return f"gRPC error: {e.code().name}\n"
    except Exception as e:
        return f"Other error: {e!r}\n"

    return "OK, called"


@functions_framework.http
def hello(request):
    return process_call()

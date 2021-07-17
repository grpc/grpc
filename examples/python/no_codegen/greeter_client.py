# Copyright 2020 The gRPC authors.
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
"""Hello World without using protoc.

This example parses message and service schemas directly from a
.proto file on the filesystem.

Several APIs used in this example are in an experimental state.
"""

from __future__ import print_function
import logging

import grpc
import grpc.experimental

# NOTE: The path to the .proto file must be reachable from an entry
# on sys.path. Use sys.path.insert or set the $PYTHONPATH variable to
# import from files located elsewhere on the filesystem.

protos = grpc.protos("helloworld.proto")
services = grpc.services("helloworld.proto")

logging.basicConfig()

response = services.Greeter.SayHello(protos.HelloRequest(name='you'),
                                     'localhost:50051',
                                     insecure=True)
print("Greeter client received: " + response.message)

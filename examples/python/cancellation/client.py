# Copyright the 2019 gRPC authors.
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
"""An example of cancelling requests in gRPC."""

from __future__ import absolute_import
from __future__ import division
from __future__ import print_function

from concurrent import futures
import logging
import time

import grpc

from examples.python.cancellation import hash_name_pb2
from examples.python.cancellation import hash_name_pb2_grpc

_LOGGER = logging.getLogger(__name__)

def main():
    # TODO(rbellevi): Fix the connaissance of target.
    with grpc.insecure_channel('localhost:50051') as channel:
        stub = hash_name_pb2_grpc.HashFinderStub(channel)
        response = stub.Find(hash_name_pb2.HashNameRequest(desired_name="doctor",
                                                     maximum_hamming_distance=0))
        print(response)

if __name__ == "__main__":
    logging.basicConfig()
    main()

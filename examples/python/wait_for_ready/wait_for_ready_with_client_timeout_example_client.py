# Copyright 2023 The gRPC Authors
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
"""The Python client of utilizing wait-for-ready flag with client time out."""

import logging
import threading

import grpc

helloworld_pb2, helloworld_pb2_grpc = grpc.protos_and_services(
    "helloworld.proto")

_LOGGER = logging.getLogger(__name__)
_LOGGER.setLevel(logging.INFO)

def wait_for_metadata(response_future, event):
    for key, value in response_future.initial_metadata():
        print('Greeter client received initial metadata: key=%s value=%s' %
            (key, value))
    event.set()

def check_status(response_future, wait_success):
    if wait_success:
        print("received initial metadata before time out!")
        for response in response_future:
            message = response.message
        print("Greeter client received: " + message)
    else:
        print("Timed out before receiving any initial metadata!")
        response_future.cancel()


def main():
    # Create gRPC channel
    with grpc.insecure_channel('localhost:50051') as channel:
        stub = helloworld_pb2_grpc.GreeterStub(channel)

        event_for_delay = threading.Event()

        # Server will delay send initial metadata back for this RPC
        response_future_delay = stub.SayHelloStreamReply(
            helloworld_pb2.HelloRequest(name='you'), wait_for_ready=True)

        # Fire RPC and wait for metadata
        thread_with_delay = threading.Thread(target=wait_for_metadata,
            args=(response_future_delay, event_for_delay))
        thread_with_delay.start()

        # Wait on client side with timeout
        timeout = 3
        check_status(response_future_delay, event_for_delay.wait(timeout))

        # Expected to timeout.
        thread_with_delay.join()

if __name__ == '__main__':
    logging.basicConfig(level=logging.INFO)
    main()

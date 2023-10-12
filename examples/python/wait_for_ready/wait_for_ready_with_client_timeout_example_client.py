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
"""An example of setting a server connection timeout independent from the
overall RPC timeout.

For stream server, if client set wait_for_ready but server never actually starts,
client will wait indefinitely, this example will do the following steps to set a
timeout on client side:
1. Set server to return initial_metadata once it receives request.
2. Client will set a timer (customized client timeout) waiting for initial_metadata.
3. Client will timeout if it didn't receive initial_metadata.
"""
import logging
import threading
from typing import Sequence, Tuple

import grpc

helloworld_pb2, helloworld_pb2_grpc = grpc.protos_and_services(
    "helloworld.proto"
)

_LOGGER = logging.getLogger(__name__)
_LOGGER.setLevel(logging.INFO)


def wait_for_metadata(response_future, event):
    metadata: Sequence[Tuple[str, str]] = response_future.initial_metadata()
    for key, value in metadata:
        print(
            "Greeter client received initial metadata: key=%s value=%s"
            % (key, value)
        )
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
    with grpc.insecure_channel("localhost:50051") as channel:
        stub = helloworld_pb2_grpc.GreeterStub(channel)

        event_for_delay = threading.Event()

        # Server will delay send initial metadata back for this RPC
        response_future_delay = stub.SayHelloStreamReply(
            helloworld_pb2.HelloRequest(name="you"), wait_for_ready=True
        )

        # Fire RPC and wait for metadata
        thread_with_delay = threading.Thread(
            target=wait_for_metadata,
            args=(response_future_delay, event_for_delay),
            daemon=True,
        )
        thread_with_delay.start()

        # Wait on client side with 7 seconds timeout
        timeout = 7
        check_status(response_future_delay, event_for_delay.wait(timeout))


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO)
    main()

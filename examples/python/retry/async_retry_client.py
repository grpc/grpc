# Copyright 2021 The gRPC Authors
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
"""The Python AsyncIO implementation of the gRPC client-side retry example."""

import asyncio
import json
import logging

import grpc

helloworld_pb2, helloworld_pb2_grpc = grpc.protos_and_services(
    "helloworld.proto"
)


async def run() -> None:
    # The ServiceConfig proto definition can be found:
    # https://github.com/grpc/grpc-proto/blob/ec886024c2f7b7f597ba89d5b7d60c3f94627b17/grpc/service_config/service_config.proto#L377
    service_config_json = json.dumps(
        {
            "methodConfig": [
                {
                    # To apply retry to all methods, put [{}] in the "name" field
                    "name": [
                        {"service": "helloworld.Greeter", "method": "SayHello"}
                    ],
                    "retryPolicy": {
                        "maxAttempts": 5,
                        "initialBackoff": "0.1s",
                        "maxBackoff": "1s",
                        "backoffMultiplier": 2,
                        "retryableStatusCodes": ["UNAVAILABLE"],
                    },
                }
            ]
        }
    )
    options = []
    # NOTE: the retry feature will be enabled by default >=v1.40.0
    options.append(("grpc.enable_retries", 1))
    options.append(("grpc.service_config", service_config_json))
    async with grpc.aio.insecure_channel(
        "localhost:50051", options=options
    ) as channel:
        stub = helloworld_pb2_grpc.GreeterStub(channel)
        response = await stub.SayHello(helloworld_pb2.HelloRequest(name="you"))
    print("Greeter client received: " + response.message)


if __name__ == "__main__":
    logging.basicConfig()
    asyncio.run(run())

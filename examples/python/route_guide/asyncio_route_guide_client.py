# Copyright 2020 The gRPC Authors
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
"""The Python AsyncIO implementation of the gRPC route guide client."""

import asyncio
import logging
import random
from typing import Iterable, List

import grpc
import route_guide_pb2
import route_guide_pb2_grpc
import route_guide_resources


def make_route_note(
    message: str, latitude: int, longitude: int
) -> route_guide_pb2.RouteNote:
    return route_guide_pb2.RouteNote(
        message=message,
        location=route_guide_pb2.Point(latitude=latitude, longitude=longitude),
    )


# Performs an unary call
async def guide_get_one_feature(
    stub: route_guide_pb2_grpc.RouteGuideStub, point: route_guide_pb2.Point
) -> None:
    feature = await stub.GetFeature(point)
    if not feature.location:
        print("Server returned incomplete feature")
        return

    if feature.name:
        print(f"Feature called {feature.name} at {feature.location}")
    else:
        print(f"Found no feature at {feature.location}")


async def guide_get_feature(stub: route_guide_pb2_grpc.RouteGuideStub) -> None:
    # The following two coroutines will be wrapped in a Future object
    # and scheduled in the event loop so that they can run concurrently
    task_group = asyncio.gather(
        guide_get_one_feature(
            stub,
            route_guide_pb2.Point(latitude=409146138, longitude=-746188906),
        ),
        guide_get_one_feature(
            stub, route_guide_pb2.Point(latitude=0, longitude=0)
        ),
    )
    # Wait until the Future is resolved
    await task_group


# Performs a server-streaming call
async def guide_list_features(
    stub: route_guide_pb2_grpc.RouteGuideStub,
) -> None:
    rectangle = route_guide_pb2.Rectangle(
        lo=route_guide_pb2.Point(latitude=400000000, longitude=-750000000),
        hi=route_guide_pb2.Point(latitude=420000000, longitude=-730000000),
    )
    print("Looking for features between 40, -75 and 42, -73")

    features = stub.ListFeatures(rectangle)

    async for feature in features:
        print(f"Feature called {feature.name} at {feature.location}")


def generate_route(
    feature_list: List[route_guide_pb2.Feature],
) -> Iterable[route_guide_pb2.Point]:
    for _ in range(0, 10):
        random_feature = random.choice(feature_list)
        print(f"Visiting point {random_feature.location}")
        yield random_feature.location


# Performs a client-streaming call
async def guide_record_route(stub: route_guide_pb2_grpc.RouteGuideStub) -> None:
    feature_list = route_guide_resources.read_route_guide_database()
    route_iterator = generate_route(feature_list)

    # gRPC AsyncIO client-streaming RPC API accepts both synchronous iterables
    # and async iterables.
    route_summary = await stub.RecordRoute(route_iterator)
    print(f"Finished trip with {route_summary.point_count} points")
    print(f"Passed {route_summary.feature_count} features")
    print(f"Travelled {route_summary.distance} meters")
    print(f"It took {route_summary.elapsed_time} seconds")


def generate_messages() -> Iterable[route_guide_pb2.RouteNote]:
    messages = [
        make_route_note("First message", 0, 0),
        make_route_note("Second message", 0, 1),
        make_route_note("Third message", 1, 0),
        make_route_note("Fourth message", 0, 0),
        make_route_note("Fifth message", 1, 0),
    ]
    for msg in messages:
        print(f"Sending {msg.message} at {msg.location}")
        yield msg


# Performs a bidi-streaming call
async def guide_route_chat(stub: route_guide_pb2_grpc.RouteGuideStub) -> None:
    # gRPC AsyncIO bidi-streaming RPC API accepts both synchronous iterables
    # and async iterables.
    call = stub.RouteChat(generate_messages())
    async for response in call:
        print(f"Received message {response.message} at {response.location}")


async def main() -> None:
    async with grpc.aio.insecure_channel("localhost:50051") as channel:
        stub = route_guide_pb2_grpc.RouteGuideStub(channel)
        print("-------------- GetFeature --------------")
        await guide_get_feature(stub)
        print("-------------- ListFeatures --------------")
        await guide_list_features(stub)
        print("-------------- RecordRoute --------------")
        await guide_record_route(stub)
        print("-------------- RouteChat --------------")
        await guide_route_chat(stub)


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO)
    asyncio.get_event_loop().run_until_complete(main())

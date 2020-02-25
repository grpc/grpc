"""Flaky route guide server."""

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

import logging
import threading
import random

from concurrent import futures

import grpc

import route_guide_pb2
import route_guide_pb2_grpc

_FEATURES_PER_REQUEST = 16
_FAILURE_INTERVAL = 31

class RouteGuideServicer(route_guide_pb2_grpc.RouteGuideServicer):
    """Provides methods that implement functionality of route guide server."""

    def __init__(self):
        self._lock = threading.Lock()
        self._counter = 0

    # NOTE: Other methods are intentionally left unimplemented.

    def ListFeatures(self, request, context):
        for feature in range(_FEATURES_PER_REQUEST):
            feature = route_guide_pb2.Feature()
            with self._lock:
                if self._counter % _FAILURE_INTERVAL == 0:
                    self._counter += 1
                    context.abort(grpc.StatusCode.RESOURCE_EXHAUSTED, "Exhausted")
                feature.name = str(self._counter)
                self._counter += 1
            feature.location.latitude = int(random.uniform(request.lo.latitude, request.hi.latitude))
            feature.location.longitude = int(random.uniform(request.lo.longitude, request.hi.longitude))
            yield feature


def serve():
    server = grpc.server(futures.ThreadPoolExecutor())
    route_guide_pb2_grpc.add_RouteGuideServicer_to_server(
        RouteGuideServicer(), server)
    server.add_insecure_port('[::]:50051')
    server.start()
    server.wait_for_termination()

if __name__ == '__main__':
    logging.basicConfig()
    serve()

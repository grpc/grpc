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
"""CSDS implementation for gRPC Python."""

import grpc
from grpc._cython import cygrpc

from envoy.service.status.v3 import csds_pb2
from envoy.service.status.v3 import csds_pb2_grpc

from google.protobuf import json_format


class ClientStatusDiscoveryServiceServicer(
        csds_pb2_grpc.ClientStatusDiscoveryServiceServicer):
    """CSDS Servicer."""

    @staticmethod
    def FetchClientStatus(request, unused_context):
        client_config = csds_pb2.ClientConfig.FromString(
            cygrpc.dump_xds_configs())
        response = csds_pb2.ClientStatusResponse()
        response.config.append(client_config)
        return response

    @staticmethod
    def StreamClientStatus(request_iterator, context):
        for request in request_iterator:
            yield ClientStatusDiscoveryServiceServicer.FetchClientStatus(
                request, context)

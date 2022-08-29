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
"""Channelz debug service implementation in gRPC Python."""

from envoy.service.status.v3 import csds_pb2
from envoy.service.status.v3 import csds_pb2_grpc
from google.protobuf import json_format
from grpc._cython import cygrpc


class ClientStatusDiscoveryServiceServicer(
        csds_pb2_grpc.ClientStatusDiscoveryServiceServicer):
    """CSDS Servicer works for both the sync API and asyncio API."""

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


def add_csds_servicer(server):
    """Register CSDS servicer to a server.

    CSDS is part of xDS protocol used to expose in-effective traffic
    configuration (or xDS resources). It focuses on simplify the debugging of
    unexpected routing behaviors, which could be due to a misconfiguration,
    unhealthy backends or issues in the control or data plane.

    Args:
        server: A gRPC server to which the CSDS service will be added.
    """
    csds_pb2_grpc.add_ClientStatusDiscoveryServiceServicer_to_server(
        ClientStatusDiscoveryServiceServicer(), server)


__all__ = ['ClientStatusDiscoveryServiceServicer', 'add_csds_servicer']

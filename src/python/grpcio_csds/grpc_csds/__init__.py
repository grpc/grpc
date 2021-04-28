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

import sys
import grpc

from envoy.service.status.v3 import csds_pb2_grpc
from grpc_csds._servicer import ClientStatusDiscoveryServiceServicer


def add_csds_servicer(server):
    csds_pb2_grpc.add_ChannelzServicer_to_server(
        ClientStatusDiscoveryServiceServicer(), server)

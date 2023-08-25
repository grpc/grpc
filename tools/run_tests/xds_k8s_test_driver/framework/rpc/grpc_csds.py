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
"""
This contains helpers for gRPC services defined in
https://github.com/envoyproxy/envoy/blob/main/api/envoy/service/status/v3/csds.proto
"""

import logging
from typing import Optional

# Needed to load the descriptors so that Any is parsed
# TODO(sergiitk): replace with import xds_protos when it works
import framework.rpc.xds_protos_imports  # pylint: disable=unused-import

from envoy.service.status.v3 import csds_pb2
from envoy.service.status.v3 import csds_pb2_grpc
import grpc

import framework.rpc

logger = logging.getLogger(__name__)

# Type aliases
ClientConfig = csds_pb2.ClientConfig
_ClientStatusRequest = csds_pb2.ClientStatusRequest


class CsdsClient(framework.rpc.grpc.GrpcClientHelper):
    stub: csds_pb2_grpc.ClientStatusDiscoveryServiceStub

    def __init__(
        self, channel: grpc.Channel, *, log_target: Optional[str] = ""
    ):
        super().__init__(
            channel,
            csds_pb2_grpc.ClientStatusDiscoveryServiceStub,
            log_target=log_target,
        )

    def fetch_client_status(self, **kwargs) -> Optional[ClientConfig]:
        """Fetches the active xDS configurations."""
        response = self.call_unary_with_deadline(
            rpc="FetchClientStatus", req=_ClientStatusRequest(), **kwargs
        )
        if len(response.config) != 1:
            logger.debug(
                "Unexpected number of client configs: %s", len(response.config)
            )
            return None
        return response.config[0]

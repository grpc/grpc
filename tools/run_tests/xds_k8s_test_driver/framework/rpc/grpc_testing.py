# Copyright 2020 gRPC authors.
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
https://github.com/grpc/grpc/blob/master/src/proto/grpc/testing/test.proto
"""
import logging
from typing import Iterable, Optional, Tuple

import grpc
from grpc_health.v1 import health_pb2
from grpc_health.v1 import health_pb2_grpc

import framework.rpc
from src.proto.grpc.testing import empty_pb2
from src.proto.grpc.testing import messages_pb2
from src.proto.grpc.testing import test_pb2_grpc

# Type aliases
_LoadBalancerStatsRequest = messages_pb2.LoadBalancerStatsRequest
LoadBalancerStatsResponse = messages_pb2.LoadBalancerStatsResponse
_LoadBalancerAccumulatedStatsRequest = messages_pb2.LoadBalancerAccumulatedStatsRequest
LoadBalancerAccumulatedStatsResponse = messages_pb2.LoadBalancerAccumulatedStatsResponse
MethodStats = messages_pb2.LoadBalancerAccumulatedStatsResponse.MethodStats
RpcsByPeer = messages_pb2.LoadBalancerStatsResponse.RpcsByPeer


class LoadBalancerStatsServiceClient(framework.rpc.grpc.GrpcClientHelper):
    stub: test_pb2_grpc.LoadBalancerStatsServiceStub
    STATS_PARTIAL_RESULTS_TIMEOUT_SEC = 1200
    STATS_ACCUMULATED_RESULTS_TIMEOUT_SEC = 600

    def __init__(self,
                 channel: grpc.Channel,
                 *,
                 log_target: Optional[str] = ''):
        super().__init__(channel,
                         test_pb2_grpc.LoadBalancerStatsServiceStub,
                         log_target=log_target)

    def get_client_stats(
        self,
        *,
        num_rpcs: int,
        timeout_sec: Optional[int] = STATS_PARTIAL_RESULTS_TIMEOUT_SEC,
    ) -> LoadBalancerStatsResponse:
        if timeout_sec is None:
            timeout_sec = self.STATS_PARTIAL_RESULTS_TIMEOUT_SEC

        return self.call_unary_with_deadline(rpc='GetClientStats',
                                             req=_LoadBalancerStatsRequest(
                                                 num_rpcs=num_rpcs,
                                                 timeout_sec=timeout_sec),
                                             deadline_sec=timeout_sec,
                                             log_level=logging.INFO)

    def get_client_accumulated_stats(
        self,
        *,
        timeout_sec: Optional[int] = None
    ) -> LoadBalancerAccumulatedStatsResponse:
        if timeout_sec is None:
            timeout_sec = self.STATS_ACCUMULATED_RESULTS_TIMEOUT_SEC

        return self.call_unary_with_deadline(
            rpc='GetClientAccumulatedStats',
            req=_LoadBalancerAccumulatedStatsRequest(),
            deadline_sec=timeout_sec,
            log_level=logging.INFO)


class XdsUpdateClientConfigureServiceClient(framework.rpc.grpc.GrpcClientHelper
                                           ):
    stub: test_pb2_grpc.XdsUpdateClientConfigureServiceStub
    CONFIGURE_TIMEOUT_SEC: int = 5

    def __init__(self,
                 channel: grpc.Channel,
                 *,
                 log_target: Optional[str] = ''):
        super().__init__(channel,
                         test_pb2_grpc.XdsUpdateClientConfigureServiceStub,
                         log_target=log_target)

    def configure(
        self,
        *,
        rpc_types: Iterable[str],
        metadata: Optional[Iterable[Tuple[str, str, str]]] = None,
        app_timeout: Optional[int] = None,
        timeout_sec: int = CONFIGURE_TIMEOUT_SEC,
    ) -> None:
        request = messages_pb2.ClientConfigureRequest()
        for rpc_type in rpc_types:
            request.types.append(
                messages_pb2.ClientConfigureRequest.RpcType.Value(rpc_type))
        if metadata:
            for entry in metadata:
                request.metadata.append(
                    messages_pb2.ClientConfigureRequest.Metadata(
                        type=messages_pb2.ClientConfigureRequest.RpcType.Value(
                            entry[0]),
                        key=entry[1],
                        value=entry[2],
                    ))
        if app_timeout:
            request.timeout_sec = app_timeout
        # Configure's response is empty
        self.call_unary_with_deadline(rpc='Configure',
                                      req=request,
                                      deadline_sec=timeout_sec,
                                      log_level=logging.INFO)


class XdsUpdateHealthServiceClient(framework.rpc.grpc.GrpcClientHelper):
    stub: test_pb2_grpc.XdsUpdateHealthServiceStub

    def __init__(self, channel: grpc.Channel, log_target: Optional[str] = ''):
        super().__init__(channel,
                         test_pb2_grpc.XdsUpdateHealthServiceStub,
                         log_target=log_target)

    def set_serving(self):
        self.call_unary_with_deadline(rpc='SetServing',
                                      req=empty_pb2.Empty(),
                                      log_level=logging.INFO)

    def set_not_serving(self):
        self.call_unary_with_deadline(rpc='SetNotServing',
                                      req=empty_pb2.Empty(),
                                      log_level=logging.INFO)


class HealthClient(framework.rpc.grpc.GrpcClientHelper):
    stub: health_pb2_grpc.HealthStub

    def __init__(self, channel: grpc.Channel, log_target: Optional[str] = ''):
        super().__init__(channel,
                         health_pb2_grpc.HealthStub,
                         log_target=log_target)

    def check_health(self):
        return self.call_unary_with_deadline(
            rpc='Check',
            req=health_pb2.HealthCheckRequest(),
            log_level=logging.INFO)

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
from typing import Optional

import grpc

import framework.rpc
from src.proto.grpc.testing import test_pb2_grpc
from src.proto.grpc.testing import messages_pb2

# Type aliases
_LoadBalancerStatsRequest = messages_pb2.LoadBalancerStatsRequest
LoadBalancerStatsResponse = messages_pb2.LoadBalancerStatsResponse


class LoadBalancerStatsServiceClient(framework.rpc.grpc.GrpcClientHelper):
    stub: test_pb2_grpc.LoadBalancerStatsServiceStub
    STATS_PARTIAL_RESULTS_TIMEOUT_SEC = 1200

    def __init__(self, channel: grpc.Channel):
        super().__init__(channel, test_pb2_grpc.LoadBalancerStatsServiceStub)

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

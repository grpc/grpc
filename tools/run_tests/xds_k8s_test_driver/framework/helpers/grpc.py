# Copyright 2023 gRPC authors.
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
"""This contains common helpers for working with grpc data structures."""
import dataclasses
import functools
import json
from typing import Dict, List, Optional

import grpc
import yaml

from framework.rpc import grpc_testing


@functools.cache  # pylint: disable=no-member
def status_from_int(grpc_status_int: int) -> Optional[grpc.StatusCode]:
    """Converts the integer gRPC status code to the grpc.StatusCode enum."""
    for grpc_status in grpc.StatusCode:
        if grpc_status.value[0] == grpc_status_int:
            return grpc_status
    return None


def status_eq(grpc_status_int: int, grpc_status: grpc.StatusCode) -> bool:
    """Compares the integer gRPC status code with the grpc.StatusCode enum."""
    return status_from_int(grpc_status_int) is grpc_status


def status_pretty(grpc_status: grpc.StatusCode) -> str:
    """Formats the status code as (int, NAME), f.e. (4, DEADLINE_EXCEEDED)"""
    return f"({grpc_status.value[0]}, {grpc_status.name})"


@dataclasses.dataclass(frozen=True)
class PrettyStatsPerMethod:
    method: str
    rpcs_started: int
    result: Dict[str, int]

    @staticmethod
    def from_response(
            method_name: str,
            method_stats: grpc_testing.MethodStats) -> "PrettyStatsPerMethod":
        stats: Dict[str, int] = dict()
        for status_int, count in method_stats.result.items():
            status: Optional[grpc.StatusCode] = status_from_int(status_int)
            status_formatted = status_pretty(status) if status else "None"
            stats[status_formatted] = count
        return PrettyStatsPerMethod(
            method=method_name,
            rpcs_started=method_stats.rpcs_started,
            result=stats,
        )


def accumulated_stats_pretty(
    accumulated_stats: grpc_testing.LoadBalancerAccumulatedStatsResponse
) -> str:
    """Pretty print LoadBalancerAccumulatedStatsResponse.

    Example:
      - method: EMPTY
        rpcs_started: 0
        result:
          (2, UNKNOWN): 20
      - method: UNARY
        rpcs_started: 31
        result:
          (0, OK): 10
          (14, UNAVAILABLE): 20
    """
    # Only look at stats_per_method, as the other fields are deprecated.
    result: List[Dict] = []
    for method_name, method_stats in accumulated_stats.stats_per_method.items():
        pretty_stats = PrettyStatsPerMethod.from_response(
            method_name, method_stats)
        result.append(dataclasses.asdict(pretty_stats))

    return yaml.dump(result, sort_keys=False)


def lb_stats_pretty(lb: grpc_testing.LoadBalancerStatsResponse) -> str:
    """Pretty print LoadBalancerStatsResponse.

    Example:
      - method: EMPTY
        rpcs_started: 0
        result:
          (2, UNKNOWN): 20
      - method: UNARY
        rpcs_started: 31
        result:
          (0, OK): 10
          (14, UNAVAILABLE): 20
    """
    # tbd
    return ""

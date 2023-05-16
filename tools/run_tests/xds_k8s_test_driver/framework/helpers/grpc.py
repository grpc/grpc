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
import functools
from typing import Optional

import grpc


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

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

from __future__ import annotations

import abc
from dataclasses import dataclass
from dataclasses import field
import enum
from typing import List, Mapping, Tuple

from grpc_observability import _cyobservability
from grpc_observability._observability import GCPOpenCensusObservability


class Exporter(metaclass=abc.ABCMeta):

    @abc.abstractmethod
    def export_stats_data(self, stats_data: List[TracingData]) -> None:
        raise NotImplementedError()

    @abc.abstractmethod
    def export_tracing_data(self, tracing_data: List[StatsData]) -> None:
        raise NotImplementedError()


@enum.unique
class MetricsName(enum.Enum):
    CLIENT_STARTED_RPCS = (_cyobservability.MetricsName.CLIENT_STARTED_RPCS,
                           'CLIENT_STARTED_RPCS')
    CLIENT_API_LATENCY = (_cyobservability.MetricsName.CLIENT_API_LATENCY,
                          'CLIENT_API_LATENCY')
    CLIENT_SNET_MESSSAGES_PER_RPC = (
        _cyobservability.MetricsName.CLIENT_SNET_MESSSAGES_PER_RPC,
        'CLIENT_API_LATENCY')
    CLIENT_SEND_BYTES_PER_RPC = (
        _cyobservability.MetricsName.CLIENT_SEND_BYTES_PER_RPC,
        'CLIENT_SEND_BYTES_PER_RPC')
    CLIENT_RECEIVED_MESSAGES_PER_RPC = (
        _cyobservability.MetricsName.CLIENT_RECEIVED_MESSAGES_PER_RPC,
        'CLIENT_RECEIVED_MESSAGES_PER_RPC')
    CLIENT_RECEIVED_BYTES_PER_RPC = (
        _cyobservability.MetricsName.CLIENT_RECEIVED_BYTES_PER_RPC,
        'CLIENT_RECEIVED_BYTES_PER_RPC')
    CLIENT_ROUNDTRIP_LATENCY = (
        _cyobservability.MetricsName.CLIENT_ROUNDTRIP_LATENCY,
        'CLIENT_ROUNDTRIP_LATENCY')
    CLIENT_SERVER_LATENCY = (_cyobservability.MetricsName.CLIENT_SERVER_LATENCY,
                             'CLIENT_SERVER_LATENCY')
    CLIENT_RETRIES_PER_CALL = (
        _cyobservability.MetricsName.CLIENT_RETRIES_PER_CALL,
        'CLIENT_RETRIES_PER_CALL')
    CLIENT_TRANSPARENT_RETRIES_PER_CALL = (
        _cyobservability.MetricsName.CLIENT_TRANSPARENT_RETRIES_PER_CALL,
        'CLIENT_TRANSPARENT_RETRIES_PER_CALL')
    CLIENT_RETRY_DELAY_PER_CALL = (
        _cyobservability.MetricsName.CLIENT_RETRY_DELAY_PER_CALL,
        'CLIENT_RETRY_DELAY_PER_CALL')
    CLIENT_TRANSPORT_LATENCY = (
        _cyobservability.MetricsName.CLIENT_TRANSPORT_LATENCY,
        'CLIENT_TRANSPORT_LATENCY')
    SERVER_SENT_MESSAGES_PER_RPC = (
        _cyobservability.MetricsName.SERVER_SENT_MESSAGES_PER_RPC,
        'SERVER_SENT_MESSAGES_PER_RPC')
    SERVER_SENT_BYTES_PER_RPC = (
        _cyobservability.MetricsName.SERVER_SENT_BYTES_PER_RPC,
        'SERVER_SENT_BYTES_PER_RPC')
    SERVER_RECEIVED_MESSAGES_PER_RPC = (
        _cyobservability.MetricsName.SERVER_RECEIVED_MESSAGES_PER_RPC,
        'SERVER_RECEIVED_MESSAGES_PER_RPC')
    SERVER_RECEIVED_BYTES_PER_RPC = (
        _cyobservability.MetricsName.SERVER_RECEIVED_BYTES_PER_RPC,
        'SERVER_RECEIVED_BYTES_PER_RPC')
    SERVER_SERVER_LATENCY = (_cyobservability.MetricsName.SERVER_SERVER_LATENCY,
                             'SERVER_SERVER_LATENCY')
    SERVER_STARTED_RPCS = (_cyobservability.MetricsName.SERVER_STARTED_RPCS,
                           'SERVER_STARTED_RPCS')


@dataclass(frozen=True)
class StatsData:
    name: MetricsName
    measure_double: bool
    value_int: int = 0
    value_float: float = 0.0
    labels: Mapping[str, str] = field(default_factory=dict)


@dataclass(frozen=True)
class TracingData:
    name: str
    start_time: str
    end_time: str
    trace_id: str
    span_id: str
    parent_span_id: str
    status: str
    should_sample: bool
    child_span_count: int
    span_labels: Mapping[str, str] = field(default_factory=dict)
    span_annotations: List[Tuple[str, str]] = field(default_factory=list)


__all__ = ('GCPOpenCensusObservability', 'Exporter', 'MetricsName', 'StatsData',
           'TracingData')

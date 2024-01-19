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
from typing import List, Mapping, Tuple


class Exporter(metaclass=abc.ABCMeta):
    """Abstract base class for census data exporters."""

    @abc.abstractmethod
    def export_stats_data(self, stats_data: List[TracingData]) -> None:
        """Exports a list of TracingData objects to the exporter's destination.

        Args:
          stats_data: A list of TracingData objects to export.
        """
        raise NotImplementedError()

    @abc.abstractmethod
    def export_tracing_data(self, tracing_data: List[StatsData]) -> None:
        """Exports a list of StatsData objects to the exporter's destination.

        Args:
          tracing_data: A list of StatsData objects to export.
        """
        raise NotImplementedError()


@dataclass(frozen=True)
class StatsData:
    """A data class representing stats data.

    Attributes:
      name: An element of grpc_observability._cyobservability.MetricsName, e.g.
        MetricsName.CLIENT_STARTED_RPCS.
      measure_double: A bool indicate whether the metric is a floating-point
        value.
      value_int: The actual metric value if measure_double is False.
      value_float: The actual metric value if measure_double is True.
      labels: A dictionary that maps label tags associated with this metric to
       corresponding label value.
    """

    name: "grpc_observability._cyobservability.MetricsName"
    measure_double: bool
    value_int: int = 0
    value_float: float = 0.0
    labels: Mapping[str, str] = field(default_factory=dict)


@dataclass(frozen=True)
class TracingData:
    """A data class representing tracing data.

    Attributes:
      name: The name for tracing data, also the name for the Span.
      start_time: The start time for the span in RFC3339 UTC "Zulu" format, e.g.
       2014-10-02T15:01:23Z
      end_time: The end time for the span in RFC3339 UTC "Zulu" format, e.g.
       2014-10-02T15:01:23Z
      trace_id: The identifier for the trace associated with this span as a
       32-character hexadecimal encoded string,
       e.g. 26ed0036f2eff2b7317bccce3e28d01f
      span_id: The identifier for the span as a 16-character hexadecimal encoded
       string. e.g. 113ec879e62583bc
      parent_span_id: An option identifier for the span's parent id.
      status: An element of grpc.StatusCode in string format representing the
       final status for the trace data.
      should_sample: A bool indicates whether the span is sampled.
      child_span_count: The number of child span associated with this span.
      span_labels: A dictionary that maps labels tags associated with this
       span to corresponding label value.
      span_annotations: A dictionary that maps annotation timeStamp with
       description. The timeStamp have a format which can be converted
       to Python datetime.datetime, e.g. 2023-05-29 17:07:09.895
    """

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

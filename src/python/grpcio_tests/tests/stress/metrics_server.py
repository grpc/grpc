# Copyright 2016 gRPC authors.
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
"""MetricsService for publishing stress test qps data."""

import time

from src.proto.grpc.testing import metrics_pb2
from src.proto.grpc.testing import metrics_pb2_grpc

GAUGE_NAME = "python_overall_qps"


class MetricsServer(metrics_pb2_grpc.MetricsServiceServicer):
    def __init__(self, histogram):
        self._start_time = time.time()
        self._histogram = histogram

    def _get_qps(self):
        count = self._histogram.get_data().count
        delta = time.time() - self._start_time
        self._histogram.reset()
        self._start_time = time.time()
        return int(count / delta)

    def GetAllGauges(self, request, context):
        qps = self._get_qps()
        return [metrics_pb2.GaugeResponse(name=GAUGE_NAME, long_value=qps)]

    def GetGauge(self, request, context):
        if request.name != GAUGE_NAME:
            raise Exception("Gauge {} does not exist".format(request.name))
        qps = self._get_qps()
        return metrics_pb2.GaugeResponse(name=GAUGE_NAME, long_value=qps)

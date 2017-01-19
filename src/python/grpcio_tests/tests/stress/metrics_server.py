# Copyright 2016, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
"""MetricsService for publishing stress test qps data."""

import time

from src.proto.grpc.testing import metrics_pb2

GAUGE_NAME = 'python_overall_qps'


class MetricsServer(metrics_pb2.MetricsServiceServicer):

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
            raise Exception('Gauge {} does not exist'.format(request.name))
        qps = self._get_qps()
        return metrics_pb2.GaugeResponse(name=GAUGE_NAME, long_value=qps)

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

import sys
import logging
import views
import collections
from typing import AnyStr, Optional, List, Tuple, Mapping
from datetime import datetime

from opencensus.tags.tag_key import TagKey

logger = logging.getLogger(__name__)

_Label = collections.namedtuple('_Label', (
    'key',
    'tag_key',
    'value',
))

class gcpObservabilityConfig:
    _singleton = None

    def __init__(self):
        self.project_id = ""
        self.monitoring_enabled = False
        self.tracing_enabled = False
        self.labels = []
        self.sampler = None
        self.sampling_rate = 0.0
        self.tracer = None

    @staticmethod
    def get():
        if gcpObservabilityConfig._singleton is None:
            gcpObservabilityConfig._singleton = gcpObservabilityConfig()
        return gcpObservabilityConfig._singleton

    def set_configuration(self,
                          project_id,
                          sampling_rate=0.0,
                          labels=None,
                          tracing_enabled=False,
                          monitoring_enabled=False):
        self.project_id = project_id
        self.monitoring_enabled = monitoring_enabled
        self.tracing_enabled = tracing_enabled
        self.labels = []
        self.sampling_rate = sampling_rate
        for key, value in labels.items():
            self.labels.append(_Label(key, TagKey(key), value))


def export_metric_batch(py_metrics_batch: list) -> None:
    pass


def export_span_batch(py_span_batch: list) -> None:
    pass

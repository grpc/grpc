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

from dataclasses import dataclass
from dataclasses import field
import logging
import threading
import time
from typing import Any, Mapping, Optional

import grpc
from grpc_observability import _cyobservability  # pytype: disable=pyi-error
from opencensus.trace import execution_context
from opencensus.trace import span_context as span_context_module
from opencensus.trace import trace_options as trace_options_module

_LOGGER = logging.getLogger(__name__)

ClientCallTracerCapsule = Any  # it appears only once in the function signature
ServerCallTracerFactoryCapsule = Any  # it appears only once in the function signature
grpc_observability = Any  # grpc_observability.py imports this module.

GRPC_STATUS_CODE_TO_STRING = {
    grpc.StatusCode.OK: "OK",
    grpc.StatusCode.CANCELLED: "CANCELLED",
    grpc.StatusCode.UNKNOWN: "UNKNOWN",
    grpc.StatusCode.INVALID_ARGUMENT: "INVALID_ARGUMENT",
    grpc.StatusCode.DEADLINE_EXCEEDED: "DEADLINE_EXCEEDED",
    grpc.StatusCode.NOT_FOUND: "NOT_FOUND",
    grpc.StatusCode.ALREADY_EXISTS: "ALREADY_EXISTS",
    grpc.StatusCode.PERMISSION_DENIED: "PERMISSION_DENIED",
    grpc.StatusCode.UNAUTHENTICATED: "UNAUTHENTICATED",
    grpc.StatusCode.RESOURCE_EXHAUSTED: "RESOURCE_EXHAUSTED",
    grpc.StatusCode.FAILED_PRECONDITION: "FAILED_PRECONDITION",
    grpc.StatusCode.ABORTED: "ABORTED",
    grpc.StatusCode.OUT_OF_RANGE: "OUT_OF_RANGE",
    grpc.StatusCode.UNIMPLEMENTED: "UNIMPLEMENTED",
    grpc.StatusCode.INTERNAL: "INTERNAL",
    grpc.StatusCode.UNAVAILABLE: "UNAVAILABLE",
    grpc.StatusCode.DATA_LOSS: "DATA_LOSS",
}


@dataclass
class GcpObservabilityPythonConfig:
    _singleton = None
    _lock: threading.RLock = threading.RLock()
    project_id: str = ""
    stats_enabled: bool = False
    tracing_enabled: bool = False
    labels: Optional[Mapping[str, str]] = field(default_factory=dict)
    sampling_rate: Optional[float] = 0.0

    @staticmethod
    def get():
        with GcpObservabilityPythonConfig._lock:
            if GcpObservabilityPythonConfig._singleton is None:
                GcpObservabilityPythonConfig._singleton = GcpObservabilityPythonConfig(
                )
        return GcpObservabilityPythonConfig._singleton

    def set_configuration(self,
                          project_id: str,
                          sampling_rate: Optional[float] = 0.0,
                          labels: Optional[Mapping[str, str]] = None,
                          tracing_enabled: bool = False,
                          stats_enabled: bool = False) -> None:
        self.project_id = project_id
        self.stats_enabled = stats_enabled
        self.tracing_enabled = tracing_enabled
        self.labels = labels
        self.sampling_rate = sampling_rate


# pylint: disable=no-self-use
class GCPOpenCensusObservability(grpc.ObservabilityPlugin):
    config: GcpObservabilityPythonConfig
    exporter: "grpc_observability.Exporter"

    def __init__(self):
        # 1. Read config.
        self.exporter = None
        self.config = GcpObservabilityPythonConfig.get()
        config_valid = _cyobservability.set_gcp_observability_config(
            self.config)
        if not config_valid:
            raise ValueError("Invalid configuration")

        if self.config.tracing_enabled:
            self.enable_tracing(True)
        if self.config.stats_enabled:
            self.enable_stats(True)

    def init(self, exporter: "grpc_observability.Exporter" = None) -> None:
        if exporter:
            self.exporter = exporter
        else:
            # 2. Creating measures and register views.
            # 3. Create and Saves Tracer and Sampler to ContextVar.
            pass  # Actual implementation of OC exporter
            # open_census = importlib.import_module(
            #     "grpc_observability._open_census")
            # self.exporter = open_census.OpenCensusExporter(
            #     self.config.get().labels)

        # 4. Start exporting thread.
        try:
            _cyobservability.cyobservability_init(self.exporter)
        #TODO(xuanwn): Use specific exceptons
        except Exception as e:  # pylint: disable=broad-except
            _LOGGER.exception("grpc_observability init failed with: %s", e)

        # 5. Init grpc.
        # 5.1 Refister grpc_observability
        # 5.2 set_server_call_tracer_factory
        grpc._observability_init(self)

    def exit(self) -> None:
        # Sleep for 0.5s so all data can be flushed.
        time.sleep(0.5)
        self.enable_tracing(False)
        self.enable_stats(False)
        _cyobservability.at_observability_exit()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb) -> None:
        self.exit()

    def create_client_call_tracer(
            self, method_name: bytes) -> ClientCallTracerCapsule:
        current_span = execution_context.get_current_span()
        if current_span:
            # Propagate existing OC context
            trace_id = current_span.context_tracer.trace_id.encode('utf8')
            parent_span_id = current_span.span_id.encode('utf8')
            capsule = _cyobservability.create_client_call_tracer(
                method_name, trace_id, parent_span_id)
        else:
            trace_id = span_context_module.generate_trace_id().encode('utf8')
            capsule = _cyobservability.create_client_call_tracer(
                method_name, trace_id)
        return capsule

    def create_server_call_tracer_factory(
            self) -> ServerCallTracerFactoryCapsule:
        capsule = _cyobservability.create_server_call_tracer_factory_capsule()
        return capsule

    def delete_client_call_tracer(
            self, client_call_tracer: ClientCallTracerCapsule) -> None:
        _cyobservability.delete_client_call_tracer(client_call_tracer)

    def save_trace_context(self, trace_id: str, span_id: str,
                           is_sampled: bool) -> None:
        trace_options = trace_options_module.TraceOptions(0)
        trace_options.set_enabled(is_sampled)
        span_context = span_context_module.SpanContext(
            trace_id=trace_id, span_id=span_id, trace_options=trace_options)
        current_tracer = execution_context.get_opencensus_tracer()
        current_tracer.span_context = span_context

    def record_rpc_latency(self, method: str, rpc_latency: float,
                           status_code: grpc.StatusCode) -> None:
        status_code = GRPC_STATUS_CODE_TO_STRING.get(status_code, "UNKNOWN")
        _cyobservability._record_rpc_latency(self.exporter, method, rpc_latency,
                                             status_code)

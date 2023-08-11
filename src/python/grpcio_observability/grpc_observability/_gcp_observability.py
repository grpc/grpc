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

import logging
import time
from typing import Any

import grpc

# pytype: disable=pyi-error
from grpc_observability import _cyobservability
from grpc_observability import _observability_config

# pytype: enable=pyi-error
from grpc_observability._open_census_exporter import CENSUS_UPLOAD_INTERVAL_SECS
from grpc_observability._open_census_exporter import OpenCensusExporter
from opencensus.trace import execution_context
from opencensus.trace import span_context as span_context_module
from opencensus.trace import trace_options as trace_options_module

_LOGGER = logging.getLogger(__name__)

ClientCallTracerCapsule = Any  # it appears only once in the function signature
ServerCallTracerFactoryCapsule = (
    Any  # it appears only once in the function signature
)
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

GRPC_SPAN_CONTEXT = "grpc_span_context"


# pylint: disable=no-self-use
class GCPOpenCensusObservability(grpc._observability.ObservabilityPlugin):
    """GCP OpenCensus based plugin implementation.

    If no exporter is passed, the default will be OpenCensus StackDriver
    based exporter.

    For more details, please refer to User Guide:
      * https://cloud.google.com/stackdriver/docs/solutions/grpc

    Attributes:
      config: Configuration for GCP OpenCensus Observability.
      exporter: Exporter used to export data.
    """

    config: _observability_config.GcpObservabilityConfig
    exporter: "grpc_observability.Exporter"
    use_open_census_exporter: bool

    def __init__(self, exporter: "grpc_observability.Exporter" = None):
        self.exporter = None
        self.config = _observability_config.GcpObservabilityConfig.get()
        self.use_open_census_exporter = False
        try:
            self.config = _observability_config.read_config()
            _cyobservability.activate_config(self.config)
        except Exception as e:  # pylint: disable=broad-except
            raise ValueError(f"Read configuration failed with: {e}")

        if exporter:
            self.exporter = exporter
        else:
            self.exporter = OpenCensusExporter(self.config)
            self.use_open_census_exporter = True

        if self.config.tracing_enabled:
            self.set_tracing(True)
        if self.config.stats_enabled:
            self.set_stats(True)

    def __enter__(self):
        try:
            _cyobservability.cyobservability_init(self.exporter)
        # TODO(xuanwn): Use specific exceptons
        except Exception as e:  # pylint: disable=broad-except
            _LOGGER.exception("GCPOpenCensusObservability failed with: %s", e)

        grpc._observability.observability_init(self)
        return self

    def __exit__(self, exc_type, exc_val, exc_tb) -> None:
        self.exit()

    def exit(self) -> None:
        # Sleep so we don't loss any data. If we shutdown export thread
        # immediately after exit, it's possible that core didn't call RecordEnd
        # in callTracer, and all data recorded by calling RecordEnd will be
        # lost.
        # CENSUS_EXPORT_BATCH_INTERVAL_SECS: The time equals to the time in
        # AwaitNextBatchLocked.
        # TODO(xuanwn): explicit synchronization
        # https://github.com/grpc/grpc/issues/33262
        time.sleep(_cyobservability.CENSUS_EXPORT_BATCH_INTERVAL_SECS)
        if self.use_open_census_exporter:
            # Sleep so StackDriver can upload data to GCP.
            time.sleep(CENSUS_UPLOAD_INTERVAL_SECS)
        self.set_tracing(False)
        self.set_stats(False)
        _cyobservability.observability_deinit()
        grpc._observability.observability_deinit()

    def create_client_call_tracer(
        self, method_name: bytes
    ) -> ClientCallTracerCapsule:
        grpc_span_context = execution_context.get_opencensus_attr(
            GRPC_SPAN_CONTEXT
        )
        if grpc_span_context:
            trace_id = grpc_span_context.trace_id.encode("utf8")
            parent_span_id = grpc_span_context.span_id.encode("utf8")
            capsule = _cyobservability.create_client_call_tracer(
                method_name, trace_id, parent_span_id
            )
        else:
            trace_id = span_context_module.generate_trace_id().encode("utf8")
            capsule = _cyobservability.create_client_call_tracer(
                method_name, trace_id
            )
        return capsule

    def create_server_call_tracer_factory(
        self,
    ) -> ServerCallTracerFactoryCapsule:
        capsule = _cyobservability.create_server_call_tracer_factory_capsule()
        return capsule

    def delete_client_call_tracer(
        self, client_call_tracer: ClientCallTracerCapsule
    ) -> None:
        _cyobservability.delete_client_call_tracer(client_call_tracer)

    def save_trace_context(
        self, trace_id: str, span_id: str, is_sampled: bool
    ) -> None:
        trace_options = trace_options_module.TraceOptions(0)
        trace_options.set_enabled(is_sampled)
        span_context = span_context_module.SpanContext(
            trace_id=trace_id,
            span_id=span_id,
            trace_options=trace_options,
        )
        execution_context.set_opencensus_attr(GRPC_SPAN_CONTEXT, span_context)

    def record_rpc_latency(
        self, method: str, rpc_latency: float, status_code: grpc.StatusCode
    ) -> None:
        status_code = GRPC_STATUS_CODE_TO_STRING.get(status_code, "UNKNOWN")
        _cyobservability._record_rpc_latency(
            self.exporter, method, rpc_latency, status_code
        )

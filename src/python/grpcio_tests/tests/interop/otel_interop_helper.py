# Copyright 2026 gRPC authors.
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
"""OpenTelemetry Tracing Interop Helper for Python gRPC Interop Client/Server"""

import os
from typing import Optional, Tuple

import grpc
from opentelemetry import trace
from opentelemetry.proto.collector.trace.v1 import trace_service_pb2
from opentelemetry.proto.collector.trace.v1 import trace_service_pb2_grpc
from opentelemetry.proto.common.v1 import common_pb2
from opentelemetry.proto.trace.v1 import trace_pb2
from opentelemetry.sdk.trace import ReadableSpan
from opentelemetry.sdk.trace import TracerProvider
from opentelemetry.sdk.trace.export import SimpleSpanProcessor
from opentelemetry.sdk.trace.export import SpanExporter


class OTLPSpanExporter(SpanExporter):
    """Exporter that sends OTLP spans to OTLP Collector over gRPC."""

    def __init__(self, endpoint: str):
        if endpoint.startswith("http://"):
            endpoint = endpoint[7:]
        elif endpoint.startswith("https://"):
            endpoint = endpoint[8:]
        self._channel = grpc.insecure_channel(endpoint)
        self._stub = trace_service_pb2_grpc.TraceServiceStub(self._channel)

    def export(self, spans: Tuple[ReadableSpan, ...]) -> None:
        if not spans:
            return

        otlp_spans = []
        for span in spans:
            ctx = span.context
            parent_ctx = span.parent

            trace_id_bytes = ctx.trace_id.to_bytes(16, "big")
            span_id_bytes = ctx.span_id.to_bytes(8, "big")
            parent_span_id_bytes = (
                parent_ctx.span_id.to_bytes(8, "big")
                if parent_ctx and parent_ctx.span_id
                else b""
            )

            proto_attributes = []
            if span.attributes:
                for k, v in span.attributes.items():
                    kv = common_pb2.KeyValue(key=k)
                    if isinstance(v, bool):
                        kv.value.bool_value = v
                    elif isinstance(v, int):
                        kv.value.int_value = v
                    elif isinstance(v, float):
                        kv.value.double_value = v
                    else:
                        kv.value.string_value = str(v)
                    proto_attributes.append(kv)

            proto_events = []
            if span.events:
                for event in span.events:
                    e = trace_pb2.Span.Event(
                        name=event.name,
                        time_unix_nano=event.timestamp,
                    )
                    proto_events.append(e)

            kind = (
                trace_pb2.Span.SpanKind.SPAN_KIND_CLIENT
                if span.kind == trace.SpanKind.CLIENT
                else trace_pb2.Span.SpanKind.SPAN_KIND_SERVER
            )

            proto_span = trace_pb2.Span(
                trace_id=trace_id_bytes,
                span_id=span_id_bytes,
                parent_span_id=parent_span_id_bytes,
                name=span.name,
                kind=kind,
                start_time_unix_nano=span.start_time,
                end_time_unix_nano=span.end_time,
                attributes=proto_attributes,
                events=proto_events,
            )
            otlp_spans.append(proto_span)

        scope_spans = trace_pb2.ScopeSpans(spans=otlp_spans)
        resource_spans = trace_pb2.ResourceSpans(scope_spans=[scope_spans])
        request = trace_service_pb2.ExportTraceServiceRequest(
            resource_spans=[resource_spans]
        )

        try:
            self._stub.Export(request, timeout=5)
        except Exception as e:
            print(f"OTLPSpanExporter Export exception: {e}", flush=True)

    def shutdown(self) -> None:
        self._channel.close()

    def force_flush(self, timeout_millis: int = 30000) -> bool:
        return True


_GLOBAL_PROVIDER: Optional[TracerProvider] = None


def init_tracer_provider() -> Tuple[TracerProvider, trace.Tracer]:
    # pylint: disable=global-statement
    global _GLOBAL_PROVIDER
    if _GLOBAL_PROVIDER is None:
        endpoint = os.environ.get(
            "OTEL_EXPORTER_OTLP_ENDPOINT", "http://localhost:4317"
        )
        exporter = OTLPSpanExporter(endpoint)
        processor = SimpleSpanProcessor(exporter)
        _GLOBAL_PROVIDER = TracerProvider()
        _GLOBAL_PROVIDER.add_span_processor(processor)
        trace.set_tracer_provider(_GLOBAL_PROVIDER)
    tracer = trace.get_tracer("grpc-python-interop")
    return _GLOBAL_PROVIDER, tracer


def flush_tracer_provider():
    if _GLOBAL_PROVIDER:
        _GLOBAL_PROVIDER.force_flush()


def pack_grpc_trace_bin(
    trace_id_int: int, span_id_int: int, is_sampled: bool = True
) -> bytes:
    trace_id_bytes = trace_id_int.to_bytes(16, "big")
    span_id_bytes = span_id_int.to_bytes(8, "big")
    options = 1 if is_sampled else 0
    return (
        b"\x00\x00"
        + trace_id_bytes
        + b"\x01"
        + span_id_bytes
        + b"\x02"
        + bytes([options])
    )


def unpack_grpc_trace_bin(
    header_bytes: bytes,
) -> Tuple[Optional[int], Optional[int], bool]:
    if len(header_bytes) >= 29 and header_bytes[0] == 0:
        trace_id_int = int.from_bytes(header_bytes[2:18], "big")
        span_id_int = int.from_bytes(header_bytes[19:27], "big")
        is_sampled = bool(header_bytes[28] & 1)
        return trace_id_int, span_id_int, is_sampled
    return None, None, False


def parse_traceparent(
    header_str: str,
) -> Tuple[Optional[int], Optional[int], bool]:
    parts = header_str.split("-")
    if len(parts) >= 4 and parts[0] == "00":
        try:
            trace_id_int = int(parts[1], 16)
            span_id_int = int(parts[2], 16)
            is_sampled = (int(parts[3], 16) & 1) != 0
            return trace_id_int, span_id_int, is_sampled
        except ValueError:
            pass
    return None, None, False


class OTelServerInterceptor(grpc.ServerInterceptor):
    """Server interceptor to extract trace context and create Recv span."""

    def __init__(self, tracer: trace.Tracer):
        self._tracer = tracer

    def intercept_service(self, continuation, handler_call_details):
        trace_bin_header = None
        traceparent_header = None
        for k, v in handler_call_details.invocation_metadata:
            k_str = (
                k.decode("ascii", errors="ignore")
                if isinstance(k, bytes)
                else str(k)
            )
            if k_str.lower() == "grpc-trace-bin":
                trace_bin_header = v
            elif k_str.lower() == "traceparent":
                traceparent_header = (
                    v if isinstance(v, str) else v.decode("latin1")
                )

        parent_ctx = None
        trace_id, parent_span_id, is_sampled = None, None, False

        if trace_bin_header:
            if isinstance(trace_bin_header, str):
                trace_bin_header = trace_bin_header.encode("latin1")
            (
                trace_id,
                parent_span_id,
                is_sampled,
            ) = unpack_grpc_trace_bin(trace_bin_header)
        elif traceparent_header:
            (
                trace_id,
                parent_span_id,
                is_sampled,
            ) = parse_traceparent(traceparent_header)

        if trace_id and parent_span_id:
            parent_ctx = trace.SpanContext(
                trace_id=trace_id,
                span_id=parent_span_id,
                is_remote=True,
                trace_flags=trace.TraceFlags(1 if is_sampled else 0),
            )

        method = handler_call_details.method
        full_method = method.lstrip("/")
        span_name = f"Recv.{full_method}"

        if parent_ctx:
            ctx = trace.set_span_in_context(trace.NonRecordingSpan(parent_ctx))
            server_span = self._tracer.start_span(
                span_name, kind=trace.SpanKind.SERVER, context=ctx
            )
        else:
            server_span = self._tracer.start_span(
                span_name, kind=trace.SpanKind.SERVER
            )

        server_span.add_event("Inbound message")

        handler = continuation(handler_call_details)

        if handler is None:
            server_span.end()
            return None

        if handler.unary_unary:
            orig_func = handler.unary_unary

            def wrapper(request, context):
                try:
                    res = orig_func(request, context)
                    server_span.add_event("Outbound message")
                    return res
                finally:
                    server_span.end()
                    flush_tracer_provider()

            return grpc.unary_unary_rpc_method_handler(
                wrapper,
                request_deserializer=handler.request_deserializer,
                response_serializer=handler.response_serializer,
            )

        return handler

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

import base64
import os
import struct
import time
from typing import Optional, Tuple, Union

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
from opentelemetry.sdk.trace.export import SpanExportResult

_SPAN_KIND_MAP = {
    trace.SpanKind.CLIENT: trace_pb2.Span.SpanKind.SPAN_KIND_CLIENT,
    trace.SpanKind.SERVER: trace_pb2.Span.SpanKind.SPAN_KIND_SERVER,
    trace.SpanKind.INTERNAL: trace_pb2.Span.SpanKind.SPAN_KIND_INTERNAL,
    trace.SpanKind.PRODUCER: trace_pb2.Span.SpanKind.SPAN_KIND_PRODUCER,
    trace.SpanKind.CONSUMER: trace_pb2.Span.SpanKind.SPAN_KIND_CONSUMER,
}


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
                    if k == "previous-rpc-attempts":
                        try:
                            kv.value.int_value = int(v)
                        except (ValueError, TypeError):
                            kv.value.int_value = 0
                    elif k == "transparent-retry":
                        if isinstance(v, (str, bytes)):
                            v_str = (
                                v.decode("utf-8", errors="ignore")
                                if isinstance(v, bytes)
                                else v
                            )
                            kv.value.bool_value = v_str.lower() in (
                                "true",
                                "1",
                                "t",
                                "yes",
                                "y",
                            )
                        else:
                            kv.value.bool_value = bool(v)
                    elif isinstance(v, bool):
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

            kind = _SPAN_KIND_MAP.get(
                span.kind, trace_pb2.Span.SpanKind.SPAN_KIND_UNSPECIFIED
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
        except Exception as e:  # pylint: disable=broad-except
            print(f"OTLPSpanExporter Export exception: {e}", flush=True)
            return SpanExportResult.FAILURE
        return SpanExportResult.SUCCESS

    def shutdown(self) -> None:
        time.sleep(0.5)
        try:
            self._channel.close()
        except Exception:
            pass

    def force_flush(self, timeout_millis: int = 30000) -> bool:
        time.sleep(0.5)
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
        time.sleep(0.5)


def shutdown_tracer_provider():
    # pylint: disable=global-statement
    global _GLOBAL_PROVIDER
    if _GLOBAL_PROVIDER:
        try:
            _GLOBAL_PROVIDER.shutdown()
        except Exception:
            pass
        _GLOBAL_PROVIDER = None
        time.sleep(0.5)


def format_traceparent(
    trace_id: Union[int, str],
    span_id: Union[int, str],
    is_sampled: bool = True,
) -> str:
    """Format W3C traceparent header string (00-{trace_id_32_hex}-{span_id_16_hex}-{flags_2_hex})."""
    if isinstance(trace_id, str):
        trace_id_int = int(trace_id, 16)
    else:
        trace_id_int = int(trace_id)
    if isinstance(span_id, str):
        span_id_int = int(span_id, 16)
    else:
        span_id_int = int(span_id)
    flags = "01" if is_sampled else "00"
    return f"00-{trace_id_int:032x}-{span_id_int:016x}-{flags}"


def pack_grpc_trace_bin(
    trace_id: Union[int, str, bytes],
    span_id: Union[int, str, bytes],
    is_sampled: bool = True,
) -> bytes:
    """Packs trace_id and span_id into standard 29-byte grpc-trace-bin TLV format."""
    if isinstance(trace_id, str):
        trace_id_bytes = int(trace_id, 16).to_bytes(16, byteorder="big")
    elif isinstance(trace_id, int):
        trace_id_bytes = trace_id.to_bytes(16, byteorder="big")
    elif isinstance(trace_id, (bytes, bytearray, memoryview)):
        trace_id_bytes = bytes(trace_id).rjust(16, b"\x00")
    else:
        raise TypeError(f"Unsupported trace_id type: {type(trace_id)}")

    if isinstance(span_id, str):
        span_id_bytes = int(span_id, 16).to_bytes(8, byteorder="big")
    elif isinstance(span_id, int):
        span_id_bytes = span_id.to_bytes(8, byteorder="big")
    elif isinstance(span_id, (bytes, bytearray, memoryview)):
        span_id_bytes = bytes(span_id).rjust(8, b"\x00")
    else:
        raise TypeError(f"Unsupported span_id type: {type(span_id)}")

    trace_options = 1 if is_sampled else 0
    return struct.pack(
        ">BB16sB8sBB",
        0,
        0,
        trace_id_bytes,
        1,
        span_id_bytes,
        2,
        trace_options,
    )


def unpack_grpc_trace_bin(
    header_bytes: Union[bytes, bytearray, memoryview, str],
) -> Tuple[Optional[int], Optional[int], bool]:
    """Unpacks a 29-byte grpc-trace-bin TLV header into (trace_id, span_id, is_sampled)."""
    if isinstance(header_bytes, str):
        try:
            s = header_bytes.strip()
            s += "=" * (-len(s) % 4)
            raw = base64.b64decode(s)
        except Exception:
            raw = header_bytes.encode("latin1")
    elif isinstance(header_bytes, (bytes, bytearray, memoryview)):
        raw = bytes(header_bytes)
        if len(raw) != 29 and len(raw) >= 38:
            try:
                b = raw.strip()
                b += b"=" * (-len(b) % 4)
                decoded = base64.b64decode(b)
                if len(decoded) >= 29 and decoded[0] == 0:
                    raw = decoded
            except Exception:
                pass
    else:
        return None, None, False

    if len(raw) < 29 or raw[0] != 0:
        return None, None, False

    trace_id_int = None
    span_id_int = None
    is_sampled = False

    pos = 1
    while pos < len(raw):
        field_id = raw[pos]
        pos += 1
        if field_id == 0:
            if pos + 16 > len(raw):
                break
            trace_id_int = int.from_bytes(raw[pos : pos + 16], "big")
            pos += 16
        elif field_id == 1:
            if pos + 8 > len(raw):
                break
            span_id_int = int.from_bytes(raw[pos : pos + 8], "big")
            pos += 8
        elif field_id == 2:
            if pos + 1 > len(raw):
                break
            is_sampled = bool(raw[pos] & 1)
            pos += 1
        else:
            break

    if (
        trace_id_int is not None
        and span_id_int is not None
        and trace_id_int != 0
        and span_id_int != 0
    ):
        return trace_id_int, span_id_int, is_sampled
    return None, None, False


_HEX_DIGITS = frozenset("0123456789abcdefABCDEF")


def parse_traceparent(
    header: Union[str, bytes],
) -> Tuple[Optional[int], Optional[int], bool]:
    """Parses W3C traceparent header string into (trace_id, span_id, is_sampled)."""
    if isinstance(header, (bytes, bytearray, memoryview)):
        header_str = bytes(header).decode("ascii", errors="ignore").strip()
    elif isinstance(header, str):
        header_str = header.strip()
    else:
        return None, None, False

    if len(header_str) != 55:
        return None, None, False

    parts = header_str.split("-")
    if len(parts) != 4:
        return None, None, False

    version, trace_id_hex, span_id_hex, flags_hex = parts
    if version != "00":
        return None, None, False

    if len(trace_id_hex) != 32 or len(span_id_hex) != 16 or len(flags_hex) != 2:
        return None, None, False

    if not all(c in _HEX_DIGITS for c in trace_id_hex):
        return None, None, False
    if not all(c in _HEX_DIGITS for c in span_id_hex):
        return None, None, False
    if not all(c in _HEX_DIGITS for c in flags_hex):
        return None, None, False

    try:
        trace_id_int = int(trace_id_hex, 16)
        span_id_int = int(span_id_hex, 16)
        flags_int = int(flags_hex, 16)
        if trace_id_int == 0 or span_id_int == 0:
            return None, None, False
        is_sampled = bool(flags_int & 1)
        return trace_id_int, span_id_int, is_sampled
    except ValueError:
        return None, None, False


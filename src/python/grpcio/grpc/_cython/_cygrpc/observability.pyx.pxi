import sys
import time
import os
import codecs
from libcpp.cast cimport static_cast
from libc.stdio cimport printf

from grpc import _observability

cdef const char* CLIENT_CALL_TRACER = "client_call_tracer"
cdef const char* SERVER_CALL_TRACER_FACTORY = "server_call_tracer_factory"

def observability_enabled() -> bool:
  return observability_tracing_enabled() or observability_metrics_enabled()


def observability_tracing_enabled() -> bool:
  return os.environ.get('GRPC_OPEN_CENSUS_TRACING_ENABLED', '0') == 'True'


def observability_metrics_enabled() -> bool:
  return os.environ.get('GRPC_OPEN_CENSUS_STATS_ENABLED', '0') == 'True'


def set_server_call_tracer_factory(object capsule) -> None:
  capsule_ptr = cpython.PyCapsule_GetPointer(capsule, SERVER_CALL_TRACER_FACTORY)
  grpc_register_server_call_tracer_factory(capsule_ptr)


def set_context_from_server_call_tracer(RequestCallEvent event) -> None:
  if not observability_enabled():
    return
  cdef ServerCallTracer* server_call_tracer
  server_call_tracer = static_cast['ServerCallTracer*'](grpc_call_get_call_tracer(event.call.c_call))
  if observability_tracing_enabled():
    # TraceId and SpanId is hex string, need to convert to str
    trace_id = _decode(codecs.decode(server_call_tracer.TraceId(), 'hex_codec'))
    span_id = _decode(codecs.decode(server_call_tracer.SpanId(), 'hex_codec'))
    is_sampled = server_call_tracer.IsSampled()
    _observability.save_span_context(trace_id, span_id, is_sampled)


cdef void set_client_call_tracer_on_call(_CallState call_state, bytes method):
  capsule = _observability.create_client_call_tracer_capsule(method)
  capsule_ptr = cpython.PyCapsule_GetPointer(capsule, CLIENT_CALL_TRACER)
  grpc_call_set_call_tracer(call_state.c_call, capsule_ptr)
  call_state.call_tracer_capsule = capsule

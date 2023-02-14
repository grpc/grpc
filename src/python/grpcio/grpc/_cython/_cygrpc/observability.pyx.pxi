import grpc
import codecs
from typing import Optional

from libcpp.cast cimport static_cast
from libc.stdio cimport printf


cdef const char* CLIENT_CALL_TRACER = "gcp_opencensus_client_call_tracer"
cdef const char* SERVER_CALL_TRACER_FACTORY = "gcp_opencensus_server_call_tracer_factory"


def get_grpc_observability() -> Optional[grpc.GrpcObservability]:
  return getattr(grpc, '_grpc_observability', None)


def set_server_call_tracer_factory() -> None:
  observability = get_grpc_observability()
  if not observability:
    return
  capsule = observability.create_server_call_tracer_factory()
  capsule_ptr = cpython.PyCapsule_GetPointer(capsule, SERVER_CALL_TRACER_FACTORY)
  _register_server_call_tracer_factory(capsule_ptr)


def maybe_save_server_trace_context(RequestCallEvent event) -> None:
  observability = get_grpc_observability()
  if not (observability and observability._tracing_enabled()):
    return
  cdef ServerCallTracer* server_call_tracer
  server_call_tracer = static_cast['ServerCallTracer*'](_get_call_tracer(event.call.c_call))
  # TraceId and SpanId is hex string, need to convert to str
  trace_id = _decode(codecs.decode(server_call_tracer.TraceId(), 'hex_codec'))
  span_id = _decode(codecs.decode(server_call_tracer.SpanId(), 'hex_codec'))
  is_sampled = server_call_tracer.IsSampled()
  observability.save_trace_context(trace_id, span_id, is_sampled)


def maybe_record_rpc_latency(object state) -> None:
  observability = get_grpc_observability()
  if not (observability and observability._stats_enabled()):
    return
  rpc_latency = state.rpc_end_time - state.rpc_start_time
  rpc_latency_ms = rpc_latency.total_seconds() * 1000
  observability.record_rpc_latency(state.method, rpc_latency_ms, state.code)


cdef void maybe_set_client_call_tracer_on_call(_CallState call_state, bytes method) except *:
  observability = get_grpc_observability()
  if not (observability and observability._observability_enabled()):
    return
  capsule = observability.create_client_call_tracer_capsule(method)
  capsule_ptr = cpython.PyCapsule_GetPointer(capsule, CLIENT_CALL_TRACER)
  _set_call_tracer(call_state.c_call, capsule_ptr)
  call_state.call_tracer_capsule = capsule


cdef void _set_call_tracer(grpc_call* call, void* capsule_ptr):
  cdef ClientCallTracer* call_tracer = <ClientCallTracer*>capsule_ptr
  grpc_call_context_set(call, GRPC_CONTEXT_CALL_TRACER_ANNOTATION_INTERFACE, call_tracer, NULL)


cdef void _register_server_call_tracer_factory(void* capsule_ptr):
  cdef ServerCallTracerFactory* call_tracer_factory = <ServerCallTracerFactory*>capsule_ptr
  ServerCallTracerFactory.RegisterGlobal(call_tracer_factory)


cdef void* _get_call_tracer(grpc_call* call):
  cdef void* call_tracer = grpc_call_context_get(call, GRPC_CONTEXT_CALL_TRACER_ANNOTATION_INTERFACE)
  return call_tracer
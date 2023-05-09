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


import codecs
from typing import Optional

from libcpp.cast cimport static_cast

from grpc import _observability


cdef const char* CLIENT_CALL_TRACER = "gcp_opencensus_client_call_tracer"
cdef const char* SERVER_CALL_TRACER_FACTORY = "gcp_opencensus_server_call_tracer_factory"


def set_server_call_tracer_factory(object observability_plugin) -> None:
  capsule = observability_plugin.create_server_call_tracer_factory()
  capsule_ptr = cpython.PyCapsule_GetPointer(capsule, SERVER_CALL_TRACER_FACTORY)
  _register_server_call_tracer_factory(capsule_ptr)


def maybe_save_server_trace_context(RequestCallEvent event) -> None:
  observability_plugin = _observability._grpc_observability_stub
  if not (observability_plugin and observability_plugin.tracing_enabled):
    return
  cdef ServerCallTracer* server_call_tracer
  server_call_tracer = static_cast['ServerCallTracer*'](_get_call_tracer(event.call.c_call))
  # TraceId and SpanId is hex string, need to convert to str
  trace_id = _decode(codecs.decode(server_call_tracer.TraceId(), 'hex_codec'))
  span_id = _decode(codecs.decode(server_call_tracer.SpanId(), 'hex_codec'))
  is_sampled = server_call_tracer.IsSampled()
  observability_plugin.save_trace_context(trace_id, span_id, is_sampled)


cdef void maybe_set_client_call_tracer_on_call(_CallState call_state, bytes method_name) except *:
  observability_plugin = _observability._grpc_observability_stub
  if not (observability_plugin and observability_plugin.observability_enabled):
    return
  capsule = observability_plugin.create_client_call_tracer(method_name)
  capsule_ptr = cpython.PyCapsule_GetPointer(capsule, CLIENT_CALL_TRACER)
  _set_call_tracer(call_state.c_call, capsule_ptr)
  call_state.call_tracer_capsule = capsule


cdef void _set_call_tracer(grpc_call* call, void* capsule_ptr):
  cdef ClientCallTracer* call_tracer = <ClientCallTracer*>capsule_ptr
  grpc_call_context_set(call, GRPC_CONTEXT_CALL_TRACER_ANNOTATION_INTERFACE, call_tracer, NULL)


cdef void* _get_call_tracer(grpc_call* call):
  cdef void* call_tracer = grpc_call_context_get(call, GRPC_CONTEXT_CALL_TRACER_ANNOTATION_INTERFACE)
  return call_tracer


cdef void _register_server_call_tracer_factory(void* capsule_ptr):
  cdef ServerCallTracerFactory* call_tracer_factory = <ServerCallTracerFactory*>capsule_ptr
  ServerCallTracerFactory.RegisterGlobal(call_tracer_factory)

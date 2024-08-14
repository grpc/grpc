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


cdef const char* CLIENT_CALL_TRACER = "client_call_tracer"
cdef const char* SERVER_CALL_TRACER_FACTORY = "server_call_tracer_factory"


def get_server_call_tracer_factory_address(object observability_plugin, bint xds) -> Optional[int]:
  capsule = observability_plugin.create_server_call_tracer_factory(xds=xds)
  if capsule:
    capsule_ptr = cpython.PyCapsule_GetPointer(capsule, SERVER_CALL_TRACER_FACTORY)
    return int(<uintptr_t>capsule_ptr)
  else:
    return None

def clear_server_call_tracer_factory() -> None:
  _register_server_call_tracer_factory(NULL)


def maybe_save_server_trace_context(RequestCallEvent event) -> None:
  cdef ServerCallTracer* server_call_tracer
  with _observability.get_plugin() as plugin:
    if not (plugin and plugin.tracing_enabled):
      return
    server_call_tracer = static_cast['ServerCallTracer*'](_get_call_tracer(event.call.c_call))
    # TraceId and SpanId is hex string, need to convert to str
    trace_id = _decode(codecs.decode(server_call_tracer.TraceId(), 'hex_codec'))
    span_id = _decode(codecs.decode(server_call_tracer.SpanId(), 'hex_codec'))
    is_sampled = server_call_tracer.IsSampled()
    plugin.save_trace_context(trace_id, span_id, is_sampled)


cdef void _set_call_tracer(grpc_call* call, void* capsule_ptr):
  cdef ClientCallTracer* call_tracer = <ClientCallTracer*>capsule_ptr
  grpc_call_tracer_set_and_manage(call, call_tracer)


cdef void* _get_call_tracer(grpc_call* call):
  cdef void* call_tracer = grpc_call_tracer_get(call)
  return call_tracer


cdef void _register_server_call_tracer_factory(void* capsule_ptr):
  cdef ServerCallTracerFactory* call_tracer_factory = <ServerCallTracerFactory*>capsule_ptr
  ServerCallTracerFactory.RegisterGlobal(call_tracer_factory)

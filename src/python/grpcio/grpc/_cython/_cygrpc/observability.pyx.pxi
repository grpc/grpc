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


def set_server_call_tracer_factory() -> None:
  capsule = _observability.create_server_call_tracer_factory_capsule()
  capsule_ptr = cpython.PyCapsule_GetPointer(capsule, SERVER_CALL_TRACER_FACTORY)
  grpc_register_server_call_tracer_factory(capsule_ptr)


cdef void set_client_call_tracer_on_call(_CallState call_state, bytes method):
  capsule = _observability.create_client_call_tracer_capsule(method)
  capsule_ptr = cpython.PyCapsule_GetPointer(capsule, CLIENT_CALL_TRACER)
  grpc_call_set_call_tracer(call_state.c_call, capsule_ptr)
  call_state.call_tracer_capsule = capsule

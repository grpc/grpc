# Copyright 2023 The gRPC authors.
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

import abc
import logging
import sys
import threading
import types
from typing import Any, Generic, Optional, TypeVar

import grpc  # pytype: disable=pyi-error
from grpc._cython import cygrpc as _cygrpc

_LOGGER = logging.getLogger(__name__)

_channel = Any  # _channel.py imports this module.
PyCapsule = TypeVar('PyCapsule')


class GrpcObservability(Generic[PyCapsule], metaclass=abc.ABCMeta):
    # we need to add hooks so that the GCP observability package can register functions with
    # the grpcio module and so can any other observability module conforming to the interface.
    _TRACING_ENABLED: bool = False
    _STATS_ENABLED: bool = False

    @abc.abstractmethod
    def create_client_call_tracer_capsule(self, method_name: bytes) -> PyCapsule:
        raise NotImplementedError()

    @abc.abstractmethod
    def delete_client_call_tracer(
            self, client_call_tracer_capsule: PyCapsule) -> None:
        # delte client call tracer have to be called on o11y package side.
        # Call it for both segregated and integrated call (`_process_integrated_call_tag`)
        raise NotImplementedError()

    @abc.abstractmethod
    def save_trace_context(self, trace_id: str, span_id: str,
                           is_sampled: bool) -> None:
        raise NotImplementedError()

    @abc.abstractmethod
    def create_server_call_tracer_factory(self) -> PyCapsule:
        raise NotImplementedError()

    @abc.abstractmethod
    def record_rpc_latency(self, method: str, rpc_latency: float,
                           status_code: Any) -> None:
        raise NotImplementedError()

    def _enable_tracing(self, enable: bool) -> None:
        self._TRACING_ENABLED = enable

    def _enable_stats(self, enable: bool) -> None:
        self._STATS_ENABLED = enable

    def _tracing_enabled(self) -> bool:
        return self._TRACING_ENABLED

    def _stats_enabled(self) -> bool:
        return self._STATS_ENABLED

    def _observability_enabled(self) -> bool:
        return self._tracing_enabled() or self._stats_enabled()


def _observability_init(grpc_observability: GrpcObservability) -> None:
    try:
        setattr(grpc, "_grpc_observability", grpc_observability)
        _cygrpc.set_server_call_tracer_factory(grpc_observability)
    except Exception as e:  # pylint:disable=broad-except
        _LOGGER.exception("grpc.observability initiazation failed with %s", e)


def get_grpc_observability() -> Optional[GrpcObservability]:
    return getattr(grpc, '_grpc_observability', None)


def delete_call_tracer(client_call_tracer_capsule: PyCapsule) -> None:
    observability = get_grpc_observability()
    if not (observability and observability._observability_enabled()):
        return
    observability.delete_client_call_tracer(client_call_tracer_capsule)


def maybe_record_rpc_latency(state: "_channel._RPCState") -> None:
    observability = get_grpc_observability()
    if not (observability and observability._stats_enabled()):
        return
    rpc_latency = state.rpc_end_time - state.rpc_start_time
    rpc_latency_ms = rpc_latency.total_seconds() * 1000
    observability.record_rpc_latency(state.method, rpc_latency_ms, state.code)

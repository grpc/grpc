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

from __future__ import annotations

import abc
import logging
import threading
from typing import Any, Generic, Optional, TypeVar

from grpc._cython import cygrpc as _cygrpc

_LOGGER = logging.getLogger(__name__)

_channel = Any  # _channel.py imports this module.
ClientCallTracerCapsule = TypeVar('ClientCallTracerCapsule')
ServerCallTracerFactoryCapsule = TypeVar('ServerCallTracerFactoryCapsule')

_lock: threading.RLock = threading.RLock()
_grpc_observability_stub: Optional[ObservabilityPlugin] = None  # pylint: disable=used-before-assignment


class ObservabilityPlugin(Generic[ClientCallTracerCapsule,
                                  ServerCallTracerFactoryCapsule],
                          metaclass=abc.ABCMeta):
    """
    Note: Any future methods added to this interface cannot have the @abc.abstractmethod annotation.
    """
    _tracing_enabled: bool = False
    _stats_enabled: bool = False

    @abc.abstractmethod
    def create_client_call_tracer(
            self, method_name: bytes) -> ClientCallTracerCapsule:
        raise NotImplementedError()

    @abc.abstractmethod
    def delete_client_call_tracer(
            self, client_call_tracer: ClientCallTracerCapsule) -> None:
        # delte client call tracer have to be called on o11y package side.
        # Call it for both segregated and integrated call (`_process_integrated_call_tag`)
        raise NotImplementedError()

    @abc.abstractmethod
    def save_trace_context(self, trace_id: str, span_id: str,
                           is_sampled: bool) -> None:
        raise NotImplementedError()

    @abc.abstractmethod
    def create_server_call_tracer_factory(
            self) -> ServerCallTracerFactoryCapsule:
        raise NotImplementedError()

    @abc.abstractmethod
    def record_rpc_latency(self, method: str, rpc_latency: float,
                           status_code: Any) -> None:
        raise NotImplementedError()

    def enable_tracing(self, enable: bool) -> None:
        self._tracing_enabled = enable

    def enable_stats(self, enable: bool) -> None:
        self._stats_enabled = enable

    @property
    def tracing_enabled(self) -> bool:
        return self._tracing_enabled

    @property
    def stats_enabled(self) -> bool:
        return self._stats_enabled

    @property
    def observability_enabled(self) -> bool:
        return self.tracing_enabled or self.stats_enabled


def _observability_init(observability_plugin: ObservabilityPlugin) -> None:
    global _grpc_observability_stub  # pylint: disable=global-statement
    try:
        with _lock:
            _grpc_observability_stub = observability_plugin
        _cygrpc.set_server_call_tracer_factory(observability_plugin)
    # TODO(xuanwn): Change to specific exception
    except Exception as e:  # pylint:disable=broad-except
        _LOGGER.exception("grpc.observability initialization failed with %s", e)


def delete_call_tracer(
        client_call_tracer_capsule: "ClientCallTracerCapsule") -> None:
    global _grpc_observability_stub  # pylint: disable=global-statement
    if not (_grpc_observability_stub and
            _grpc_observability_stub.observability_enabled):
        return
    _grpc_observability_stub.delete_client_call_tracer(
        client_call_tracer_capsule)


def maybe_record_rpc_latency(state: "_channel._RPCState") -> None:
    global _grpc_observability_stub  # pylint: disable=global-statement
    if not (_grpc_observability_stub and
            _grpc_observability_stub.stats_enabled):
        return
    rpc_latency = state.rpc_end_time - state.rpc_start_time
    rpc_latency_ms = rpc_latency.total_seconds() * 1000
    _grpc_observability_stub.record_rpc_latency(state.method, rpc_latency_ms,
                                                state.code)

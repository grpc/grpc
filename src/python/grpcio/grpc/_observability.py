# Copyright 2020 The gRPC authors.
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

import types
import logging

from grpc._cython import cygrpc as _cygrpc

_REQUIRED_SYMBOLS = ("_create_client_call_tracer_capsule",
                     "_save_span_context")
_UNINSTALLED_TEMPLATE = "Install the grpc_observability package (1.xx.xx) to use the {} function."

_LOGGER = logging.getLogger(__name__)


def _has_symbols(mod: types.ModuleType) -> bool:
    return all(hasattr(mod, sym) for sym in _REQUIRED_SYMBOLS)


def _is_grpc_observability_importable() -> bool:
    try:
        import grpc_observability  # pylint: disable=unused-import # pytype: disable=import-error
        return True
    except ImportError as e:
        # NOTE: It's possible that we're encountering a transitive ImportError, so
        # we check for that and re-raise if so.
        if "grpc_observability" not in e.args[0]:
            raise
        return False


def _call_with_lazy_import(fn_name: str, **kwargs) -> types.ModuleType:
    """Calls one of the three functions, lazily importing grpc_observability.

    Args:
      fn_name: The name of the function to import from grpc_observability.observability.

      **kwargs:
        method: The keyword args used to call functions.

    Returns:
      The appropriate module object.
    """
    if not _is_grpc_observability_importable():
        raise NotImplementedError(_UNINSTALLED_TEMPLATE.format(fn_name))
    import grpc_observability.observability  # pytype: disable=import-error
    if _has_symbols(grpc_observability.observability):
        fn = getattr(grpc_observability.observability, '_' + fn_name)
        return fn(**kwargs)
    else:
        raise NotImplementedError(_UNINSTALLED_TEMPLATE.format(fn_name))


def observability_init(server_call_tracer_factory: object) -> None:
    if not _cygrpc.observability_enabled():
        return

    try:
        _cygrpc.set_server_call_tracer_factory(server_call_tracer_factory)
    except Exception as e:  # pylint:disable=broad-except
        _LOGGER.exception(f"Observability initiazation failed with {e}")


def create_client_call_tracer_capsule(method: bytes) -> object:
    return _call_with_lazy_import("create_client_call_tracer_capsule",
                                  method=method)

def save_span_context(trace_id: str, span_id: str, is_sampled: bool) -> None:
    return _call_with_lazy_import("save_span_context",
                                  trace_id=trace_id,
                                  span_id=span_id,
                                  is_sampled=is_sampled)

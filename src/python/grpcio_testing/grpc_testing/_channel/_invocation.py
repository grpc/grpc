# Copyright 2017 gRPC authors.
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

import logging
import threading
import types
from typing import Any, Callable, Iterator, Optional, Tuple

import grpc
from grpc._typing import MetadataType
from grpc_testing import _common

_NOT_YET_OBSERVED = object()
logging.basicConfig()
_LOGGER = logging.getLogger(__name__)


def _cancel(handler: _common.ChannelRpcHandler) -> bool:
    return handler.cancel(grpc.StatusCode.CANCELLED, 'Locally cancelled!')


def _is_active(handler: _common.ChannelRpcHandler) -> bool:
    return handler.is_active()


def _time_remaining(unused_handler: _common.ChannelRpcHandler) -> float:
    raise NotImplementedError()


def _add_callback(handler: _common.ChannelRpcHandler,
                  callback: Callable) -> None:
    return handler.add_callback(callback)


def _initial_metadata(
        handler: _common.ChannelRpcHandler) -> Optional[MetadataType]:
    return handler.initial_metadata()


def _trailing_metadata(
        handler: _common.ChannelRpcHandler) -> Optional[MetadataType]:
    trailing_metadata, unused_code, unused_details = handler.termination()
    return trailing_metadata


def _code(handler: _common.ChannelRpcHandler) -> grpc.StatusCode:
    unused_trailing_metadata, code, unused_details = handler.termination()
    return code


def _details(handler: _common.ChannelRpcHandler) -> str:
    unused_trailing_metadata, unused_code, details = handler.termination()
    return details


class _Call(grpc.Call):
    _hanlder: _common.ChannelRpcHandler

    def __init__(self, handler: _common.ChannelRpcHandler):
        self._handler = handler

    def cancel(self) -> bool:
        _cancel(self._handler)

    def is_active(self) -> bool:
        return _is_active(self._handler)

    def time_remaining(self) -> float:
        return _time_remaining(self._handler)

    def add_callback(self, callback: Callable) -> None:
        return _add_callback(self._handler, callback)

    def initial_metadata(self) -> Optional[MetadataType]:
        return _initial_metadata(self._handler)

    def trailing_metadata(self) -> Optional[MetadataType]:
        return _trailing_metadata(self._handler)

    def code(self) -> grpc.StatusCode:
        return _code(self._handler)

    def details(self) -> str:
        return _details(self._handler)


class _RpcErrorCall(grpc.RpcError, grpc.Call):

    def __init__(self, handler: _common.ChannelRpcHandler):
        self._handler = handler

    def cancel(self) -> bool:
        _cancel(self._handler)

    def is_active(self) -> bool:
        return _is_active(self._handler)

    def time_remaining(self) -> float:
        return _time_remaining(self._handler)

    def add_callback(self, callback: Callable) -> None:
        return _add_callback(self._handler, callback)

    def initial_metadata(self) -> Optional[MetadataType]:
        return _initial_metadata(self._handler)

    def trailing_metadata(self) -> Optional[MetadataType]:
        return _trailing_metadata(self._handler)

    def code(self) -> grpc.StatusCode:
        return _code(self._handler)

    def details(self) -> str:
        return _details(self._handler)


def _next(handler: _common.ChannelRpcHandler) -> Any:
    read = handler.take_response()
    if read.code is None:
        return read.response
    elif read.code is grpc.StatusCode.OK:
        raise StopIteration()
    else:
        raise _RpcErrorCall(handler)


class _HandlerExtras(object):

    def __init__(self):
        self.condition = threading.Condition()
        self.unary_response = _NOT_YET_OBSERVED
        self.cancelled = False


def _with_extras_cancel(handler: _common.ChannelRpcHandler,
                        extras: _HandlerExtras) -> bool:
    with extras.condition:
        if handler.cancel(grpc.StatusCode.CANCELLED, 'Locally cancelled!'):
            extras.cancelled = True
            return True
        else:
            return False


def _extras_without_cancelled(extras: _HandlerExtras):
    with extras.condition:
        return extras.cancelled


def _running(handler) -> bool:
    return handler.is_active()


def _done(handler) -> bool:
    return not handler.is_active()


def _with_extras_unary_response(handler: _common.ChannelRpcHandler,
                                extras: _HandlerExtras) -> Any:
    with extras.condition:
        if extras.unary_response is _NOT_YET_OBSERVED:
            read = handler.take_response()
            if read.code is None:
                extras.unary_response = read.response
                return read.response
            else:
                raise _RpcErrorCall(handler)
        else:
            return extras.unary_response


def _exception(
        unused_handler: _common.ChannelRpcHandler) -> Optional[Exception]:
    raise NotImplementedError('TODO!')


def _traceback(
        unused_handler: _common.ChannelRpcHandler
) -> Optional[types.TracebackType]:
    raise NotImplementedError('TODO!')


def _add_done_callback(handler: _common.ChannelRpcHandler, callback: Callable,
                       future) -> None:
    adapted_callback = lambda: callback(future)
    if not handler.add_callback(adapted_callback):
        callback(future)


class _FutureCall(grpc.Future, grpc.Call):
    _handler: _common.ChannelRpcHandler
    _extras: _HandlerExtras

    def __init__(self, handler: _common.ChannelRpcHandler,
                 extras: _HandlerExtras):
        self._handler = handler
        self._extras = extras

    def cancel(self) -> bool:
        return _with_extras_cancel(self._handler, self._extras)

    def cancelled(self) -> bool:
        return _extras_without_cancelled(self._extras)

    def running(self) -> bool:
        return _running(self._handler)

    def done(self) -> bool:
        return _done(self._handler)

    def result(self) -> Any:
        return _with_extras_unary_response(self._handler, self._extras)

    def exception(self) -> Optional[Exception]:
        return _exception(self._handler)

    def traceback(self) -> Optional[types.TracebackType]:
        return _traceback(self._handler)

    def add_done_callback(self, fn: Callable) -> None:
        _add_done_callback(self._handler, fn, self)

    def is_active(self) -> bool:
        return _is_active(self._handler)

    def time_remaining(self) -> float:
        return _time_remaining(self._handler)

    def add_callback(self, callback: Callable) -> None:
        return _add_callback(self._handler, callback)

    def initial_metadata(self) -> Optional[MetadataType]:
        return _initial_metadata(self._handler)

    def trailing_metadata(self) -> Optional[MetadataType]:
        return _trailing_metadata(self._handler)

    def code(self) -> grpc.StatusCode:
        return _code(self._handler)

    def details(self) -> str:
        return _details(self._handler)


def consume_requests(request_iterator: Iterator,
                     handler: _common.ChannelRpcHandler) -> None:

    def _consume():
        while True:
            try:
                request = next(request_iterator)
                added = handler.add_request(request)
                if not added:
                    break
            except StopIteration:
                handler.close_requests()
                break
            except Exception:  # pylint: disable=broad-except
                details = 'Exception iterating requests!'
                _LOGGER.exception(details)
                handler.cancel(grpc.StatusCode.UNKNOWN, details)

    consumption = threading.Thread(target=_consume)
    consumption.start()


def blocking_unary_response(handler: _common.ChannelRpcHandler) -> Any:
    read = handler.take_response()
    if read.code is None:
        unused_trailing_metadata, code, unused_details = handler.termination()
        if code is grpc.StatusCode.OK:
            return read.response
        else:
            raise _RpcErrorCall(handler)
    else:
        raise _RpcErrorCall(handler)


def blocking_unary_response_with_call(
        handler: _common.ChannelRpcHandler) -> Tuple[Any, _Call]:
    read = handler.take_response()
    if read.code is None:
        unused_trailing_metadata, code, unused_details = handler.termination()
        if code is grpc.StatusCode.OK:
            return read.response, _Call(handler)
        else:
            raise _RpcErrorCall(handler)
    else:
        raise _RpcErrorCall(handler)


def future_call(handler: _common.ChannelRpcHandler) -> _FutureCall:
    return _FutureCall(handler, _HandlerExtras())


class ResponseIteratorCall(grpc.Call):
    _hanlder: _common.ChannelRpcHandler

    def __init__(self, handler: _common.ChannelRpcHandler):
        self._handler = handler

    def __iter__(self):
        return self

    def __next__(self) -> Any:
        return _next(self._handler)

    def next(self) -> Any:
        return _next(self._handler)

    def cancel(self) -> bool:
        _cancel(self._handler)

    def is_active(self) -> bool:
        return _is_active(self._handler)

    def time_remaining(self) -> float:
        return _time_remaining(self._handler)

    def add_callback(self, callback: Callable) -> None:
        return _add_callback(self._handler, callback)

    def initial_metadata(self) -> Optional[MetadataType]:
        return _initial_metadata(self._handler)

    def trailing_metadata(self) -> Optional[MetadataType]:
        return _trailing_metadata(self._handler)

    def code(self) -> grpc.StatusCode:
        return _code(self._handler)

    def details(self) -> str:
        return _details(self._handler)

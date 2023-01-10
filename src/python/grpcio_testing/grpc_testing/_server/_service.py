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

import copy
from typing import Any, Callable, Union

import grpc
from grpc_testing._server import _handler
from grpc_testing._server import _rpc
from grpc_testing._server import _servicer_context


class _RequestIterator(object):
    _rpc: _rpc.Rpc
    _hanlder: _handler.Handler

    def __init__(self, rpc: _rpc.Rpc, handler: _handler.Handler):
        self._rpc = rpc
        self._handler = handler

    def _next(self) -> Any:
        read = self._handler.take_request()
        if read.requests_closed:
            raise StopIteration()
        elif read.terminated:
            rpc_error = grpc.RpcError()
            self._rpc.add_rpc_error(rpc_error)
            raise rpc_error
        else:
            return read.request

    def __iter__(self):
        return self

    def __next__(self) -> Any:
        return self._next()

    def next(self) -> Any:
        return self._next()


def _unary_response(
        argument: Any,
        implementation: Callable[[Any, _servicer_context.ServicerContext],
                                 Any], rpc: _rpc.Rpc,
        servicer_context: _servicer_context.ServicerContext) -> None:
    try:
        response = implementation(argument, servicer_context)
    except Exception as exception:  # pylint: disable=broad-except
        rpc.application_exception_abort(exception)
    else:
        rpc.unary_response_complete(response)


def _stream_response(
        argument: Any,
        implementation: Callable[[Any, _servicer_context.ServicerContext],
                                 Any], rpc: _rpc.Rpc,
        servicer_context: _servicer_context.ServicerContext) -> None:
    try:
        response_iterator = implementation(argument, servicer_context)
    except Exception as exception:  # pylint: disable=broad-except
        rpc.application_exception_abort(exception)
    else:
        while True:
            try:
                response = copy.deepcopy(next(response_iterator))
            except StopIteration:
                rpc.stream_response_complete()
                break
            except Exception as exception:  # pylint: disable=broad-except
                rpc.application_exception_abort(exception)
                break
            else:
                rpc.stream_response(response)


def unary_unary(implementation: Callable[
    [Any, _servicer_context.ServicerContext], Any], rpc: _rpc.Rpc, request: Any,
                servicer_context: _servicer_context.ServicerContext) -> None:
    _unary_response(request, implementation, rpc, servicer_context)


def unary_stream(implementation: Callable[
    [Any, _servicer_context.ServicerContext], Any], rpc: _rpc.Rpc, request: Any,
                 servicer_context: _servicer_context.ServicerContext) -> None:
    _stream_response(request, implementation, rpc, servicer_context)


def stream_unary(implementation: Callable[
    [Any, _servicer_context.ServicerContext], Any], rpc: _rpc.Rpc,
                 handler: _handler.Handler,
                 servicer_context: _servicer_context.ServicerContext) -> None:
    _unary_response(_RequestIterator(rpc, handler), implementation, rpc,
                    servicer_context)


def stream_stream(implementation: Callable[
    [Any, _servicer_context.ServicerContext], Any], rpc: _rpc.Rpc,
                  handler: _handler.Handler,
                  servicer_context: _servicer_context.ServicerContext) -> None:
    _stream_response(_RequestIterator(rpc, handler), implementation, rpc,
                     servicer_context)

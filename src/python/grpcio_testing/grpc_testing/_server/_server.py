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

import threading

import grpc_testing
from grpc_testing import _common
from grpc_testing._server import _handler
from grpc_testing._server import _rpc
from grpc_testing._server import _server_rpc
from grpc_testing._server import _service
from grpc_testing._server import _servicer_context


def _implementation(descriptors_to_servicers, method_descriptor):
    servicer = descriptors_to_servicers[method_descriptor.containing_service]
    return getattr(servicer, method_descriptor.name)


def _unary_unary_service(request):
    def service(implementation, rpc, servicer_context):
        _service.unary_unary(
            implementation, rpc, request, servicer_context)
    return service


def _unary_stream_service(request):
    def service(implementation, rpc, servicer_context):
        _service.unary_stream(
            implementation, rpc, request, servicer_context)
    return service


def _stream_unary_service(handler):
    def service(implementation, rpc, servicer_context):
        _service.stream_unary(implementation, rpc, handler, servicer_context)
    return service


def _stream_stream_service(handler):
    def service(implementation, rpc, servicer_context):
        _service.stream_stream(implementation, rpc, handler, servicer_context)
    return service


class _Serverish(_common.Serverish):

    def __init__(self, descriptors_to_servicers, time):
        self._descriptors_to_servicers = descriptors_to_servicers
        self._time = time

    def _invoke(
            self, service_behavior, method_descriptor, handler,
            invocation_metadata, deadline):
        implementation = _implementation(
            self._descriptors_to_servicers, method_descriptor)
        rpc = _rpc.Rpc(handler, invocation_metadata)
        if handler.add_termination_callback(rpc.extrinsic_abort):
            servicer_context = _servicer_context.ServicerContext(
                rpc, self._time, deadline)
            service_thread = threading.Thread(
                target=service_behavior,
                args=(implementation, rpc, servicer_context,))
            service_thread.start()

    def invoke_unary_unary(
            self, method_descriptor, handler, invocation_metadata, request,
            deadline):
        self._invoke(
            _unary_unary_service(request), method_descriptor, handler,
            invocation_metadata, deadline)

    def invoke_unary_stream(
            self, method_descriptor, handler, invocation_metadata, request,
            deadline):
        self._invoke(
            _unary_stream_service(request), method_descriptor, handler,
            invocation_metadata, deadline)

    def invoke_stream_unary(
            self, method_descriptor, handler, invocation_metadata, deadline):
        self._invoke(
            _stream_unary_service(handler), method_descriptor, handler,
            invocation_metadata, deadline)

    def invoke_stream_stream(
            self, method_descriptor, handler, invocation_metadata, deadline):
        self._invoke(
            _stream_stream_service(handler), method_descriptor, handler,
            invocation_metadata, deadline)


def _deadline_and_handler(requests_closed, time, timeout):
    if timeout is None:
        return None, _handler.handler_without_deadline(requests_closed)
    else:
        deadline = time.time() + timeout
        handler = _handler.handler_with_deadline(requests_closed, time, deadline)
        return deadline, handler


class _Server(grpc_testing.Server):

    def __init__(self, serverish, time):
        self._serverish = serverish
        self._time = time

    def invoke_unary_unary(
            self, method_descriptor, invocation_metadata, request, timeout):
        deadline, handler = _deadline_and_handler(True, self._time, timeout)
        self._serverish.invoke_unary_unary(
            method_descriptor, handler, invocation_metadata, request, deadline)
        return _server_rpc.UnaryUnaryServerRpc(handler)

    def invoke_unary_stream(
            self, method_descriptor, invocation_metadata, request, timeout):
        deadline, handler = _deadline_and_handler(True, self._time, timeout)
        self._serverish.invoke_unary_stream(
            method_descriptor, handler, invocation_metadata, request, deadline)
        return _server_rpc.UnaryStreamServerRpc(handler)

    def invoke_stream_unary(
            self, method_descriptor, invocation_metadata, timeout):
        deadline, handler = _deadline_and_handler(False, self._time, timeout)
        self._serverish.invoke_stream_unary(
            method_descriptor, handler, invocation_metadata, deadline)
        return _server_rpc.StreamUnaryServerRpc(handler)

    def invoke_stream_stream(
            self, method_descriptor, invocation_metadata, timeout):
        deadline, handler = _deadline_and_handler(False, self._time, timeout)
        self._serverish.invoke_stream_stream(
            method_descriptor, handler, invocation_metadata, deadline)
        return _server_rpc.StreamStreamServerRpc(handler)


def server_from_descriptor_to_servicers(descriptors_to_servicers, time):
    return _Server(_Serverish(descriptors_to_servicers, time), time)

# Copyright 2016, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
"""Implementation of gRPC Python interceptors."""

import collections

import grpc


class _UnaryClientInfo(
        collections.namedtuple('_UnaryClientInfo',
                               ('full_method', 'timeout',))):
    pass


class _StreamClientInfo(
        collections.namedtuple('_StreamClientInfo', (
            'full_method', 'is_client_stream', 'is_server_stream', 'timeout'))):
    pass


class _InterceptorUnaryUnaryMultiCallable(grpc.UnaryUnaryMultiCallable):

    def __init__(self, method, base_callable, interceptor):
        self._method = method
        self._base_callable = base_callable
        self._interceptor = interceptor

    def __call__(self, request, timeout=None, metadata=None, credentials=None):

        def invoker(request, metadata):
            return self._base_callable(request, timeout, metadata, credentials)

        client_info = _UnaryClientInfo(self._method, timeout)
        return self._interceptor.intercept_unary(request, metadata, client_info,
                                                 invoker)

    def with_call(self, request, timeout=None, metadata=None, credentials=None):

        def invoker(request, metadata):
            return self._base_callable.with_call(request, timeout, metadata,
                                                 credentials)

        client_info = _UnaryClientInfo(self._method, timeout)
        return self._interceptor.intercept_unary(request, metadata, client_info,
                                                 invoker)

    def future(self, request, timeout=None, metadata=None, credentials=None):

        def invoker(request, metadata):
            return self._base_callable.future(request, timeout, metadata,
                                              credentials)

        client_info = _UnaryClientInfo(self._method, timeout)
        return self._interceptor.intercept_unary(request, metadata, client_info,
                                                 invoker)


class _InterceptorUnaryStreamMultiCallable(grpc.UnaryStreamMultiCallable):

    def __init__(self, method, base_callable, interceptor):
        self._method = method
        self._base_callable = base_callable
        self._interceptor = interceptor

    def __call__(self, request, timeout=None, metadata=None, credentials=None):

        def invoker(request, metadata):
            return self._base_callable(request, timeout, metadata, credentials)

        client_info = _StreamClientInfo(self._method, False, True, timeout)
        return self._interceptor.intercept_stream(request, metadata,
                                                  client_info, invoker)


class _InterceptorStreamUnaryMultiCallable(grpc.StreamUnaryMultiCallable):

    def __init__(self, method, base_callable, interceptor):
        self._method = method
        self._base_callable = base_callable
        self._interceptor = interceptor

    def __call__(self,
                 request_iterator,
                 timeout=None,
                 metadata=None,
                 credentials=None):

        def invoker(request_iterator, metadata):
            return self._base_callable(request_iterator, timeout, metadata,
                                       credentials)

        client_info = _StreamClientInfo(self._method, True, False, timeout)
        return self._interceptor.intercept_stream(request_iterator, metadata,
                                                  client_info, invoker)

    def with_call(self,
                  request_iterator,
                  timeout=None,
                  metadata=None,
                  credentials=None):

        def invoker(request_iterator, metadata):
            return self._base_callable.with_call(request_iterator, timeout,
                                                 metadata, credentials)

        client_info = _StreamClientInfo(self._method, True, False, timeout)
        return self._interceptor.intercept_stream(request_iterator, metadata,
                                                  client_info, invoker)

    def future(self,
               request_iterator,
               timeout=None,
               metadata=None,
               credentials=None):

        def invoker(request_iterator, metadata):
            return self._base_callable.future(request_iterator, timeout,
                                              metadata, credentials)

        client_info = _StreamClientInfo(self._method, True, False, timeout)
        return self._interceptor.intercept_stream(request_iterator, metadata,
                                                  client_info, invoker)


class _InterceptorStreamStreamMultiCallable(grpc.StreamStreamMultiCallable):

    def __init__(self, method, base_callable, interceptor):
        self._method = method
        self._base_callable = base_callable
        self._interceptor = interceptor

    def __call__(self,
                 request_iterator,
                 timeout=None,
                 metadata=None,
                 credentials=None):

        def invoker(request_iterator, metadata):
            return self._base_callable(request_iterator, timeout, metadata,
                                       credentials)

        client_info = _StreamClientInfo(self._method, True, True, timeout)
        return self._interceptor.intercept_stream(request_iterator, metadata,
                                                  client_info, invoker)


class _InterceptorChannel(grpc.Channel):

    def __init__(self, channel, interceptor):
        self._channel = channel
        self._interceptor = interceptor

    def subscribe(self, *args, **kwargs):
        self._channel.subscribe(*args, **kwargs)

    def unsubscribe(self, *args, **kwargs):
        self._channel.unsubscribe(*args, **kwargs)

    def unary_unary(self,
                    method,
                    request_serializer=None,
                    response_deserializer=None):
        base_callable = self._channel.unary_unary(method, request_serializer,
                                                  response_deserializer)
        if isinstance(self._interceptor, grpc.UnaryClientInterceptor):
            return _InterceptorUnaryUnaryMultiCallable(method, base_callable,
                                                       self._interceptor)
        else:
            return base_callable

    def unary_stream(self,
                     method,
                     request_serializer=None,
                     response_deserializer=None):
        base_callable = self._channel.unary_stream(method, request_serializer,
                                                   response_deserializer)
        if isinstance(self._interceptor, grpc.StreamClientInterceptor):
            return _InterceptorUnaryStreamMultiCallable(method, base_callable,
                                                        self._interceptor)
        else:
            return base_callable

    def stream_unary(self,
                     method,
                     request_serializer=None,
                     response_deserializer=None):
        base_callable = self._channel.stream_unary(method, request_serializer,
                                                   response_deserializer)
        if isinstance(self._interceptor, grpc.StreamClientInterceptor):
            return _InterceptorStreamUnaryMultiCallable(method, base_callable,
                                                        self._interceptor)
        else:
            return base_callable

    def stream_stream(self,
                      method,
                      request_serializer=None,
                      response_deserializer=None):
        base_callable = self._channel.stream_stream(method, request_serializer,
                                                    response_deserializer)
        if isinstance(self._interceptor, grpc.StreamClientInterceptor):
            return _InterceptorStreamStreamMultiCallable(method, base_callable,
                                                         self._interceptor)
        else:
            return base_callable


def intercept_channel(channel, *interceptors):
    result = channel
    for interceptor in interceptors:
        if not isinstance(interceptor, grpc.UnaryClientInterceptor) and \
           not isinstance(interceptor, grpc.StreamClientInterceptor):
            raise TypeError('interceptor must be either a '
                            'grpc.UnaryClientInterceptor or a '
                            'grpc.StreamClientInterceptor')
        result = _InterceptorChannel(result, interceptor)
    return result


class _UnaryServerInfo(
        collections.namedtuple('_UnaryServerInfo', ('full_method',))):
    pass


class _StreamServerInfo(
        collections.namedtuple('_StreamServerInfo', (
            'full_method', 'is_client_stream', 'is_server_stream'))):
    pass


class _InterceptorRpcMethodHandler(grpc.RpcMethodHandler):

    def __init__(self, rpc_method_handler, method, interceptor):
        self._rpc_method_handler = rpc_method_handler
        self._method = method
        self._interceptor = interceptor

    @property
    def request_streaming(self):
        return self._rpc_method_handler.request_streaming

    @property
    def response_streaming(self):
        return self._rpc_method_handler.response_streaming

    @property
    def request_deserializer(self):
        return self._rpc_method_handler.request_deserializer

    @property
    def response_serializer(self):
        return self._rpc_method_handler.response_serializer

    @property
    def unary_unary(self):
        if not isinstance(self._interceptor, grpc.UnaryServerInterceptor):
            return self._rpc_method_handler.unary_unary

        def adaptation(request, servicer_context):

            def handler(request, servicer_context):
                return self._rpc_method_handler.unary_unary(request,
                                                            servicer_context)

            return self._interceptor.intercept_unary(
                request, servicer_context,
                _UnaryServerInfo(self._method), handler)

        return adaptation

    @property
    def unary_stream(self):
        if not isinstance(self._interceptor, grpc.StreamServerInterceptor):
            return self._rpc_method_handler.unary_stream

        def adaptation(request, servicer_context):

            def handler(request, servicer_context):
                return self._rpc_method_handler.unary_stream(request,
                                                             servicer_context)

            return self._interceptor.intercept_stream(
                request, servicer_context,
                _StreamServerInfo(self._method, False, True), handler)

        return adaptation

    @property
    def stream_unary(self):
        if not isinstance(self._interceptor, grpc.StreamServerInterceptor):
            return self._rpc_method_handler.stream_unary

        def adaptation(request_iterator, servicer_context):

            def handler(request_iterator, servicer_context):
                return self._rpc_method_handler.stream_unary(request_iterator,
                                                             servicer_context)

            return self._interceptor.intercept_stream(
                request_iterator, servicer_context,
                _StreamServerInfo(self._method, True, False), handler)

        return adaptation

    @property
    def stream_stream(self):
        if not isinstance(self._interceptor, grpc.StreamServerInterceptor):
            return self._rpc_method_handler.stream_stream

        def adaptation(request_iterator, servicer_context):

            def handler(request_iterator, servicer_context):
                return self._rpc_method_handler.stream_stream(request_iterator,
                                                              servicer_context)

            return self._interceptor.intercept_stream(
                request_iterator, servicer_context,
                _StreamServerInfo(self._method, True, True), handler)

        return adaptation


class _InterceptorGenericRpcHandler(grpc.GenericRpcHandler):

    def __init__(self, generic_rpc_handler, interceptor):
        self.generic_rpc_handler = generic_rpc_handler
        self._interceptor = interceptor

    def service(self, handler_call_details):
        result = self.generic_rpc_handler.service(handler_call_details)
        if result:
            result = _InterceptorRpcMethodHandler(
                result, handler_call_details.method, self._interceptor)
        return result


class _InterceptorServer(grpc.Server):

    def __init__(self, server, interceptor):
        self._server = server
        self._interceptor = interceptor

    def add_generic_rpc_handlers(self, generic_rpc_handlers):
        generic_rpc_handlers = [
            _InterceptorGenericRpcHandler(generic_rpc_handler,
                                          self._interceptor)
            for generic_rpc_handler in generic_rpc_handlers
        ]
        return self._server.add_generic_rpc_handlers(generic_rpc_handlers)

    def add_insecure_port(self, *args, **kwargs):
        return self._server.add_insecure_port(*args, **kwargs)

    def add_secure_port(self, *args, **kwargs):
        return self._server.add_secure_port(*args, **kwargs)

    def start(self, *args, **kwargs):
        return self._server.start(*args, **kwargs)

    def stop(self, *args, **kwargs):
        return self._server.stop(*args, **kwargs)


def intercept_server(server, *interceptors):
    result = server
    for interceptor in interceptors:
        if not isinstance(interceptor, grpc.UnaryServerInterceptor) and \
           not isinstance(interceptor, grpc.StreamServerInterceptor):
            raise TypeError('interceptor must be either a '
                            'grpc.UnaryServerInterceptor or a '
                            'grpc.StreamServerInterceptor')
        result = _InterceptorServer(result, interceptor)
    return result

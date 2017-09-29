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
"""Implementation of gRPC Python interceptors."""

import grpc


class _InterceptingUnaryUnaryMultiCallable(grpc.UnaryUnaryMultiCallable):

    def __init__(self, method, callable_factory, interceptor):
        self._method = method
        self._callable_factory = callable_factory
        self._interceptor = interceptor

    def __call__(self, *args, **kwargs):

        return self.with_call(*args, **kwargs)[0]

    def with_call(self, *args, **kwargs):

        def invoker(method, *args, **kwargs):
            return self._callable_factory(method).with_call(*args, **kwargs)

        return self._interceptor.intercept_unary_unary_call(
            invoker, self._method, *args, **kwargs)

    def future(self, *args, **kwargs):

        def invoker(method, *args, **kwargs):
            return self._callable_factory(method).future(*args, **kwargs)

        return self._interceptor.intercept_unary_unary_future(
            invoker, self._method, *args, **kwargs)


class _InterceptingUnaryStreamMultiCallable(grpc.UnaryStreamMultiCallable):

    def __init__(self, method, callable_factory, interceptor):
        self._method = method
        self._callable_factory = callable_factory
        self._interceptor = interceptor

    def __call__(self, *args, **kwargs):

        def invoker(method, *args, **kwargs):
            return self._callable_factory(method)(*args, **kwargs)

        return self._interceptor.intercept_unary_stream_call(
            invoker, self._method, *args, **kwargs)


class _InterceptingStreamUnaryMultiCallable(grpc.StreamUnaryMultiCallable):

    def __init__(self, method, callable_factory, interceptor):
        self._method = method
        self._callable_factory = callable_factory
        self._interceptor = interceptor

    def __call__(self, *args, **kwargs):

        return self.with_call(*args, **kwargs)[0]

    def with_call(self, *args, **kwargs):

        def invoker(method, *args, **kwargs):
            return self._callable_factory(method).with_call(*args, **kwargs)

        return self._interceptor.intercept_stream_unary_call(
            invoker, self._method, *args, **kwargs)

    def future(self, *args, **kwargs):

        def invoker(method, *args, **kwargs):
            return self._callable_factory(method).future(*args, **kwargs)

        return self._interceptor.intercept_stream_unary_future(
            invoker, self._method, *args, **kwargs)


class _InterceptingStreamStreamMultiCallable(grpc.StreamStreamMultiCallable):

    def __init__(self, method, callable_factory, interceptor):
        self._method = method
        self._callable_factory = callable_factory
        self._interceptor = interceptor

    def __call__(self, *args, **kwargs):

        def invoker(method, *args, **kwargs):
            return self._callable_factory(method)(*args, **kwargs)

        return self._interceptor.intercept_stream_stream_call(
            invoker, self._method, *args, **kwargs)


class _InterceptingChannel(grpc.Channel):

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

        def callable_factory(method):
            return self._channel.unary_unary(method, request_serializer,
                                             response_deserializer)

        if isinstance(self._interceptor, grpc.UnaryUnaryClientInterceptor):
            return _InterceptingUnaryUnaryMultiCallable(
                method, callable_factory, self._interceptor)
        else:
            return callable_factory(method)

    def unary_stream(self,
                     method,
                     request_serializer=None,
                     response_deserializer=None):

        def callable_factory(method):
            return self._channel.unary_stream(method, request_serializer,
                                              response_deserializer)

        if isinstance(self._interceptor, grpc.UnaryStreamClientInterceptor):
            return _InterceptingUnaryStreamMultiCallable(
                method, callable_factory, self._interceptor)
        else:
            return callable_factory(method)

    def stream_unary(self,
                     method,
                     request_serializer=None,
                     response_deserializer=None):

        def callable_factory(method):
            return self._channel.stream_unary(method, request_serializer,
                                              response_deserializer)

        if isinstance(self._interceptor, grpc.StreamUnaryClientInterceptor):
            return _InterceptingStreamUnaryMultiCallable(
                method, callable_factory, self._interceptor)
        else:
            return callable_factory(method)

    def stream_stream(self,
                      method,
                      request_serializer=None,
                      response_deserializer=None):

        def callable_factory(method):
            return self._channel.stream_stream(method, request_serializer,
                                               response_deserializer)

        if isinstance(self._interceptor, grpc.StreamStreamClientInterceptor):
            return _InterceptingStreamStreamMultiCallable(
                method, callable_factory, self._interceptor)
        else:
            return callable_factory(method)


def intercept_channel(channel, *interceptors):
    for interceptor in reversed(list(interceptors)):
        if not isinstance(interceptor, grpc.UnaryUnaryClientInterceptor) and \
           not isinstance(interceptor, grpc.UnaryStreamClientInterceptor) and \
           not isinstance(interceptor, grpc.StreamUnaryClientInterceptor) and \
           not isinstance(interceptor, grpc.StreamStreamClientInterceptor):
            raise TypeError('interceptor must be '
                            'grpc.UnaryUnaryClientInterceptor or '
                            'grpc.UnaryStreamClientInterceptor or '
                            'grpc.StreamUnaryClientInterceptor or '
                            'grpc.StreamStreamClientInterceptor or ')
        channel = _InterceptingChannel(channel, interceptor)
    return channel

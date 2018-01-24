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

import collections
import sys

import grpc


class _ServicePipeline(object):

    def __init__(self, interceptors):
        self.interceptors = tuple(interceptors)

    def _continuation(self, thunk, index):
        return lambda context: self._intercept_at(thunk, index, context)

    def _intercept_at(self, thunk, index, context):
        if index < len(self.interceptors):
            interceptor = self.interceptors[index]
            thunk = self._continuation(thunk, index + 1)
            return interceptor.intercept_service(thunk, context)
        else:
            return thunk(context)

    def execute(self, thunk, context):
        return self._intercept_at(thunk, 0, context)


def service_pipeline(interceptors):
    return _ServicePipeline(interceptors) if interceptors else None


class _ClientCallDetails(
        collections.namedtuple(
            '_ClientCallDetails',
            ('method', 'timeout', 'metadata', 'credentials')),
        grpc.ClientCallDetails):
    pass


def _unwrap_client_call_details(call_details, default_details):
    try:
        method = call_details.method
    except AttributeError:
        method = default_details.method

    try:
        timeout = call_details.timeout
    except AttributeError:
        timeout = default_details.timeout

    try:
        metadata = call_details.metadata
    except AttributeError:
        metadata = default_details.metadata

    try:
        credentials = call_details.credentials
    except AttributeError:
        credentials = default_details.credentials

    return method, timeout, metadata, credentials


class _LocalFailure(grpc.RpcError, grpc.Future, grpc.Call):

    def __init__(self, exception, traceback):
        super(_LocalFailure, self).__init__()
        self._exception = exception
        self._traceback = traceback

    def initial_metadata(self):
        return None

    def trailing_metadata(self):
        return None

    def code(self):
        return grpc.StatusCode.INTERNAL

    def details(self):
        return 'Exception raised while intercepting the RPC'

    def cancel(self):
        return False

    def cancelled(self):
        return False

    def running(self):
        return False

    def done(self):
        return True

    def result(self, ignored_timeout=None):
        raise self._exception

    def exception(self, ignored_timeout=None):
        return self._exception

    def traceback(self, ignored_timeout=None):
        return self._traceback

    def add_done_callback(self, fn):
        fn(self)

    def __iter__(self):
        return self

    def next(self):
        raise self._exception


class _UnaryUnaryMultiCallable(grpc.UnaryUnaryMultiCallable):

    def __init__(self, thunk, method, interceptor):
        self._thunk = thunk
        self._method = method
        self._interceptor = interceptor

    def __call__(self, request, timeout=None, metadata=None, credentials=None):
        call_future = self.future(
            request,
            timeout=timeout,
            metadata=metadata,
            credentials=credentials)
        return call_future.result()

    def with_call(self, request, timeout=None, metadata=None, credentials=None):
        call_future = self.future(
            request,
            timeout=timeout,
            metadata=metadata,
            credentials=credentials)
        return call_future.result(), call_future

    def future(self, request, timeout=None, metadata=None, credentials=None):

        client_call_details = _ClientCallDetails(self._method, timeout,
                                                 metadata, credentials)

        def continuation(new_details, request):
            new_method, new_timeout, new_metadata, new_credentials = (
                _unwrap_client_call_details(new_details, client_call_details))
            return self._thunk(new_method).future(
                request,
                timeout=new_timeout,
                metadata=new_metadata,
                credentials=new_credentials)

        try:
            return self._interceptor.intercept_unary_unary(
                continuation, client_call_details, request)
        except Exception as exception:  # pylint:disable=broad-except
            return _LocalFailure(exception, sys.exc_info()[2])


class _UnaryStreamMultiCallable(grpc.UnaryStreamMultiCallable):

    def __init__(self, thunk, method, interceptor):
        self._thunk = thunk
        self._method = method
        self._interceptor = interceptor

    def __call__(self, request, timeout=None, metadata=None, credentials=None):
        client_call_details = _ClientCallDetails(self._method, timeout,
                                                 metadata, credentials)

        def continuation(new_details, request):
            new_method, new_timeout, new_metadata, new_credentials = (
                _unwrap_client_call_details(new_details, client_call_details))
            return self._thunk(new_method)(
                request,
                timeout=new_timeout,
                metadata=new_metadata,
                credentials=new_credentials)

        try:
            return self._interceptor.intercept_unary_stream(
                continuation, client_call_details, request)
        except Exception as exception:  # pylint:disable=broad-except
            return _LocalFailure(exception, sys.exc_info()[2])


class _StreamUnaryMultiCallable(grpc.StreamUnaryMultiCallable):

    def __init__(self, thunk, method, interceptor):
        self._thunk = thunk
        self._method = method
        self._interceptor = interceptor

    def __call__(self,
                 request_iterator,
                 timeout=None,
                 metadata=None,
                 credentials=None):
        call_future = self.future(
            request_iterator,
            timeout=timeout,
            metadata=metadata,
            credentials=credentials)
        return call_future.result()

    def with_call(self,
                  request_iterator,
                  timeout=None,
                  metadata=None,
                  credentials=None):
        call_future = self.future(
            request_iterator,
            timeout=timeout,
            metadata=metadata,
            credentials=credentials)
        return call_future.result(), call_future

    def future(self,
               request_iterator,
               timeout=None,
               metadata=None,
               credentials=None):
        client_call_details = _ClientCallDetails(self._method, timeout,
                                                 metadata, credentials)

        def continuation(new_details, request_iterator):
            new_method, new_timeout, new_metadata, new_credentials = (
                _unwrap_client_call_details(new_details, client_call_details))
            return self._thunk(new_method).future(
                request_iterator,
                timeout=new_timeout,
                metadata=new_metadata,
                credentials=new_credentials)

        try:
            return self._interceptor.intercept_stream_unary(
                continuation, client_call_details, request_iterator)
        except Exception as exception:  # pylint:disable=broad-except
            return _LocalFailure(exception, sys.exc_info()[2])


class _StreamStreamMultiCallable(grpc.StreamStreamMultiCallable):

    def __init__(self, thunk, method, interceptor):
        self._thunk = thunk
        self._method = method
        self._interceptor = interceptor

    def __call__(self,
                 request_iterator,
                 timeout=None,
                 metadata=None,
                 credentials=None):
        client_call_details = _ClientCallDetails(self._method, timeout,
                                                 metadata, credentials)

        def continuation(new_details, request_iterator):
            new_method, new_timeout, new_metadata, new_credentials = (
                _unwrap_client_call_details(new_details, client_call_details))
            return self._thunk(new_method)(
                request_iterator,
                timeout=new_timeout,
                metadata=new_metadata,
                credentials=new_credentials)

        try:
            return self._interceptor.intercept_stream_stream(
                continuation, client_call_details, request_iterator)
        except Exception as exception:  # pylint:disable=broad-except
            return _LocalFailure(exception, sys.exc_info()[2])


class _Channel(grpc.Channel):

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
        thunk = lambda m: self._channel.unary_unary(m, request_serializer, response_deserializer)
        if isinstance(self._interceptor, grpc.UnaryUnaryClientInterceptor):
            return _UnaryUnaryMultiCallable(thunk, method, self._interceptor)
        else:
            return thunk(method)

    def unary_stream(self,
                     method,
                     request_serializer=None,
                     response_deserializer=None):
        thunk = lambda m: self._channel.unary_stream(m, request_serializer, response_deserializer)
        if isinstance(self._interceptor, grpc.UnaryStreamClientInterceptor):
            return _UnaryStreamMultiCallable(thunk, method, self._interceptor)
        else:
            return thunk(method)

    def stream_unary(self,
                     method,
                     request_serializer=None,
                     response_deserializer=None):
        thunk = lambda m: self._channel.stream_unary(m, request_serializer, response_deserializer)
        if isinstance(self._interceptor, grpc.StreamUnaryClientInterceptor):
            return _StreamUnaryMultiCallable(thunk, method, self._interceptor)
        else:
            return thunk(method)

    def stream_stream(self,
                      method,
                      request_serializer=None,
                      response_deserializer=None):
        thunk = lambda m: self._channel.stream_stream(m, request_serializer, response_deserializer)
        if isinstance(self._interceptor, grpc.StreamStreamClientInterceptor):
            return _StreamStreamMultiCallable(thunk, method, self._interceptor)
        else:
            return thunk(method)


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
        channel = _Channel(channel, interceptor)
    return channel

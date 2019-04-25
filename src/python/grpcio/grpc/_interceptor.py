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
            ('method', 'timeout', 'metadata', 'credentials', 'wait_for_ready')),
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

    try:
        wait_for_ready = call_details.wait_for_ready
    except AttributeError:
        wait_for_ready = default_details.wait_for_ready

    return method, timeout, metadata, credentials, wait_for_ready


class _FailureOutcome(grpc.RpcError, grpc.Future, grpc.Call):  # pylint: disable=too-many-ancestors

    def __init__(self, exception, traceback):
        super(_FailureOutcome, self).__init__()
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

    def is_active(self):
        return False

    def time_remaining(self):
        return None

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

    def add_callback(self, unused_callback):
        return False

    def add_done_callback(self, fn):
        fn(self)

    def __iter__(self):
        return self

    def __next__(self):
        raise self._exception

    def next(self):
        return self.__next__()


class _UnaryOutcome(grpc.Call, grpc.Future):

    def __init__(self, response, call):
        self._response = response
        self._call = call

    def initial_metadata(self):
        return self._call.initial_metadata()

    def trailing_metadata(self):
        return self._call.trailing_metadata()

    def code(self):
        return self._call.code()

    def details(self):
        return self._call.details()

    def is_active(self):
        return self._call.is_active()

    def time_remaining(self):
        return self._call.time_remaining()

    def cancel(self):
        return self._call.cancel()

    def add_callback(self, callback):
        return self._call.add_callback(callback)

    def cancelled(self):
        return False

    def running(self):
        return False

    def done(self):
        return True

    def result(self, ignored_timeout=None):
        return self._response

    def exception(self, ignored_timeout=None):
        return None

    def traceback(self, ignored_timeout=None):
        return None

    def add_done_callback(self, fn):
        fn(self)


class _UnaryUnaryMultiCallable(grpc.UnaryUnaryMultiCallable):

    def __init__(self, thunk, method, interceptor):
        self._thunk = thunk
        self._method = method
        self._interceptor = interceptor

    def __call__(self,
                 request,
                 timeout=None,
                 metadata=None,
                 credentials=None,
                 wait_for_ready=None):
        response, ignored_call = self._with_call(
            request,
            timeout=timeout,
            metadata=metadata,
            credentials=credentials,
            wait_for_ready=wait_for_ready)
        return response

    def _with_call(self,
                   request,
                   timeout=None,
                   metadata=None,
                   credentials=None,
                   wait_for_ready=None):
        client_call_details = _ClientCallDetails(
            self._method, timeout, metadata, credentials, wait_for_ready)

        def continuation(new_details, request):
            new_method, new_timeout, new_metadata, new_credentials, new_wait_for_ready = (
                _unwrap_client_call_details(new_details, client_call_details))
            try:
                response, call = self._thunk(new_method).with_call(
                    request,
                    timeout=new_timeout,
                    metadata=new_metadata,
                    credentials=new_credentials,
                    wait_for_ready=new_wait_for_ready)
                return _UnaryOutcome(response, call)
            except grpc.RpcError as rpc_error:
                return rpc_error
            except Exception as exception:  # pylint:disable=broad-except
                return _FailureOutcome(exception, sys.exc_info()[2])

        call = self._interceptor.intercept_unary_unary(
            continuation, client_call_details, request)
        return call.result(), call

    def with_call(self,
                  request,
                  timeout=None,
                  metadata=None,
                  credentials=None,
                  wait_for_ready=None):
        return self._with_call(
            request,
            timeout=timeout,
            metadata=metadata,
            credentials=credentials,
            wait_for_ready=wait_for_ready)

    def future(self,
               request,
               timeout=None,
               metadata=None,
               credentials=None,
               wait_for_ready=None):
        client_call_details = _ClientCallDetails(
            self._method, timeout, metadata, credentials, wait_for_ready)

        def continuation(new_details, request):
            new_method, new_timeout, new_metadata, new_credentials, new_wait_for_ready = (
                _unwrap_client_call_details(new_details, client_call_details))
            return self._thunk(new_method).future(
                request,
                timeout=new_timeout,
                metadata=new_metadata,
                credentials=new_credentials,
                wait_for_ready=new_wait_for_ready)

        try:
            return self._interceptor.intercept_unary_unary(
                continuation, client_call_details, request)
        except Exception as exception:  # pylint:disable=broad-except
            return _FailureOutcome(exception, sys.exc_info()[2])


class _UnaryStreamMultiCallable(grpc.UnaryStreamMultiCallable):

    def __init__(self, thunk, method, interceptor):
        self._thunk = thunk
        self._method = method
        self._interceptor = interceptor

    def __call__(self,
                 request,
                 timeout=None,
                 metadata=None,
                 credentials=None,
                 wait_for_ready=None):
        client_call_details = _ClientCallDetails(
            self._method, timeout, metadata, credentials, wait_for_ready)

        def continuation(new_details, request):
            new_method, new_timeout, new_metadata, new_credentials, new_wait_for_ready = (
                _unwrap_client_call_details(new_details, client_call_details))
            return self._thunk(new_method)(
                request,
                timeout=new_timeout,
                metadata=new_metadata,
                credentials=new_credentials,
                wait_for_ready=new_wait_for_ready)

        try:
            return self._interceptor.intercept_unary_stream(
                continuation, client_call_details, request)
        except Exception as exception:  # pylint:disable=broad-except
            return _FailureOutcome(exception, sys.exc_info()[2])


class _StreamUnaryMultiCallable(grpc.StreamUnaryMultiCallable):

    def __init__(self, thunk, method, interceptor):
        self._thunk = thunk
        self._method = method
        self._interceptor = interceptor

    def __call__(self,
                 request_iterator,
                 timeout=None,
                 metadata=None,
                 credentials=None,
                 wait_for_ready=None):
        response, ignored_call = self._with_call(
            request_iterator,
            timeout=timeout,
            metadata=metadata,
            credentials=credentials,
            wait_for_ready=wait_for_ready)
        return response

    def _with_call(self,
                   request_iterator,
                   timeout=None,
                   metadata=None,
                   credentials=None,
                   wait_for_ready=None):
        client_call_details = _ClientCallDetails(
            self._method, timeout, metadata, credentials, wait_for_ready)

        def continuation(new_details, request_iterator):
            new_method, new_timeout, new_metadata, new_credentials, new_wait_for_ready = (
                _unwrap_client_call_details(new_details, client_call_details))
            try:
                response, call = self._thunk(new_method).with_call(
                    request_iterator,
                    timeout=new_timeout,
                    metadata=new_metadata,
                    credentials=new_credentials,
                    wait_for_ready=new_wait_for_ready)
                return _UnaryOutcome(response, call)
            except grpc.RpcError as rpc_error:
                return rpc_error
            except Exception as exception:  # pylint:disable=broad-except
                return _FailureOutcome(exception, sys.exc_info()[2])

        call = self._interceptor.intercept_stream_unary(
            continuation, client_call_details, request_iterator)
        return call.result(), call

    def with_call(self,
                  request_iterator,
                  timeout=None,
                  metadata=None,
                  credentials=None,
                  wait_for_ready=None):
        return self._with_call(
            request_iterator,
            timeout=timeout,
            metadata=metadata,
            credentials=credentials,
            wait_for_ready=wait_for_ready)

    def future(self,
               request_iterator,
               timeout=None,
               metadata=None,
               credentials=None,
               wait_for_ready=None):
        client_call_details = _ClientCallDetails(
            self._method, timeout, metadata, credentials, wait_for_ready)

        def continuation(new_details, request_iterator):
            new_method, new_timeout, new_metadata, new_credentials, new_wait_for_ready = (
                _unwrap_client_call_details(new_details, client_call_details))
            return self._thunk(new_method).future(
                request_iterator,
                timeout=new_timeout,
                metadata=new_metadata,
                credentials=new_credentials,
                wait_for_ready=new_wait_for_ready)

        try:
            return self._interceptor.intercept_stream_unary(
                continuation, client_call_details, request_iterator)
        except Exception as exception:  # pylint:disable=broad-except
            return _FailureOutcome(exception, sys.exc_info()[2])


class _StreamStreamMultiCallable(grpc.StreamStreamMultiCallable):

    def __init__(self, thunk, method, interceptor):
        self._thunk = thunk
        self._method = method
        self._interceptor = interceptor

    def __call__(self,
                 request_iterator,
                 timeout=None,
                 metadata=None,
                 credentials=None,
                 wait_for_ready=None):
        client_call_details = _ClientCallDetails(
            self._method, timeout, metadata, credentials, wait_for_ready)

        def continuation(new_details, request_iterator):
            new_method, new_timeout, new_metadata, new_credentials, new_wait_for_ready = (
                _unwrap_client_call_details(new_details, client_call_details))
            return self._thunk(new_method)(
                request_iterator,
                timeout=new_timeout,
                metadata=new_metadata,
                credentials=new_credentials,
                wait_for_ready=new_wait_for_ready)

        try:
            return self._interceptor.intercept_stream_stream(
                continuation, client_call_details, request_iterator)
        except Exception as exception:  # pylint:disable=broad-except
            return _FailureOutcome(exception, sys.exc_info()[2])


class _Channel(grpc.Channel):

    def __init__(self, channel, interceptor):
        self._channel = channel
        self._interceptor = interceptor

    def subscribe(self, callback, try_to_connect=False):
        self._channel.subscribe(callback, try_to_connect=try_to_connect)

    def unsubscribe(self, callback):
        self._channel.unsubscribe(callback)

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

    def _close(self):
        self._channel.close()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self._close()
        return False

    def close(self):
        self._channel.close()


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

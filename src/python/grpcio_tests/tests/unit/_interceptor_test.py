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
"""Test of gRPC Python interceptors."""

import collections
import itertools
import threading
import unittest
import logging
import os
from concurrent import futures

import grpc
from grpc.framework.foundation import logging_pool

from tests.unit import test_common
from tests.unit.framework.common import test_constants
from tests.unit.framework.common import test_control

_SERIALIZE_REQUEST = lambda bytestring: bytestring * 2
_DESERIALIZE_REQUEST = lambda bytestring: bytestring[len(bytestring) // 2:]
_SERIALIZE_RESPONSE = lambda bytestring: bytestring * 3
_DESERIALIZE_RESPONSE = lambda bytestring: bytestring[:len(bytestring) // 3]

_EXCEPTION_REQUEST = b'\x09\x0a'

_UNARY_UNARY = '/test/UnaryUnary'
_UNARY_STREAM = '/test/UnaryStream'
_STREAM_UNARY = '/test/StreamUnary'
_STREAM_STREAM = '/test/StreamStream'


class _ApplicationErrorStandin(Exception):
    pass


class _Callback(object):

    def __init__(self):
        self._condition = threading.Condition()
        self._value = None
        self._called = False

    def __call__(self, value):
        with self._condition:
            self._value = value
            self._called = True
            self._condition.notify_all()

    def value(self):
        with self._condition:
            while not self._called:
                self._condition.wait()
            return self._value


class _Handler(object):

    def __init__(self, control):
        self._control = control

    def handle_unary_unary(self, request, servicer_context):
        self._control.control()
        if servicer_context is not None:
            servicer_context.set_trailing_metadata(((
                'testkey',
                'testvalue',
            ),))
        if request == _EXCEPTION_REQUEST:
            raise _ApplicationErrorStandin()
        return request

    def handle_unary_stream(self, request, servicer_context):
        if request == _EXCEPTION_REQUEST:
            raise _ApplicationErrorStandin()
        for _ in range(test_constants.STREAM_LENGTH):
            self._control.control()
            yield request
        self._control.control()
        if servicer_context is not None:
            servicer_context.set_trailing_metadata(((
                'testkey',
                'testvalue',
            ),))

    def handle_stream_unary(self, request_iterator, servicer_context):
        if servicer_context is not None:
            servicer_context.invocation_metadata()
        self._control.control()
        response_elements = []
        for request in request_iterator:
            self._control.control()
            response_elements.append(request)
        self._control.control()
        if servicer_context is not None:
            servicer_context.set_trailing_metadata(((
                'testkey',
                'testvalue',
            ),))
        if _EXCEPTION_REQUEST in response_elements:
            raise _ApplicationErrorStandin()
        return b''.join(response_elements)

    def handle_stream_stream(self, request_iterator, servicer_context):
        self._control.control()
        if servicer_context is not None:
            servicer_context.set_trailing_metadata(((
                'testkey',
                'testvalue',
            ),))
        for request in request_iterator:
            if request == _EXCEPTION_REQUEST:
                raise _ApplicationErrorStandin()
            self._control.control()
            yield request
        self._control.control()


class _MethodHandler(grpc.RpcMethodHandler):

    def __init__(self, request_streaming, response_streaming,
                 request_deserializer, response_serializer, unary_unary,
                 unary_stream, stream_unary, stream_stream):
        self.request_streaming = request_streaming
        self.response_streaming = response_streaming
        self.request_deserializer = request_deserializer
        self.response_serializer = response_serializer
        self.unary_unary = unary_unary
        self.unary_stream = unary_stream
        self.stream_unary = stream_unary
        self.stream_stream = stream_stream


class _GenericHandler(grpc.GenericRpcHandler):

    def __init__(self, handler):
        self._handler = handler

    def service(self, handler_call_details):
        if handler_call_details.method == _UNARY_UNARY:
            return _MethodHandler(False, False, None, None,
                                  self._handler.handle_unary_unary, None, None,
                                  None)
        elif handler_call_details.method == _UNARY_STREAM:
            return _MethodHandler(False, True, _DESERIALIZE_REQUEST,
                                  _SERIALIZE_RESPONSE, None,
                                  self._handler.handle_unary_stream, None, None)
        elif handler_call_details.method == _STREAM_UNARY:
            return _MethodHandler(True, False, _DESERIALIZE_REQUEST,
                                  _SERIALIZE_RESPONSE, None, None,
                                  self._handler.handle_stream_unary, None)
        elif handler_call_details.method == _STREAM_STREAM:
            return _MethodHandler(True, True, None, None, None, None, None,
                                  self._handler.handle_stream_stream)
        else:
            return None


def _unary_unary_multi_callable(channel):
    return channel.unary_unary(_UNARY_UNARY)


def _unary_stream_multi_callable(channel):
    return channel.unary_stream(_UNARY_STREAM,
                                request_serializer=_SERIALIZE_REQUEST,
                                response_deserializer=_DESERIALIZE_RESPONSE)


def _stream_unary_multi_callable(channel):
    return channel.stream_unary(_STREAM_UNARY,
                                request_serializer=_SERIALIZE_REQUEST,
                                response_deserializer=_DESERIALIZE_RESPONSE)


def _stream_stream_multi_callable(channel):
    return channel.stream_stream(_STREAM_STREAM)


class _ClientCallDetails(
        collections.namedtuple(
            '_ClientCallDetails',
            ('method', 'timeout', 'metadata', 'credentials')),
        grpc.ClientCallDetails):
    pass


class _GenericClientInterceptor(grpc.UnaryUnaryClientInterceptor,
                                grpc.UnaryStreamClientInterceptor,
                                grpc.StreamUnaryClientInterceptor,
                                grpc.StreamStreamClientInterceptor):

    def __init__(self, interceptor_function):
        self._fn = interceptor_function

    def intercept_unary_unary(self, continuation, client_call_details, request):
        new_details, new_request_iterator, postprocess = self._fn(
            client_call_details, iter((request,)), False, False)
        response = continuation(new_details, next(new_request_iterator))
        return postprocess(response) if postprocess else response

    def intercept_unary_stream(self, continuation, client_call_details,
                               request):
        new_details, new_request_iterator, postprocess = self._fn(
            client_call_details, iter((request,)), False, True)
        response_it = continuation(new_details, new_request_iterator)
        return postprocess(response_it) if postprocess else response_it

    def intercept_stream_unary(self, continuation, client_call_details,
                               request_iterator):
        new_details, new_request_iterator, postprocess = self._fn(
            client_call_details, request_iterator, True, False)
        response = continuation(new_details, next(new_request_iterator))
        return postprocess(response) if postprocess else response

    def intercept_stream_stream(self, continuation, client_call_details,
                                request_iterator):
        new_details, new_request_iterator, postprocess = self._fn(
            client_call_details, request_iterator, True, True)
        response_it = continuation(new_details, new_request_iterator)
        return postprocess(response_it) if postprocess else response_it


class _LoggingInterceptor(grpc.ServerInterceptor,
                          grpc.UnaryUnaryClientInterceptor,
                          grpc.UnaryStreamClientInterceptor,
                          grpc.StreamUnaryClientInterceptor,
                          grpc.StreamStreamClientInterceptor):

    def __init__(self, tag, record):
        self.tag = tag
        self.record = record

    def intercept_service(self, continuation, handler_call_details):
        self.record.append(self.tag + ':intercept_service')
        return continuation(handler_call_details)

    def intercept_unary_unary(self, continuation, client_call_details, request):
        self.record.append(self.tag + ':intercept_unary_unary')
        result = continuation(client_call_details, request)
        assert isinstance(
            result,
            grpc.Call), '{} ({}) is not an instance of grpc.Call'.format(
                result, type(result))
        assert isinstance(
            result,
            grpc.Future), '{} ({}) is not an instance of grpc.Future'.format(
                result, type(result))
        return result

    def intercept_unary_stream(self, continuation, client_call_details,
                               request):
        self.record.append(self.tag + ':intercept_unary_stream')
        return continuation(client_call_details, request)

    def intercept_stream_unary(self, continuation, client_call_details,
                               request_iterator):
        self.record.append(self.tag + ':intercept_stream_unary')
        result = continuation(client_call_details, request_iterator)
        assert isinstance(
            result,
            grpc.Call), '{} is not an instance of grpc.Call'.format(result)
        assert isinstance(
            result,
            grpc.Future), '{} is not an instance of grpc.Future'.format(result)
        return result

    def intercept_stream_stream(self, continuation, client_call_details,
                                request_iterator):
        self.record.append(self.tag + ':intercept_stream_stream')
        return continuation(client_call_details, request_iterator)


class _DefectiveClientInterceptor(grpc.UnaryUnaryClientInterceptor):

    def intercept_unary_unary(self, ignored_continuation,
                              ignored_client_call_details, ignored_request):
        raise test_control.Defect()


def _wrap_request_iterator_stream_interceptor(wrapper):

    def intercept_call(client_call_details, request_iterator, request_streaming,
                       ignored_response_streaming):
        if request_streaming:
            return client_call_details, wrapper(request_iterator), None
        else:
            return client_call_details, request_iterator, None

    return _GenericClientInterceptor(intercept_call)


def _append_request_header_interceptor(header, value):

    def intercept_call(client_call_details, request_iterator,
                       ignored_request_streaming, ignored_response_streaming):
        metadata = []
        if client_call_details.metadata:
            metadata = list(client_call_details.metadata)
        metadata.append((
            header,
            value,
        ))
        client_call_details = _ClientCallDetails(
            client_call_details.method, client_call_details.timeout, metadata,
            client_call_details.credentials)
        return client_call_details, request_iterator, None

    return _GenericClientInterceptor(intercept_call)


class _GenericServerInterceptor(grpc.ServerInterceptor):

    def __init__(self, fn):
        self._fn = fn

    def intercept_service(self, continuation, handler_call_details):
        return self._fn(continuation, handler_call_details)


def _filter_server_interceptor(condition, interceptor):

    def intercept_service(continuation, handler_call_details):
        if condition(handler_call_details):
            return interceptor.intercept_service(continuation,
                                                 handler_call_details)
        return continuation(handler_call_details)

    return _GenericServerInterceptor(intercept_service)


class InterceptorTest(unittest.TestCase):

    def setUp(self):
        self._control = test_control.PauseFailControl()
        self._handler = _Handler(self._control)
        self._server_pool = logging_pool.pool(test_constants.THREAD_CONCURRENCY)

        self._record = []
        conditional_interceptor = _filter_server_interceptor(
            lambda x: ('secret', '42') in x.invocation_metadata,
            _LoggingInterceptor('s3', self._record))

        self._server = grpc.server(self._server_pool,
                                   options=(('grpc.so_reuseport', 0),),
                                   interceptors=(
                                       _LoggingInterceptor('s1', self._record),
                                       conditional_interceptor,
                                       _LoggingInterceptor('s2', self._record),
                                   ))
        port = self._server.add_insecure_port('[::]:0')
        self._server.add_generic_rpc_handlers((_GenericHandler(self._handler),))
        self._server.start()

        self._channel = grpc.insecure_channel('localhost:%d' % port)

    def tearDown(self):
        self._server.stop(None)
        self._server_pool.shutdown(wait=True)
        self._channel.close()

    def testTripleRequestMessagesClientInterceptor(self):

        def triple(request_iterator):
            while True:
                try:
                    item = next(request_iterator)
                    yield item
                    yield item
                    yield item
                except StopIteration:
                    break

        interceptor = _wrap_request_iterator_stream_interceptor(triple)
        channel = grpc.intercept_channel(self._channel, interceptor)
        requests = tuple(
            b'\x07\x08' for _ in range(test_constants.STREAM_LENGTH))

        multi_callable = _stream_stream_multi_callable(channel)
        response_iterator = multi_callable(
            iter(requests),
            metadata=(
                ('test',
                 'InterceptedStreamRequestBlockingUnaryResponseWithCall'),))

        responses = tuple(response_iterator)
        self.assertEqual(len(responses), 3 * test_constants.STREAM_LENGTH)

        multi_callable = _stream_stream_multi_callable(self._channel)
        response_iterator = multi_callable(
            iter(requests),
            metadata=(
                ('test',
                 'InterceptedStreamRequestBlockingUnaryResponseWithCall'),))

        responses = tuple(response_iterator)
        self.assertEqual(len(responses), test_constants.STREAM_LENGTH)

    def testDefectiveClientInterceptor(self):
        interceptor = _DefectiveClientInterceptor()
        defective_channel = grpc.intercept_channel(self._channel, interceptor)

        request = b'\x07\x08'

        multi_callable = _unary_unary_multi_callable(defective_channel)
        call_future = multi_callable.future(
            request,
            metadata=(('test',
                       'InterceptedUnaryRequestBlockingUnaryResponse'),))

        self.assertIsNotNone(call_future.exception())
        self.assertEqual(call_future.code(), grpc.StatusCode.INTERNAL)

    def testInterceptedHeaderManipulationWithServerSideVerification(self):
        request = b'\x07\x08'

        channel = grpc.intercept_channel(
            self._channel, _append_request_header_interceptor('secret', '42'))
        channel = grpc.intercept_channel(
            channel, _LoggingInterceptor('c1', self._record),
            _LoggingInterceptor('c2', self._record))

        self._record[:] = []

        multi_callable = _unary_unary_multi_callable(channel)
        multi_callable.with_call(
            request,
            metadata=(
                ('test',
                 'InterceptedUnaryRequestBlockingUnaryResponseWithCall'),))

        self.assertSequenceEqual(self._record, [
            'c1:intercept_unary_unary', 'c2:intercept_unary_unary',
            's1:intercept_service', 's3:intercept_service',
            's2:intercept_service'
        ])

    def testInterceptedUnaryRequestBlockingUnaryResponse(self):
        request = b'\x07\x08'

        self._record[:] = []

        channel = grpc.intercept_channel(
            self._channel, _LoggingInterceptor('c1', self._record),
            _LoggingInterceptor('c2', self._record))

        multi_callable = _unary_unary_multi_callable(channel)
        multi_callable(
            request,
            metadata=(('test',
                       'InterceptedUnaryRequestBlockingUnaryResponse'),))

        self.assertSequenceEqual(self._record, [
            'c1:intercept_unary_unary', 'c2:intercept_unary_unary',
            's1:intercept_service', 's2:intercept_service'
        ])

    def testInterceptedUnaryRequestBlockingUnaryResponseWithError(self):
        request = _EXCEPTION_REQUEST

        self._record[:] = []

        channel = grpc.intercept_channel(
            self._channel, _LoggingInterceptor('c1', self._record),
            _LoggingInterceptor('c2', self._record))

        multi_callable = _unary_unary_multi_callable(channel)
        with self.assertRaises(grpc.RpcError) as exception_context:
            multi_callable(
                request,
                metadata=(('test',
                           'InterceptedUnaryRequestBlockingUnaryResponse'),))
        exception = exception_context.exception
        self.assertFalse(exception.cancelled())
        self.assertFalse(exception.running())
        self.assertTrue(exception.done())
        with self.assertRaises(grpc.RpcError):
            exception.result()
        self.assertIsInstance(exception.exception(), grpc.RpcError)

    def testInterceptedUnaryRequestBlockingUnaryResponseWithCall(self):
        request = b'\x07\x08'

        channel = grpc.intercept_channel(
            self._channel, _LoggingInterceptor('c1', self._record),
            _LoggingInterceptor('c2', self._record))

        self._record[:] = []

        multi_callable = _unary_unary_multi_callable(channel)
        multi_callable.with_call(
            request,
            metadata=(
                ('test',
                 'InterceptedUnaryRequestBlockingUnaryResponseWithCall'),))

        self.assertSequenceEqual(self._record, [
            'c1:intercept_unary_unary', 'c2:intercept_unary_unary',
            's1:intercept_service', 's2:intercept_service'
        ])

    def testInterceptedUnaryRequestFutureUnaryResponse(self):
        request = b'\x07\x08'

        self._record[:] = []
        channel = grpc.intercept_channel(
            self._channel, _LoggingInterceptor('c1', self._record),
            _LoggingInterceptor('c2', self._record))

        multi_callable = _unary_unary_multi_callable(channel)
        response_future = multi_callable.future(
            request,
            metadata=(('test', 'InterceptedUnaryRequestFutureUnaryResponse'),))
        response_future.result()

        self.assertSequenceEqual(self._record, [
            'c1:intercept_unary_unary', 'c2:intercept_unary_unary',
            's1:intercept_service', 's2:intercept_service'
        ])

    def testInterceptedUnaryRequestStreamResponse(self):
        request = b'\x37\x58'

        self._record[:] = []
        channel = grpc.intercept_channel(
            self._channel, _LoggingInterceptor('c1', self._record),
            _LoggingInterceptor('c2', self._record))

        multi_callable = _unary_stream_multi_callable(channel)
        response_iterator = multi_callable(
            request,
            metadata=(('test', 'InterceptedUnaryRequestStreamResponse'),))
        tuple(response_iterator)

        self.assertSequenceEqual(self._record, [
            'c1:intercept_unary_stream', 'c2:intercept_unary_stream',
            's1:intercept_service', 's2:intercept_service'
        ])

    def testInterceptedUnaryRequestStreamResponseWithError(self):
        request = _EXCEPTION_REQUEST

        self._record[:] = []
        channel = grpc.intercept_channel(
            self._channel, _LoggingInterceptor('c1', self._record),
            _LoggingInterceptor('c2', self._record))

        multi_callable = _unary_stream_multi_callable(channel)
        response_iterator = multi_callable(
            request,
            metadata=(('test', 'InterceptedUnaryRequestStreamResponse'),))
        with self.assertRaises(grpc.RpcError) as exception_context:
            tuple(response_iterator)
        exception = exception_context.exception
        self.assertFalse(exception.cancelled())
        self.assertFalse(exception.running())
        self.assertTrue(exception.done())
        with self.assertRaises(grpc.RpcError):
            exception.result()
        self.assertIsInstance(exception.exception(), grpc.RpcError)

    def testInterceptedStreamRequestBlockingUnaryResponse(self):
        requests = tuple(
            b'\x07\x08' for _ in range(test_constants.STREAM_LENGTH))
        request_iterator = iter(requests)

        self._record[:] = []
        channel = grpc.intercept_channel(
            self._channel, _LoggingInterceptor('c1', self._record),
            _LoggingInterceptor('c2', self._record))

        multi_callable = _stream_unary_multi_callable(channel)
        multi_callable(
            request_iterator,
            metadata=(('test',
                       'InterceptedStreamRequestBlockingUnaryResponse'),))

        self.assertSequenceEqual(self._record, [
            'c1:intercept_stream_unary', 'c2:intercept_stream_unary',
            's1:intercept_service', 's2:intercept_service'
        ])

    def testInterceptedStreamRequestBlockingUnaryResponseWithCall(self):
        requests = tuple(
            b'\x07\x08' for _ in range(test_constants.STREAM_LENGTH))
        request_iterator = iter(requests)

        self._record[:] = []
        channel = grpc.intercept_channel(
            self._channel, _LoggingInterceptor('c1', self._record),
            _LoggingInterceptor('c2', self._record))

        multi_callable = _stream_unary_multi_callable(channel)
        multi_callable.with_call(
            request_iterator,
            metadata=(
                ('test',
                 'InterceptedStreamRequestBlockingUnaryResponseWithCall'),))

        self.assertSequenceEqual(self._record, [
            'c1:intercept_stream_unary', 'c2:intercept_stream_unary',
            's1:intercept_service', 's2:intercept_service'
        ])

    def testInterceptedStreamRequestFutureUnaryResponse(self):
        requests = tuple(
            b'\x07\x08' for _ in range(test_constants.STREAM_LENGTH))
        request_iterator = iter(requests)

        self._record[:] = []
        channel = grpc.intercept_channel(
            self._channel, _LoggingInterceptor('c1', self._record),
            _LoggingInterceptor('c2', self._record))

        multi_callable = _stream_unary_multi_callable(channel)
        response_future = multi_callable.future(
            request_iterator,
            metadata=(('test', 'InterceptedStreamRequestFutureUnaryResponse'),))
        response_future.result()

        self.assertSequenceEqual(self._record, [
            'c1:intercept_stream_unary', 'c2:intercept_stream_unary',
            's1:intercept_service', 's2:intercept_service'
        ])

    def testInterceptedStreamRequestFutureUnaryResponseWithError(self):
        requests = tuple(
            _EXCEPTION_REQUEST for _ in range(test_constants.STREAM_LENGTH))
        request_iterator = iter(requests)

        self._record[:] = []
        channel = grpc.intercept_channel(
            self._channel, _LoggingInterceptor('c1', self._record),
            _LoggingInterceptor('c2', self._record))

        multi_callable = _stream_unary_multi_callable(channel)
        response_future = multi_callable.future(
            request_iterator,
            metadata=(('test', 'InterceptedStreamRequestFutureUnaryResponse'),))
        with self.assertRaises(grpc.RpcError) as exception_context:
            response_future.result()
        exception = exception_context.exception
        self.assertFalse(exception.cancelled())
        self.assertFalse(exception.running())
        self.assertTrue(exception.done())
        with self.assertRaises(grpc.RpcError):
            exception.result()
        self.assertIsInstance(exception.exception(), grpc.RpcError)

    def testInterceptedStreamRequestStreamResponse(self):
        requests = tuple(
            b'\x77\x58' for _ in range(test_constants.STREAM_LENGTH))
        request_iterator = iter(requests)

        self._record[:] = []
        channel = grpc.intercept_channel(
            self._channel, _LoggingInterceptor('c1', self._record),
            _LoggingInterceptor('c2', self._record))

        multi_callable = _stream_stream_multi_callable(channel)
        response_iterator = multi_callable(
            request_iterator,
            metadata=(('test', 'InterceptedStreamRequestStreamResponse'),))
        tuple(response_iterator)

        self.assertSequenceEqual(self._record, [
            'c1:intercept_stream_stream', 'c2:intercept_stream_stream',
            's1:intercept_service', 's2:intercept_service'
        ])

    def testInterceptedStreamRequestStreamResponseWithError(self):
        requests = tuple(
            _EXCEPTION_REQUEST for _ in range(test_constants.STREAM_LENGTH))
        request_iterator = iter(requests)

        self._record[:] = []
        channel = grpc.intercept_channel(
            self._channel, _LoggingInterceptor('c1', self._record),
            _LoggingInterceptor('c2', self._record))

        multi_callable = _stream_stream_multi_callable(channel)
        response_iterator = multi_callable(
            request_iterator,
            metadata=(('test', 'InterceptedStreamRequestStreamResponse'),))
        with self.assertRaises(grpc.RpcError) as exception_context:
            tuple(response_iterator)
        exception = exception_context.exception
        self.assertFalse(exception.cancelled())
        self.assertFalse(exception.running())
        self.assertTrue(exception.done())
        with self.assertRaises(grpc.RpcError):
            exception.result()
        self.assertIsInstance(exception.exception(), grpc.RpcError)


if __name__ == '__main__':
    logging.basicConfig()
    unittest.main(verbosity=2)

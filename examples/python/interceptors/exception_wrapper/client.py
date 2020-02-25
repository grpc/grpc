"""Intercepted route guide client"""

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

import grpc

import route_guide_pb2
import route_guide_pb2_grpc

class _UnaryStreamWrapper(grpc.Call, grpc.Future):
    def __init__(self, underlay_call, failure_handler):
        super().__init__()
        self._underlay_call = underlay_call
        self._failure_handler = failure_handler

    def initial_metadata(self):
        return self._underlay_call.initial_metadata()

    def trailing_metadata(self):
        return self._underlay_call.initial_metadata()

    def code(self):
        return self._underlay_call.code()

    def details(self):
        return self._underlay_call.details()

    def debug_error_string(self):
        return self._underlay_call.debug_error_string()

    def cancelled(self):
        return self._underlay_call.cancelled()

    def running(self):
        return self._underlay_call.running()

    def done(self):
        return self._underlay_call.done()

    def result(self, timeout=None):
        return self._underlay_call.result(timeout=timeout)

    def exception(self, timeout=None):
        return self._underlay_call.exception(timeout=timeout)

    def traceback(self, timeout=None):
        return self._underlay_call.traceback(timeout=timeout)

    def add_done_callback(self, fn):
        return self._underlay_call.add_done_callback(fn)

    def add_callback(self, callback):
        return self._underlay_call.add_callback(callback)

    def is_active(self):
        return self._underlay_call.is_active()

    def time_remaining(self):
        return self._underlay_call.time_remaining()

    def cancel(self):
        return self._underlay_call.cancel()

    def __iter__(self):
        return self

    def __next__(self):
        try:
            return next(self._underlay_call)
        except StopIteration:
            raise
        except Exception as e:
            self._failure_handler(e)


class ExceptionWrapper(Exception):
    def __init__(self, underlying_error):
        self._underlying_error = underlying_error

    def __repr__(self):
        return f"ExceptionWrapper()"

    __str__ = __repr__


class ExceptionInterceptor(grpc.UnaryStreamClientInterceptor):
    """An interceptor that wraps rpc exceptions."""

    # _RETRY_STATUS_CODES = (StatusCode.INTERNAL, StatusCode.RESOURCE_EXHAUSTED)

    def intercept_unary_stream(self, continuation, client_call_details,
                               request):
        def _handle_error(error_call):
            print(f"error_call code: {error_call.code()}")
            if error_call.code() == grpc.StatusCode.RESOURCE_EXHAUSTED:
                raise ExceptionWrapper(error_call) from error_call
            else:
                raise error_call
        call = continuation(client_call_details, request)
        return _UnaryStreamWrapper(call, _handle_error)


if __name__ == "__main__":
    with grpc.insecure_channel('localhost:50051') as channel:
        intercepted_channel = grpc.intercept_channel(channel, ExceptionInterceptor())
        stub = route_guide_pb2_grpc.RouteGuideStub(intercepted_channel)
        rectangle = route_guide_pb2.Rectangle()
        rectangle.lo.latitude = -90
        rectangle.lo.longitude = -90
        rectangle.hi.latitude = 90
        rectangle.hi.longitude = 90
        for feature in stub.ListFeatures(rectangle):
            print(feature)

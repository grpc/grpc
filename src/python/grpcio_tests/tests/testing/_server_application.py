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
"""An example gRPC Python-using server-side application."""

import threading

import grpc

# requests_pb2 is a semantic dependency of this module.
from tests.testing import _application_common
from tests.testing.proto import requests_pb2  # pylint: disable=unused-import
from tests.testing.proto import services_pb2
from tests.testing.proto import services_pb2_grpc


class FirstServiceServicer(services_pb2_grpc.FirstServiceServicer):
    """Services RPCs."""

    def __init__(self):
        self._abort_lock = threading.RLock()
        self._abort_response = _application_common.ABORT_NO_STATUS_RESPONSE

    def UnUn(self, request, context):
        if request == _application_common.UNARY_UNARY_REQUEST:
            return _application_common.UNARY_UNARY_RESPONSE
        elif request == _application_common.ABORT_REQUEST:
            with self._abort_lock:
                try:
                    context.abort(
                        grpc.StatusCode.PERMISSION_DENIED,
                        "Denying permission to test abort.",
                    )
                except Exception as e:  # pylint: disable=broad-except
                    self._abort_response = (
                        _application_common.ABORT_SUCCESS_RESPONSE
                    )
                else:
                    self._abort_status = (
                        _application_common.ABORT_FAILURE_RESPONSE
                    )
            return None  # NOTE: For the linter.
        elif request == _application_common.ABORT_SUCCESS_QUERY:
            with self._abort_lock:
                return self._abort_response
        else:
            context.set_code(grpc.StatusCode.INVALID_ARGUMENT)
            context.set_details("Something is wrong with your request!")
            return services_pb2.Down()

    def UnStre(self, request, context):
        if _application_common.UNARY_STREAM_REQUEST != request:
            context.set_code(grpc.StatusCode.INVALID_ARGUMENT)
            context.set_details("Something is wrong with your request!")
        return
        yield services_pb2.Strange()  # pylint: disable=unreachable

    def StreUn(self, request_iterator, context):
        context.send_initial_metadata(
            (
                (
                    "server_application_metadata_key",
                    "Hi there!",
                ),
            )
        )
        for request in request_iterator:
            if request != _application_common.STREAM_UNARY_REQUEST:
                context.set_code(grpc.StatusCode.INVALID_ARGUMENT)
                context.set_details("Something is wrong with your request!")
                return services_pb2.Strange()
            elif not context.is_active():
                return services_pb2.Strange()
        else:
            return _application_common.STREAM_UNARY_RESPONSE

    def StreStre(self, request_iterator, context):
        valid_requests = (
            _application_common.STREAM_STREAM_REQUEST,
            _application_common.STREAM_STREAM_MUTATING_REQUEST,
        )
        for request in request_iterator:
            if request not in valid_requests:
                context.set_code(grpc.StatusCode.INVALID_ARGUMENT)
                context.set_details("Something is wrong with your request!")
                return
            elif not context.is_active():
                return
            elif request == _application_common.STREAM_STREAM_REQUEST:
                yield _application_common.STREAM_STREAM_RESPONSE
                yield _application_common.STREAM_STREAM_RESPONSE
            elif request == _application_common.STREAM_STREAM_MUTATING_REQUEST:
                response = services_pb2.Bottom()
                for i in range(
                    _application_common.STREAM_STREAM_MUTATING_COUNT
                ):
                    response.first_bottom_field = i
                    yield response

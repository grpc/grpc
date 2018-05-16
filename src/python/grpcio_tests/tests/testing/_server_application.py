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

import grpc

# requests_pb2 is a semantic dependency of this module.
from tests.testing import _application_common
from tests.testing.proto import requests_pb2  # pylint: disable=unused-import
from tests.testing.proto import services_pb2
from tests.testing.proto import services_pb2_grpc


class FirstServiceServicer(services_pb2_grpc.FirstServiceServicer):
    """Services RPCs."""

    def UnUn(self, request, context):
        if _application_common.UNARY_UNARY_REQUEST == request:
            return _application_common.UNARY_UNARY_RESPONSE
        else:
            context.set_code(grpc.StatusCode.INVALID_ARGUMENT)
            context.set_details('Something is wrong with your request!')
            return services_pb2.Down()

    def UnStre(self, request, context):
        if _application_common.UNARY_STREAM_REQUEST != request:
            context.set_code(grpc.StatusCode.INVALID_ARGUMENT)
            context.set_details('Something is wrong with your request!')
        return
        yield services_pb2.Strange()  # pylint: disable=unreachable

    def StreUn(self, request_iterator, context):
        context.send_initial_metadata(((
            'server_application_metadata_key',
            'Hi there!',
        ),))
        for request in request_iterator:
            if request != _application_common.STREAM_UNARY_REQUEST:
                context.set_code(grpc.StatusCode.INVALID_ARGUMENT)
                context.set_details('Something is wrong with your request!')
                return services_pb2.Strange()
            elif not context.is_active():
                return services_pb2.Strange()
        else:
            return _application_common.STREAM_UNARY_RESPONSE

    def StreStre(self, request_iterator, context):
        for request in request_iterator:
            if request != _application_common.STREAM_STREAM_REQUEST:
                context.set_code(grpc.StatusCode.INVALID_ARGUMENT)
                context.set_details('Something is wrong with your request!')
                return
            elif not context.is_active():
                return
            else:
                yield _application_common.STREAM_STREAM_RESPONSE
                yield _application_common.STREAM_STREAM_RESPONSE

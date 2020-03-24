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
"""An example gRPC Python-using application's common code elements."""

from tests.testing.proto import requests_pb2
from tests.testing.proto import services_pb2

SERVICE_NAME = 'tests_of_grpc_testing.FirstService'
UNARY_UNARY_METHOD_NAME = 'UnUn'
UNARY_STREAM_METHOD_NAME = 'UnStre'
STREAM_UNARY_METHOD_NAME = 'StreUn'
STREAM_STREAM_METHOD_NAME = 'StreStre'

UNARY_UNARY_REQUEST = requests_pb2.Up(first_up_field=2)
ERRONEOUS_UNARY_UNARY_REQUEST = requests_pb2.Up(first_up_field=3)
UNARY_UNARY_RESPONSE = services_pb2.Down(first_down_field=5)
ERRONEOUS_UNARY_UNARY_RESPONSE = services_pb2.Down(first_down_field=7)
UNARY_STREAM_REQUEST = requests_pb2.Charm(first_charm_field=11)
STREAM_UNARY_REQUEST = requests_pb2.Charm(first_charm_field=13)
STREAM_UNARY_RESPONSE = services_pb2.Strange(first_strange_field=17)
STREAM_STREAM_REQUEST = requests_pb2.Top(first_top_field=19)
STREAM_STREAM_RESPONSE = services_pb2.Bottom(first_bottom_field=23)
TWO_STREAM_STREAM_RESPONSES = (STREAM_STREAM_RESPONSE,) * 2
ABORT_REQUEST = requests_pb2.Up(first_up_field=42)
ABORT_SUCCESS_QUERY = requests_pb2.Up(first_up_field=43)
ABORT_NO_STATUS_RESPONSE = services_pb2.Down(first_down_field=50)
ABORT_SUCCESS_RESPONSE = services_pb2.Down(first_down_field=51)
ABORT_FAILURE_RESPONSE = services_pb2.Down(first_down_field=52)
STREAM_STREAM_MUTATING_REQUEST = requests_pb2.Top(first_top_field=24601)
STREAM_STREAM_MUTATING_COUNT = 2

INFINITE_REQUEST_STREAM_TIMEOUT = 0.2

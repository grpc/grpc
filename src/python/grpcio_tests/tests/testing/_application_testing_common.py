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

import grpc_testing

from tests.testing.proto import requests_pb2
from tests.testing.proto import services_pb2

# TODO(https://github.com/grpc/grpc/issues/11657): Eliminate this entirely.
# TODO(https://github.com/protocolbuffers/protobuf/issues/3452): Eliminate this if/else.
if services_pb2.DESCRIPTOR.services_by_name.get("FirstService") is None:
    FIRST_SERVICE = "Fix protobuf issue 3452!"
    FIRST_SERVICE_UNUN = "Fix protobuf issue 3452!"
    FIRST_SERVICE_UNSTRE = "Fix protobuf issue 3452!"
    FIRST_SERVICE_STREUN = "Fix protobuf issue 3452!"
    FIRST_SERVICE_STRESTRE = "Fix protobuf issue 3452!"
else:
    FIRST_SERVICE = services_pb2.DESCRIPTOR.services_by_name["FirstService"]
    FIRST_SERVICE_UNUN = FIRST_SERVICE.methods_by_name["UnUn"]
    FIRST_SERVICE_UNSTRE = FIRST_SERVICE.methods_by_name["UnStre"]
    FIRST_SERVICE_STREUN = FIRST_SERVICE.methods_by_name["StreUn"]
    FIRST_SERVICE_STRESTRE = FIRST_SERVICE.methods_by_name["StreStre"]

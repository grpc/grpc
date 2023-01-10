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

from typing import Any, Mapping

from google.protobuf import descriptor  # pytype: disable=pyi-error
from grpc_testing._server import _Server
from grpc_testing._server import _server  # pytype: disable=pyi-error


def server_from_dictionary(descriptors_to_servicers: Mapping[
    descriptor.ServiceDescriptor, Any], time: float) -> _Server:
    return _server.server_from_descriptor_to_servicers(descriptors_to_servicers,
                                                       time)

# Copyright 2016 gRPC authors.
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
"""Reference implementation for reflection in gRPC Python."""

import sys
import grpc

from grpc_reflection.v1alpha import reflection_pb2 as _reflection_pb2
from grpc_reflection.v1alpha import reflection_pb2_grpc as _reflection_pb2_grpc

from grpc_reflection.v1alpha._base import BaseReflectionServicer

SERVICE_NAME = _reflection_pb2.DESCRIPTOR.services_by_name[
    'ServerReflection'].full_name


class ReflectionServicer(BaseReflectionServicer):
    """Servicer handling RPCs for service statuses."""

    def ServerReflectionInfo(self, request_iterator, context):
        # pylint: disable=unused-argument
        for request in request_iterator:
            if request.HasField('file_by_filename'):
                yield self._file_by_filename(request.file_by_filename)
            elif request.HasField('file_containing_symbol'):
                yield self._file_containing_symbol(
                    request.file_containing_symbol)
            elif request.HasField('file_containing_extension'):
                yield self._file_containing_extension(
                    request.file_containing_extension.containing_type,
                    request.file_containing_extension.extension_number)
            elif request.HasField('all_extension_numbers_of_type'):
                yield self._all_extension_numbers_of_type(
                    request.all_extension_numbers_of_type)
            elif request.HasField('list_services'):
                yield self._list_services()
            else:
                yield _reflection_pb2.ServerReflectionResponse(
                    error_response=_reflection_pb2.ErrorResponse(
                        error_code=grpc.StatusCode.INVALID_ARGUMENT.value[0],
                        error_message=grpc.StatusCode.INVALID_ARGUMENT.value[1].
                        encode(),
                    ))


_enable_server_reflection_doc = """Enables server reflection on a server.

Args:
    service_names: Iterable of fully-qualified service names available.
    server: grpc.Server to which reflection service will be added.
    pool: DescriptorPool object to use (descriptor_pool.Default() if None).
"""

if sys.version_info[0] >= 3 and sys.version_info[1] >= 6:
    # Exposes AsyncReflectionServicer as public API.
    from . import _async as aio
    from grpc.experimental import aio as grpc_aio  # pylint: disable=ungrouped-imports

    def enable_server_reflection(service_names, server, pool=None):
        if isinstance(server, grpc_aio.Server):
            _reflection_pb2_grpc.add_ServerReflectionServicer_to_server(
                aio.ReflectionServicer(service_names, pool=pool), server)
        else:
            _reflection_pb2_grpc.add_ServerReflectionServicer_to_server(
                ReflectionServicer(service_names, pool=pool), server)

    enable_server_reflection.__doc__ = _enable_server_reflection_doc

    __all__ = [
        "SERVICE_NAME",
        "ReflectionServicer",
        "enable_server_reflection",
        "aio",
    ]
else:

    def enable_server_reflection(service_names, server, pool=None):
        _reflection_pb2_grpc.add_ServerReflectionServicer_to_server(
            ReflectionServicer(service_names, pool=pool), server)

    enable_server_reflection.__doc__ = _enable_server_reflection_doc

    __all__ = [
        "SERVICE_NAME",
        "ReflectionServicer",
        "enable_server_reflection",
    ]

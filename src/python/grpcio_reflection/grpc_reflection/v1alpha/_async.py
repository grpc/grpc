# Copyright 2020 gRPC authors.
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
"""The AsyncIO version of the reflection servicer."""

from typing import AsyncIterable

import grpc

from grpc_reflection.v1alpha import reflection_pb2 as _reflection_pb2
from grpc_reflection.v1alpha._base import BaseReflectionServicer


class ReflectionServicer(BaseReflectionServicer):
    """Servicer handling RPCs for service statuses."""

    async def ServerReflectionInfo(
        self, request_iterator: AsyncIterable[
            _reflection_pb2.ServerReflectionRequest], unused_context
    ) -> AsyncIterable[_reflection_pb2.ServerReflectionResponse]:
        async for request in request_iterator:
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


__all__ = [
    "ReflectionServicer",
]

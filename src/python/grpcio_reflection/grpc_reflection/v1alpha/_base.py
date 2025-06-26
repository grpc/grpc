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
"""Base implementation of reflection servicer."""

from typing import Any, Dict, Iterable, Optional, Set, Tuple

from google.protobuf import descriptor_pb2
from google.protobuf import descriptor_pool
import grpc
from grpc_reflection.v1alpha import reflection_pb2 as _reflection_pb2
from grpc_reflection.v1alpha import reflection_pb2_grpc as _reflection_pb2_grpc

_POOL = descriptor_pool.Default()


def _not_found_error(original_request: _reflection_pb2.ServerReflectionRequest) -> _reflection_pb2.ServerReflectionResponse:
    return _reflection_pb2.ServerReflectionResponse(
        error_response=_reflection_pb2.ErrorResponse(
            error_code=grpc.StatusCode.NOT_FOUND.value[0],
            error_message=grpc.StatusCode.NOT_FOUND.value[1].encode(),
        ),
        original_request=original_request,
    )


def _collect_transitive_dependencies(
    descriptor: Any, 
    seen_files: Dict[str, Any]
) -> None:
    seen_files.update({descriptor.name: descriptor})
    for dependency in descriptor.dependencies:
        if not dependency.name in seen_files:
            # descriptors cannot have circular dependencies
            _collect_transitive_dependencies(dependency, seen_files)


def _file_descriptor_response(
    descriptor: Any, 
    original_request: _reflection_pb2.ServerReflectionRequest
) -> _reflection_pb2.ServerReflectionResponse:
    # collect all dependencies
    descriptors: Dict[str, Any] = {}
    _collect_transitive_dependencies(descriptor, descriptors)

    # serialize all descriptors
    serialized_proto_list: list[bytes] = []
    for d_key in descriptors:
        proto = descriptor_pb2.FileDescriptorProto()
        descriptors[d_key].CopyToProto(proto)
        serialized_proto_list.append(proto.SerializeToString())

    return _reflection_pb2.ServerReflectionResponse(
        file_descriptor_response=_reflection_pb2.FileDescriptorResponse(
            file_descriptor_proto=(serialized_proto_list)
        ),
        original_request=original_request,
    )


class BaseReflectionServicer(_reflection_pb2_grpc.ServerReflectionServicer):
    """Base class for reflection servicer."""

    def __init__(self, service_names: Iterable[str], pool: Optional[Any] = None) -> None:
        """Constructor.

        Args:
            service_names: Iterable of fully-qualified service names available.
            pool: An optional DescriptorPool instance.
        """
        self._service_names: Tuple[str, ...] = tuple(sorted(service_names))
        self._pool: Any = _POOL if pool is None else pool

    def _file_by_filename(
        self, 
        request: _reflection_pb2.ServerReflectionRequest, 
        filename: str
    ) -> _reflection_pb2.ServerReflectionResponse:
        try:
            descriptor = self._pool.FindFileByName(filename)
        except KeyError:
            return _not_found_error(request)
        else:
            return _file_descriptor_response(descriptor, request)

    def _file_containing_symbol(
        self, 
        request: _reflection_pb2.ServerReflectionRequest, 
        fully_qualified_name: str
    ) -> _reflection_pb2.ServerReflectionResponse:
        try:
            descriptor = self._pool.FindFileContainingSymbol(
                fully_qualified_name
            )
        except KeyError:
            return _not_found_error(request)
        else:
            return _file_descriptor_response(descriptor, request)

    def _file_containing_extension(
        self, 
        request: _reflection_pb2.ServerReflectionRequest, 
        containing_type: str, 
        extension_number: int
    ) -> _reflection_pb2.ServerReflectionResponse:
        try:
            message_descriptor = self._pool.FindMessageTypeByName(
                containing_type
            )
            extension_descriptor = self._pool.FindExtensionByNumber(
                message_descriptor, extension_number
            )
            descriptor = self._pool.FindFileContainingSymbol(
                extension_descriptor.full_name
            )
        except KeyError:
            return _not_found_error(request)
        else:
            return _file_descriptor_response(descriptor, request)

    def _all_extension_numbers_of_type(
        self, 
        request: _reflection_pb2.ServerReflectionRequest, 
        containing_type: str
    ) -> _reflection_pb2.ServerReflectionResponse:
        try:
            message_descriptor = self._pool.FindMessageTypeByName(
                containing_type
            )
            extension_numbers: Tuple[int, ...] = tuple(
                sorted(
                    extension.number
                    for extension in self._pool.FindAllExtensions(
                        message_descriptor
                    )
                )
            )
        except KeyError:
            return _not_found_error(request)
        else:
            return _reflection_pb2.ServerReflectionResponse(
                all_extension_numbers_response=_reflection_pb2.ExtensionNumberResponse(
                    base_type_name=message_descriptor.full_name,
                    extension_number=extension_numbers,
                ),
                original_request=request,
            )

    def _list_services(
        self, 
        request: _reflection_pb2.ServerReflectionRequest
    ) -> _reflection_pb2.ServerReflectionResponse:
        return _reflection_pb2.ServerReflectionResponse(
            list_services_response=_reflection_pb2.ListServiceResponse(
                service=[
                    _reflection_pb2.ServiceResponse(name=service_name)
                    for service_name in self._service_names
                ]
            ),
            original_request=request,
        )


__all__ = ["BaseReflectionServicer"]

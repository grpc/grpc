# Copyright 2022 gRPC authors.
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
"""Reference implementation for reflection client in gRPC Python.

For usage instructions, see the Python Reflection documentation at
``doc/python/server_reflection.md``.
"""

import logging
from typing import Any, Dict, Iterable, List, Set

from google.protobuf.descriptor_database import DescriptorDatabase
from google.protobuf.descriptor_pb2 import FileDescriptorProto
import grpc
from grpc_reflection.v1alpha.reflection_pb2 import ExtensionNumberResponse
from grpc_reflection.v1alpha.reflection_pb2 import ExtensionRequest
from grpc_reflection.v1alpha.reflection_pb2 import FileDescriptorResponse
from grpc_reflection.v1alpha.reflection_pb2 import ListServiceResponse
from grpc_reflection.v1alpha.reflection_pb2 import ServerReflectionRequest
from grpc_reflection.v1alpha.reflection_pb2 import ServerReflectionResponse
from grpc_reflection.v1alpha.reflection_pb2 import ServiceResponse
from grpc_reflection.v1alpha.reflection_pb2_grpc import ServerReflectionStub


class ProtoReflectionDescriptorDatabase(DescriptorDatabase):
    """
    A container and interface for receiving descriptors from a server's
    Reflection service.

    ProtoReflectionDescriptorDatabase takes a channel to a server with
    Reflection service, and provides an interface to retrieve the Reflection
    information. It implements the DescriptorDatabase interface.

    It is typically used to feed a DescriptorPool instance.
    """

    # Implementation based on C++ version found here (version tag 1.39.1):
    #   grpc/test/cpp/util/proto_reflection_descriptor_database.cc
    # while implementing the Python interface given here:
    #   https://googleapis.dev/python/protobuf/3.17.0/google/protobuf/descriptor_database.html

    def __init__(self, channel: grpc.Channel):
        DescriptorDatabase.__init__(self)
        self._logger = logging.getLogger(__name__)
        self._stub = ServerReflectionStub(channel)
        self._known_files: Set[str] = set()
        self._cached_extension_numbers: Dict[str, List[int]] = dict()

    def get_services(self) -> Iterable[str]:
        """
        Get list of full names of the registered services.

        Returns:
            A list of strings corresponding to the names of the services.
        """

        request = ServerReflectionRequest(list_services="")
        response = self._do_one_request(request, key="")
        list_services: ListServiceResponse = response.list_services_response
        services: List[ServiceResponse] = list_services.service
        return [service.name for service in services]

    def FindFileByName(self, name: str) -> FileDescriptorProto:
        """
        Find a file descriptor by file name.

        This function implements a DescriptorDatabase interface, and is
        typically not called directly; prefer using a DescriptorPool instead.

        Args:
            name: The name of the file. Typically this is a relative path ending in ".proto".

        Returns:
            A FileDescriptorProto for the file.

        Raises:
            KeyError: the file was not found.
        """

        try:
            return super().FindFileByName(name)
        except KeyError:
            pass
        assert name not in self._known_files
        request = ServerReflectionRequest(file_by_filename=name)
        response = self._do_one_request(request, key=name)
        self._add_file_from_response(response.file_descriptor_response)
        return super().FindFileByName(name)

    def FindFileContainingSymbol(self, symbol: str) -> FileDescriptorProto:
        """
        Find the file containing the symbol, and return its file descriptor.

        The symbol should be a fully qualified name including the file
        descriptor's package and any containing messages. Some examples:

            * "some.package.name.Message"
            * "some.package.name.Message.NestedEnum"
            * "some.package.name.Message.some_field"

        This function implements a DescriptorDatabase interface, and is
        typically not called directly; prefer using a DescriptorPool instead.

        Args:
            symbol: The fully-qualified name of the symbol.

        Returns:
            FileDescriptorProto for the file containing the symbol.

        Raises:
            KeyError: the symbol was not found.
        """

        try:
            return super().FindFileContainingSymbol(symbol)
        except KeyError:
            pass
        # Query the server
        request = ServerReflectionRequest(file_containing_symbol=symbol)
        response = self._do_one_request(request, key=symbol)
        self._add_file_from_response(response.file_descriptor_response)
        return super().FindFileContainingSymbol(symbol)

    def FindAllExtensionNumbers(self, extendee_name: str) -> Iterable[int]:
        """
        Find the field numbers used by all known extensions of `extendee_name`.

        This function implements a DescriptorDatabase interface, and is
        typically not called directly; prefer using a DescriptorPool instead.

        Args:
            extendee_name: fully-qualified name of the extended message type.

        Returns:
            A list of field numbers used by all known extensions.

        Raises:
            KeyError: The message type `extendee_name` was not found.
        """

        if extendee_name in self._cached_extension_numbers:
            return self._cached_extension_numbers[extendee_name]
        request = ServerReflectionRequest(
            all_extension_numbers_of_type=extendee_name
        )
        response = self._do_one_request(request, key=extendee_name)
        all_extension_numbers: ExtensionNumberResponse = (
            response.all_extension_numbers_response
        )
        numbers = list(all_extension_numbers.extension_number)
        self._cached_extension_numbers[extendee_name] = numbers
        return numbers

    def FindFileContainingExtension(
        self, extendee_name: str, extension_number: int
    ) -> FileDescriptorProto:
        """
        Find the file which defines an extension for the given message type
        and field number.

        This function implements a DescriptorDatabase interface, and is
        typically not called directly; prefer using a DescriptorPool instead.

        Args:
            extendee_name: fully-qualified name of the extended message type.
            extension_number: the number of the extension field.

        Returns:
            FileDescriptorProto for the file containing the extension.

        Raises:
            KeyError: The message or the extension number were not found.
        """

        try:
            return super().FindFileContainingExtension(
                extendee_name, extension_number
            )
        except KeyError:
            pass
        request = ServerReflectionRequest(
            file_containing_extension=ExtensionRequest(
                containing_type=extendee_name, extension_number=extension_number
            )
        )
        response = self._do_one_request(
            request, key=(extendee_name, extension_number)
        )
        file_desc = response.file_descriptor_response
        self._add_file_from_response(file_desc)
        return super().FindFileContainingExtension(
            extendee_name, extension_number
        )

    def _do_one_request(
        self, request: ServerReflectionRequest, key: Any
    ) -> ServerReflectionResponse:
        response = self._stub.ServerReflectionInfo(iter([request]))
        res = next(response)
        if res.WhichOneof("message_response") == "error_response":
            # Only NOT_FOUND errors are expected at this layer
            error_code = res.error_response.error_code
            assert (
                error_code == grpc.StatusCode.NOT_FOUND.value[0]
            ), "unexpected error response: " + repr(res.error_response)
            raise KeyError(key)
        return res

    def _add_file_from_response(
        self, file_descriptor: FileDescriptorResponse
    ) -> None:
        protos: List[bytes] = file_descriptor.file_descriptor_proto
        for proto in protos:
            desc = FileDescriptorProto()
            desc.ParseFromString(proto)
            if desc.name not in self._known_files:
                self._logger.info(
                    "Loading descriptors from file: %s", desc.name
                )
                self._known_files.add(desc.name)
                self.Add(desc)

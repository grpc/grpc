# Copyright 2021 gRPC authors.
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
"""Reference implementation for reflection client in gRPC Python."""

from google.protobuf.descriptor_database import DescriptorDatabase
from google.protobuf.descriptor_pb2 import FileDescriptorProto
import grpc
from grpc_reflection.v1alpha.reflection_pb2 import (
    ErrorResponse,
    ExtensionNumberResponse,
    ExtensionRequest,
    FileDescriptorResponse,
    ListServiceResponse,
    ServerReflectionRequest,
    ServerReflectionResponse,
    ServiceResponse,
)
from grpc_reflection.v1alpha.reflection_pb2_grpc import ServerReflectionStub
import logging


class ProtoReflectionDescriptorDatabase(DescriptorDatabase):
    """
    ProtoReflectionDescriptorDatabase takes a stub of ServerReflection and provides the methods defined by
    DescriptorDatabase interfaces. It can be used to feed a DescriptorPool instance.

    Python implementation based on C++ version found here:
      https://github.com/grpc/grpc/blob/v1.39.1/test/cpp/util/proto_reflection_descriptor_database.cc
    while implementing the interface given here:
      https://googleapis.dev/python/protobuf/3.17.0/google/protobuf/descriptor_database.html

    """

    def __init__(self, channel: grpc.Channel):
        DescriptorDatabase.__init__(self)
        self._logger = logging.getLogger(__name__)
        self._stub = ServerReflectionStub(channel)
        self._known_files: Set[str] = set()
        self._cached_extension_numbers: Dict[str, List[int]] = dict()
        self._cached_extension_files: Dict[str, FileDescriptorProto] = dict()

    def _DoOneRequest(
        self, request: ServerReflectionRequest
    ) -> ServerReflectionResponse:
        response = self._stub.ServerReflectionInfo(iter([request]))
        res = next(response)
        if res.WhichOneof("message_response") == "error_response":
            raise grpc.RpcError(res.error_response)
        return res

    def _AddFileFromResponse(self, file_descriptor: FileDescriptorResponse):
        protos: List[bytes] = file_descriptor.file_descriptor_proto
        for proto in protos:
            desc = FileDescriptorProto()
            desc.ParseFromString(proto)
            if desc.name not in self._known_files:
                self._logger.info("Got new file {}".format(desc.name))
                self._known_files.add(desc.name)
                self.Add(desc)

    def GetServices(self):
        request = ServerReflectionRequest(list_services="")
        response = self._DoOneRequest(request)
        list_services: ListServiceResponse = response.list_services_response
        services: List[ServiceResponse] = list_services.service
        return [service.name for service in services]

    def FindAllExtensionNumbers(self, extendee_name):
        if extendee_name in self._cached_extension_numbers:
            return self._cached_extension_numbers[extendee_name]
        request = ServerReflectionRequest(all_extension_numbers_of_type=extendee_name)
        try:
            response = self._DoOneRequest(request)
        except grpc.RpcError as e:
            error_response: ErrorResponse = e.args[0]
            if error_response.error_code == grpc.StatusCode.NOT_FOUND:
                # Seems that this is "OK"
                self._logger.info(
                    "NOT_FOUND from server for FindAllExtensionNumbers({})".format(
                        extendee_name
                    )
                )
            return []
        all_extension_numbers: ExtensionNumberResponse = (
            response.all_extension_numbers_response
        )
        numbers = list(all_extension_numbers.extension_number)
        self._cached_extension_numbers[extendee_name] = numbers
        return numbers

    def FindFileByName(self, name):
        try:
            return super().FindFileByName(name)
        except KeyError:
            pass
        assert name not in self._known_files
        request = ServerReflectionRequest(file_by_filename=name)
        response = self._DoOneRequest(request)
        self._AddFileFromResponse(response.file_descriptor_response)
        return super().FindFileByName(name)

    def FindFileContainingExtension(self, extendee_name, extension_number):
        try:
            return super().FindFileContainingExtension(extendee_name, extension_number)
        except KeyError:
            pass
        request = ServerReflectionRequest(
            file_containing_extension=ExtensionRequest(
                containing_type=extendee_name, extension_number=extension_number
            )
        )
        response = self._DoOneRequest(request)
        file_desc = response.file_descriptor_response
        self._AddFileFromResponse(file_desc)
        return super().FindFileContainingExtension(extendee_name, extension_number)

    def FindFileContainingSymbol(self, symbol):
        try:
            return super().FindFileContainingSymbol(symbol)
        except KeyError:
            pass
        # Query the server
        request = ServerReflectionRequest(file_containing_symbol=symbol)
        response = self._DoOneRequest(request)
        self._AddFileFromResponse(response.file_descriptor_response)
        return super().FindFileContainingSymbol(symbol)




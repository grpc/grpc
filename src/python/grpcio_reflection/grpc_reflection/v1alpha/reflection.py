# Copyright 2016, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
"""Reference implementation for reflection in gRPC Python."""

import threading

import grpc
from google.protobuf import descriptor_pb2
from google.protobuf import descriptor_pool

from grpc_reflection.v1alpha import reflection_pb2

_POOL = descriptor_pool.Default()


def _not_found_error():
    return reflection_pb2.ServerReflectionResponse(
        error_response=reflection_pb2.ErrorResponse(
            error_code=grpc.StatusCode.NOT_FOUND.value[0],
            error_message=grpc.StatusCode.NOT_FOUND.value[1].encode(),))


def _file_descriptor_response(descriptor):
    proto = descriptor_pb2.FileDescriptorProto()
    descriptor.CopyToProto(proto)
    serialized_proto = proto.SerializeToString()
    return reflection_pb2.ServerReflectionResponse(
        file_descriptor_response=reflection_pb2.FileDescriptorResponse(
            file_descriptor_proto=(serialized_proto,)),)


class ReflectionServicer(reflection_pb2.ServerReflectionServicer):
    """Servicer handling RPCs for service statuses."""

    def __init__(self, service_names, pool=None):
        """Constructor.

    Args:
      service_names: Iterable of fully-qualified service names available.
    """
        self._service_names = list(service_names)
        self._pool = _POOL if pool is None else pool

    def _file_by_filename(self, filename):
        try:
            descriptor = self._pool.FindFileByName(filename)
        except KeyError:
            return _not_found_error()
        else:
            return _file_descriptor_response(descriptor)

    def _file_containing_symbol(self, fully_qualified_name):
        try:
            descriptor = self._pool.FindFileContainingSymbol(
                fully_qualified_name)
        except KeyError:
            return _not_found_error()
        else:
            return _file_descriptor_response(descriptor)

    def _file_containing_extension(containing_type, extension_number):
        # TODO(atash) Python protobuf currently doesn't support querying extensions.
        # https://github.com/google/protobuf/issues/2248
        return reflection_pb2.ServerReflectionResponse(
            error_response=reflection_pb2.ErrorResponse(
                error_code=grpc.StatusCode.UNIMPLEMENTED.value[0],
                error_message=grpc.StatusCode.UNIMPLMENTED.value[1].encode(),))

    def _extension_numbers_of_type(fully_qualified_name):
        # TODO(atash) We're allowed to leave this unsupported according to the
        # protocol, but we should still eventually implement it. Hits the same issue
        # as `_file_containing_extension`, however.
        # https://github.com/google/protobuf/issues/2248
        return reflection_pb2.ServerReflectionResponse(
            error_response=reflection_pb2.ErrorResponse(
                error_code=grpc.StatusCode.UNIMPLEMENTED.value[0],
                error_message=grpc.StatusCode.UNIMPLMENTED.value[1].encode(),))

    def _list_services(self):
        return reflection_pb2.ServerReflectionResponse(
            list_services_response=reflection_pb2.ListServiceResponse(service=[
                reflection_pb2.ServiceResponse(name=service_name)
                for service_name in self._service_names
            ]))

    def ServerReflectionInfo(self, request_iterator, context):
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
                yield _all_extension_numbers_of_type(
                    request.all_extension_numbers_of_type)
            elif request.HasField('list_services'):
                yield self._list_services()
            else:
                yield reflection_pb2.ServerReflectionResponse(
                    error_response=reflection_pb2.ErrorResponse(
                        error_code=grpc.StatusCode.INVALID_ARGUMENT.value[0],
                        error_message=grpc.StatusCode.INVALID_ARGUMENT.value[1]
                        .encode(),))

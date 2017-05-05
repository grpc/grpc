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
"""Tests of grpc_reflection.v1alpha.reflection."""

import unittest

import grpc
from grpc.framework.foundation import logging_pool
from grpc_reflection.v1alpha import reflection
from grpc_reflection.v1alpha import reflection_pb2
from grpc_reflection.v1alpha import reflection_pb2_grpc

from google.protobuf import descriptor_pool
from google.protobuf import descriptor_pb2

from src.proto.grpc.testing import empty_pb2
#empty2_pb2 is imported for import-consequent side-effects.
from src.proto.grpc.testing.proto2 import empty2_pb2  # pylint: disable=unused-import
from src.proto.grpc.testing.proto2 import empty2_extensions_pb2

from tests.unit.framework.common import test_constants

_EMPTY_PROTO_FILE_NAME = 'src/proto/grpc/testing/empty.proto'
_EMPTY_PROTO_SYMBOL_NAME = 'grpc.testing.Empty'
_SERVICE_NAMES = ('Angstrom', 'Bohr', 'Curie', 'Dyson', 'Einstein', 'Feynman',
                  'Galilei')
_EMPTY_EXTENSIONS_SYMBOL_NAME = 'grpc.testing.proto2.EmptyWithExtensions'
_EMPTY_EXTENSIONS_NUMBERS = (124, 125, 126, 127, 128,)


def _file_descriptor_to_proto(descriptor):
    proto = descriptor_pb2.FileDescriptorProto()
    descriptor.CopyToProto(proto)
    return proto.SerializeToString()


class ReflectionServicerTest(unittest.TestCase):

    def setUp(self):
        servicer = reflection.ReflectionServicer(service_names=_SERVICE_NAMES)
        server_pool = logging_pool.pool(test_constants.THREAD_CONCURRENCY)
        self._server = grpc.server(server_pool)
        port = self._server.add_insecure_port('[::]:0')
        reflection_pb2_grpc.add_ServerReflectionServicer_to_server(servicer,
                                                                   self._server)
        self._server.start()

        channel = grpc.insecure_channel('localhost:%d' % port)
        self._stub = reflection_pb2_grpc.ServerReflectionStub(channel)

    def testFileByName(self):
        requests = (reflection_pb2.ServerReflectionRequest(
            file_by_filename=_EMPTY_PROTO_FILE_NAME),
                    reflection_pb2.ServerReflectionRequest(
                        file_by_filename='i-donut-exist'),)
        responses = tuple(self._stub.ServerReflectionInfo(iter(requests)))
        expected_responses = (
            reflection_pb2.ServerReflectionResponse(
                valid_host='',
                file_descriptor_response=reflection_pb2.FileDescriptorResponse(
                    file_descriptor_proto=(
                        _file_descriptor_to_proto(empty_pb2.DESCRIPTOR),))),
            reflection_pb2.ServerReflectionResponse(
                valid_host='',
                error_response=reflection_pb2.ErrorResponse(
                    error_code=grpc.StatusCode.NOT_FOUND.value[0],
                    error_message=grpc.StatusCode.NOT_FOUND.value[1].encode(),
                )),)
        self.assertSequenceEqual(expected_responses, responses)

    def testFileBySymbol(self):
        requests = (reflection_pb2.ServerReflectionRequest(
            file_containing_symbol=_EMPTY_PROTO_SYMBOL_NAME
        ), reflection_pb2.ServerReflectionRequest(
            file_containing_symbol='i.donut.exist.co.uk.org.net.me.name.foo'),)
        responses = tuple(self._stub.ServerReflectionInfo(iter(requests)))
        expected_responses = (
            reflection_pb2.ServerReflectionResponse(
                valid_host='',
                file_descriptor_response=reflection_pb2.FileDescriptorResponse(
                    file_descriptor_proto=(
                        _file_descriptor_to_proto(empty_pb2.DESCRIPTOR),))),
            reflection_pb2.ServerReflectionResponse(
                valid_host='',
                error_response=reflection_pb2.ErrorResponse(
                    error_code=grpc.StatusCode.NOT_FOUND.value[0],
                    error_message=grpc.StatusCode.NOT_FOUND.value[1].encode(),
                )),)
        self.assertSequenceEqual(expected_responses, responses)

    @unittest.skip(
        'TODO(mmx): enable when (pure) python protobuf issue is fixed'
        '(see https://github.com/google/protobuf/issues/2882)')
    def testFileContainingExtension(self):
        requests = (reflection_pb2.ServerReflectionRequest(
            file_containing_extension=reflection_pb2.ExtensionRequest(
                containing_type=_EMPTY_EXTENSIONS_SYMBOL_NAME,
                extension_number=125,),
        ), reflection_pb2.ServerReflectionRequest(
            file_containing_extension=reflection_pb2.ExtensionRequest(
                containing_type='i.donut.exist.co.uk.org.net.me.name.foo',
                extension_number=55,),),)
        responses = tuple(self._stub.ServerReflectionInfo(iter(requests)))
        expected_responses = (
            reflection_pb2.ServerReflectionResponse(
                valid_host='',
                file_descriptor_response=reflection_pb2.FileDescriptorResponse(
                    file_descriptor_proto=(_file_descriptor_to_proto(
                        empty2_extensions_pb2.DESCRIPTOR),))),
            reflection_pb2.ServerReflectionResponse(
                valid_host='',
                error_response=reflection_pb2.ErrorResponse(
                    error_code=grpc.StatusCode.NOT_FOUND.value[0],
                    error_message=grpc.StatusCode.NOT_FOUND.value[1].encode(),
                )),)
        self.assertSequenceEqual(expected_responses, responses)

    def testExtensionNumbersOfType(self):
        requests = (reflection_pb2.ServerReflectionRequest(
            all_extension_numbers_of_type=_EMPTY_EXTENSIONS_SYMBOL_NAME
        ), reflection_pb2.ServerReflectionRequest(
            all_extension_numbers_of_type='i.donut.exist.co.uk.net.name.foo'),)
        responses = tuple(self._stub.ServerReflectionInfo(iter(requests)))
        expected_responses = (
            reflection_pb2.ServerReflectionResponse(
                valid_host='',
                all_extension_numbers_response=reflection_pb2.
                ExtensionNumberResponse(
                    base_type_name=_EMPTY_EXTENSIONS_SYMBOL_NAME,
                    extension_number=_EMPTY_EXTENSIONS_NUMBERS)),
            reflection_pb2.ServerReflectionResponse(
                valid_host='',
                error_response=reflection_pb2.ErrorResponse(
                    error_code=grpc.StatusCode.NOT_FOUND.value[0],
                    error_message=grpc.StatusCode.NOT_FOUND.value[1].encode(),
                )),)
        self.assertSequenceEqual(expected_responses, responses)

    def testListServices(self):
        requests = (reflection_pb2.ServerReflectionRequest(
            list_services='',),)
        responses = tuple(self._stub.ServerReflectionInfo(iter(requests)))
        expected_responses = (reflection_pb2.ServerReflectionResponse(
            valid_host='',
            list_services_response=reflection_pb2.ListServiceResponse(
                service=tuple(
                    reflection_pb2.ServiceResponse(name=name)
                    for name in _SERVICE_NAMES))),)
        self.assertSequenceEqual(expected_responses, responses)


if __name__ == '__main__':
    unittest.main(verbosity=2)

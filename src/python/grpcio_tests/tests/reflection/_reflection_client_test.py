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
"""Tests of grpc_reflection.v1alpha.reflection."""

import unittest

from google.protobuf.descriptor_pool import DescriptorPool
import grpc
from grpc_reflection.v1alpha import reflection
from grpc_reflection.v1alpha.proto_reflection_descriptor_database import (
    ProtoReflectionDescriptorDatabase,
)

from src.proto.grpc.testing import test_pb2

# Needed to load the EmptyWithExtensions message
from src.proto.grpc.testing.proto2 import empty2_extensions_pb2
from tests.unit import test_common

_PROTO_PACKAGE_NAME = "grpc.testing"
_PROTO_FILE_NAME = "src/proto/grpc/testing/test.proto"
_EMPTY_PROTO_FILE_NAME = "src/proto/grpc/testing/empty.proto"
_INVALID_FILE_NAME = "i-do-not-exist.proto"
_EMPTY_PROTO_SYMBOL_NAME = "grpc.testing.Empty"
_INVALID_SYMBOL_NAME = "IDoNotExist"
_EMPTY_EXTENSIONS_SYMBOL_NAME = "grpc.testing.proto2.EmptyWithExtensions"


class ReflectionClientTest(unittest.TestCase):
    def setUp(self):
        self._server = test_common.test_server()
        self._SERVICE_NAMES = (
            test_pb2.DESCRIPTOR.services_by_name["TestService"].full_name,
            reflection.SERVICE_NAME,
        )
        reflection.enable_server_reflection(self._SERVICE_NAMES, self._server)
        port = self._server.add_insecure_port("[::]:0")
        self._server.start()

        self._channel = grpc.insecure_channel("localhost:%d" % port)

        self._reflection_db = ProtoReflectionDescriptorDatabase(self._channel)
        self.desc_pool = DescriptorPool(self._reflection_db)

    def tearDown(self):
        self._server.stop(None)
        self._channel.close()

    def testListServices(self):
        services = self._reflection_db.get_services()
        self.assertCountEqual(self._SERVICE_NAMES, services)

    def testReflectionServiceName(self):
        self.assertEqual(
            reflection.SERVICE_NAME, "grpc.reflection.v1alpha.ServerReflection"
        )

    def testFindFile(self):
        file_name = _PROTO_FILE_NAME
        file_desc = self.desc_pool.FindFileByName(file_name)
        self.assertEqual(file_name, file_desc.name)
        self.assertEqual(_PROTO_PACKAGE_NAME, file_desc.package)
        self.assertIn("TestService", file_desc.services_by_name)

        file_name = _EMPTY_PROTO_FILE_NAME
        file_desc = self.desc_pool.FindFileByName(file_name)
        self.assertEqual(file_name, file_desc.name)
        self.assertEqual(_PROTO_PACKAGE_NAME, file_desc.package)
        self.assertIn("Empty", file_desc.message_types_by_name)

    def testFindFileError(self):
        with self.assertRaises(KeyError):
            self.desc_pool.FindFileByName(_INVALID_FILE_NAME)

    def testFindMessage(self):
        message_name = _EMPTY_PROTO_SYMBOL_NAME
        message_desc = self.desc_pool.FindMessageTypeByName(message_name)
        self.assertEqual(message_name, message_desc.full_name)
        self.assertTrue(message_name.endswith(message_desc.name))

    def testFindMessageError(self):
        with self.assertRaises(KeyError):
            self.desc_pool.FindMessageTypeByName(_INVALID_SYMBOL_NAME)

    def testFindServiceFindMethod(self):
        service_name = self._SERVICE_NAMES[0]
        service_desc = self.desc_pool.FindServiceByName(service_name)
        self.assertEqual(service_name, service_desc.full_name)
        self.assertTrue(service_name.endswith(service_desc.name))
        file_name = _PROTO_FILE_NAME
        file_desc = self.desc_pool.FindFileByName(file_name)
        self.assertIs(file_desc, service_desc.file)

        method_name = "EmptyCall"
        self.assertIn(method_name, service_desc.methods_by_name)

        method_desc = service_desc.FindMethodByName(method_name)
        self.assertIs(method_desc, service_desc.methods_by_name[method_name])
        self.assertIs(service_desc, method_desc.containing_service)
        self.assertEqual(method_name, method_desc.name)
        self.assertTrue(method_desc.full_name.endswith(method_name))

        empty_message_desc = self.desc_pool.FindMessageTypeByName(
            _EMPTY_PROTO_SYMBOL_NAME
        )
        self.assertEqual(empty_message_desc, method_desc.input_type)
        self.assertEqual(empty_message_desc, method_desc.output_type)

    def testFindServiceError(self):
        with self.assertRaises(KeyError):
            self.desc_pool.FindServiceByName(_INVALID_SYMBOL_NAME)

    def testFindMethodError(self):
        service_name = self._SERVICE_NAMES[0]
        service_desc = self.desc_pool.FindServiceByName(service_name)

        # FindMethodByName sometimes raises a KeyError, and sometimes returns None.
        # See https://github.com/protocolbuffers/protobuf/issues/9592
        with self.assertRaises(KeyError):
            res = service_desc.FindMethodByName(_INVALID_SYMBOL_NAME)
            if res is None:
                raise KeyError()

    def testFindExtensionNotImplemented(self):
        """
        Extensions aren't implemented in Protobuf for Python.
        For now, simply assert that indeed they don't work.
        """
        message_name = _EMPTY_EXTENSIONS_SYMBOL_NAME
        message_desc = self.desc_pool.FindMessageTypeByName(message_name)
        self.assertEqual(message_name, message_desc.full_name)
        self.assertTrue(message_name.endswith(message_desc.name))
        extension_field_descs = self.desc_pool.FindAllExtensions(message_desc)

        self.assertEqual(0, len(extension_field_descs))
        with self.assertRaises(KeyError):
            self.desc_pool.FindExtensionByName(message_name)


if __name__ == "__main__":
    unittest.main(verbosity=2)

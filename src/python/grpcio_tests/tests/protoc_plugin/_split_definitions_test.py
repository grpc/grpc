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

import collections
from concurrent import futures
import contextlib
import distutils.spawn
import errno
import importlib
import os
import os.path
import pkgutil
import shutil
import subprocess
import sys
import tempfile
import threading
import unittest
import platform

import grpc
from grpc_tools import protoc
from tests.unit.framework.common import test_constants

_MESSAGES_IMPORT = b'import "messages.proto";'
_SPLIT_NAMESPACE = b'package grpc_protoc_plugin.invocation_testing.split;'
_COMMON_NAMESPACE = b'package grpc_protoc_plugin.invocation_testing;'


@contextlib.contextmanager
def _system_path(path):
    old_system_path = sys.path[:]
    sys.path = sys.path[0:1] + path + sys.path[1:]
    yield
    sys.path = old_system_path


class DummySplitServicer(object):

    def __init__(self, request_class, response_class):
        self.request_class = request_class
        self.response_class = response_class

    def Call(self, request, context):
        return self.response_class()


class SeparateTestMixin(object):

    def testImportAttributes(self):
        with _system_path([self.python_out_directory]):
            pb2 = importlib.import_module(self.pb2_import)
        pb2.Request
        pb2.Response
        if self.should_find_services_in_pb2:
            pb2.TestServiceServicer
        else:
            with self.assertRaises(AttributeError):
                pb2.TestServiceServicer

        with _system_path([self.grpc_python_out_directory]):
            pb2_grpc = importlib.import_module(self.pb2_grpc_import)
        pb2_grpc.TestServiceServicer
        with self.assertRaises(AttributeError):
            pb2_grpc.Request
        with self.assertRaises(AttributeError):
            pb2_grpc.Response

    def testCall(self):
        with _system_path([self.python_out_directory]):
            pb2 = importlib.import_module(self.pb2_import)
        with _system_path([self.grpc_python_out_directory]):
            pb2_grpc = importlib.import_module(self.pb2_grpc_import)
        server = grpc.server(
            futures.ThreadPoolExecutor(max_workers=test_constants.POOL_SIZE))
        pb2_grpc.add_TestServiceServicer_to_server(
            DummySplitServicer(pb2.Request, pb2.Response), server)
        port = server.add_insecure_port('[::]:0')
        server.start()
        channel = grpc.insecure_channel('localhost:{}'.format(port))
        stub = pb2_grpc.TestServiceStub(channel)
        request = pb2.Request()
        expected_response = pb2.Response()
        response = stub.Call(request)
        self.assertEqual(expected_response, response)


class CommonTestMixin(object):

    def testImportAttributes(self):
        with _system_path([self.python_out_directory]):
            pb2 = importlib.import_module(self.pb2_import)
        pb2.Request
        pb2.Response
        if self.should_find_services_in_pb2:
            pb2.TestServiceServicer
        else:
            with self.assertRaises(AttributeError):
                pb2.TestServiceServicer

        with _system_path([self.grpc_python_out_directory]):
            pb2_grpc = importlib.import_module(self.pb2_grpc_import)
        pb2_grpc.TestServiceServicer
        with self.assertRaises(AttributeError):
            pb2_grpc.Request
        with self.assertRaises(AttributeError):
            pb2_grpc.Response

    def testCall(self):
        with _system_path([self.python_out_directory]):
            pb2 = importlib.import_module(self.pb2_import)
        with _system_path([self.grpc_python_out_directory]):
            pb2_grpc = importlib.import_module(self.pb2_grpc_import)
        server = grpc.server(
            futures.ThreadPoolExecutor(max_workers=test_constants.POOL_SIZE))
        pb2_grpc.add_TestServiceServicer_to_server(
            DummySplitServicer(pb2.Request, pb2.Response), server)
        port = server.add_insecure_port('[::]:0')
        server.start()
        channel = grpc.insecure_channel('localhost:{}'.format(port))
        stub = pb2_grpc.TestServiceStub(channel)
        request = pb2.Request()
        expected_response = pb2.Response()
        response = stub.Call(request)
        self.assertEqual(expected_response, response)


@unittest.skipIf(platform.python_implementation() == "PyPy",
                 "Skip test if run with PyPy")
class SameSeparateTest(unittest.TestCase, SeparateTestMixin):

    def setUp(self):
        same_proto_contents = pkgutil.get_data(
            'tests.protoc_plugin.protos.invocation_testing', 'same.proto')
        self.directory = tempfile.mkdtemp(suffix='same_separate', dir='.')
        self.proto_directory = os.path.join(self.directory, 'proto_path')
        self.python_out_directory = os.path.join(self.directory, 'python_out')
        self.grpc_python_out_directory = os.path.join(self.directory,
                                                      'grpc_python_out')
        os.makedirs(self.proto_directory)
        os.makedirs(self.python_out_directory)
        os.makedirs(self.grpc_python_out_directory)
        same_proto_file = os.path.join(self.proto_directory,
                                       'same_separate.proto')
        open(same_proto_file, 'wb').write(
            same_proto_contents.replace(
                _COMMON_NAMESPACE,
                b'package grpc_protoc_plugin.invocation_testing.same_separate;'))
        protoc_result = protoc.main([
            '',
            '--proto_path={}'.format(self.proto_directory),
            '--python_out={}'.format(self.python_out_directory),
            '--grpc_python_out=grpc_2_0:{}'.format(
                self.grpc_python_out_directory),
            same_proto_file,
        ])
        if protoc_result != 0:
            raise Exception("unexpected protoc error")
        open(os.path.join(self.grpc_python_out_directory, '__init__.py'),
             'w').write('')
        open(os.path.join(self.python_out_directory, '__init__.py'),
             'w').write('')
        self.pb2_import = 'same_separate_pb2'
        self.pb2_grpc_import = 'same_separate_pb2_grpc'
        self.should_find_services_in_pb2 = False

    def tearDown(self):
        shutil.rmtree(self.directory)


@unittest.skipIf(platform.python_implementation() == "PyPy",
                 "Skip test if run with PyPy")
class SameCommonTest(unittest.TestCase, CommonTestMixin):

    def setUp(self):
        same_proto_contents = pkgutil.get_data(
            'tests.protoc_plugin.protos.invocation_testing', 'same.proto')
        self.directory = tempfile.mkdtemp(suffix='same_common', dir='.')
        self.proto_directory = os.path.join(self.directory, 'proto_path')
        self.python_out_directory = os.path.join(self.directory, 'python_out')
        self.grpc_python_out_directory = self.python_out_directory
        os.makedirs(self.proto_directory)
        os.makedirs(self.python_out_directory)
        same_proto_file = os.path.join(self.proto_directory,
                                       'same_common.proto')
        open(same_proto_file, 'wb').write(
            same_proto_contents.replace(
                _COMMON_NAMESPACE,
                b'package grpc_protoc_plugin.invocation_testing.same_common;'))

        protoc_result = protoc.main([
            '',
            '--proto_path={}'.format(self.proto_directory),
            '--python_out={}'.format(self.python_out_directory),
            '--grpc_python_out={}'.format(self.grpc_python_out_directory),
            same_proto_file,
        ])
        if protoc_result != 0:
            raise Exception("unexpected protoc error")
        open(os.path.join(self.python_out_directory, '__init__.py'),
             'w').write('')
        self.pb2_import = 'same_common_pb2'
        self.pb2_grpc_import = 'same_common_pb2_grpc'
        self.should_find_services_in_pb2 = True

    def tearDown(self):
        shutil.rmtree(self.directory)


@unittest.skipIf(platform.python_implementation() == "PyPy",
                 "Skip test if run with PyPy")
class SplitCommonTest(unittest.TestCase, CommonTestMixin):

    def setUp(self):
        services_proto_contents = pkgutil.get_data(
            'tests.protoc_plugin.protos.invocation_testing.split_services',
            'services.proto')
        messages_proto_contents = pkgutil.get_data(
            'tests.protoc_plugin.protos.invocation_testing.split_messages',
            'messages.proto')
        self.directory = tempfile.mkdtemp(suffix='split_common', dir='.')
        self.proto_directory = os.path.join(self.directory, 'proto_path')
        self.python_out_directory = os.path.join(self.directory, 'python_out')
        self.grpc_python_out_directory = self.python_out_directory
        os.makedirs(self.proto_directory)
        os.makedirs(self.python_out_directory)
        services_proto_file = os.path.join(self.proto_directory,
                                           'split_common_services.proto')
        messages_proto_file = os.path.join(self.proto_directory,
                                           'split_common_messages.proto')
        open(services_proto_file, 'wb').write(
            services_proto_contents.replace(
                _MESSAGES_IMPORT, b'import "split_common_messages.proto";')
            .replace(
                _SPLIT_NAMESPACE,
                b'package grpc_protoc_plugin.invocation_testing.split_common;'))
        open(messages_proto_file, 'wb').write(
            messages_proto_contents.replace(
                _SPLIT_NAMESPACE,
                b'package grpc_protoc_plugin.invocation_testing.split_common;'))
        protoc_result = protoc.main([
            '',
            '--proto_path={}'.format(self.proto_directory),
            '--python_out={}'.format(self.python_out_directory),
            '--grpc_python_out={}'.format(self.grpc_python_out_directory),
            services_proto_file,
            messages_proto_file,
        ])
        if protoc_result != 0:
            raise Exception("unexpected protoc error")
        open(os.path.join(self.python_out_directory, '__init__.py'),
             'w').write('')
        self.pb2_import = 'split_common_messages_pb2'
        self.pb2_grpc_import = 'split_common_services_pb2_grpc'
        self.should_find_services_in_pb2 = False

    def tearDown(self):
        shutil.rmtree(self.directory)


@unittest.skipIf(platform.python_implementation() == "PyPy",
                 "Skip test if run with PyPy")
class SplitSeparateTest(unittest.TestCase, SeparateTestMixin):

    def setUp(self):
        services_proto_contents = pkgutil.get_data(
            'tests.protoc_plugin.protos.invocation_testing.split_services',
            'services.proto')
        messages_proto_contents = pkgutil.get_data(
            'tests.protoc_plugin.protos.invocation_testing.split_messages',
            'messages.proto')
        self.directory = tempfile.mkdtemp(suffix='split_separate', dir='.')
        self.proto_directory = os.path.join(self.directory, 'proto_path')
        self.python_out_directory = os.path.join(self.directory, 'python_out')
        self.grpc_python_out_directory = os.path.join(self.directory,
                                                      'grpc_python_out')
        os.makedirs(self.proto_directory)
        os.makedirs(self.python_out_directory)
        os.makedirs(self.grpc_python_out_directory)
        services_proto_file = os.path.join(self.proto_directory,
                                           'split_separate_services.proto')
        messages_proto_file = os.path.join(self.proto_directory,
                                           'split_separate_messages.proto')
        open(services_proto_file, 'wb').write(
            services_proto_contents.replace(
                _MESSAGES_IMPORT, b'import "split_separate_messages.proto";')
            .replace(
                _SPLIT_NAMESPACE,
                b'package grpc_protoc_plugin.invocation_testing.split_separate;'
            ))
        open(messages_proto_file, 'wb').write(
            messages_proto_contents.replace(
                _SPLIT_NAMESPACE,
                b'package grpc_protoc_plugin.invocation_testing.split_separate;'
            ))
        protoc_result = protoc.main([
            '',
            '--proto_path={}'.format(self.proto_directory),
            '--python_out={}'.format(self.python_out_directory),
            '--grpc_python_out=grpc_2_0:{}'.format(
                self.grpc_python_out_directory),
            services_proto_file,
            messages_proto_file,
        ])
        if protoc_result != 0:
            raise Exception("unexpected protoc error")
        open(os.path.join(self.python_out_directory, '__init__.py'),
             'w').write('')
        self.pb2_import = 'split_separate_messages_pb2'
        self.pb2_grpc_import = 'split_separate_services_pb2_grpc'
        self.should_find_services_in_pb2 = False

    def tearDown(self):
        shutil.rmtree(self.directory)


if __name__ == '__main__':
    unittest.main(verbosity=2)

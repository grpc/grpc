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

import abc
import contextlib
import importlib
import os
from os import path
import pkgutil
import platform
import shutil
import sys
import tempfile
import unittest

import six

import grpc
from grpc_tools import protoc
from tests.unit import test_common

_MESSAGES_IMPORT = b'import "messages.proto";'
_SPLIT_NAMESPACE = b'package grpc_protoc_plugin.invocation_testing.split;'
_COMMON_NAMESPACE = b'package grpc_protoc_plugin.invocation_testing;'

_RELATIVE_PROTO_PATH = 'relative_proto_path'
_RELATIVE_PYTHON_OUT = 'relative_python_out'


@contextlib.contextmanager
def _system_path(path_insertion):
    old_system_path = sys.path[:]
    sys.path = sys.path[0:1] + path_insertion + sys.path[1:]
    yield
    sys.path = old_system_path


# NOTE(nathaniel): https://twitter.com/exoplaneteer/status/677259364256747520
# Life lesson "just always default to idempotence" reinforced.
def _create_directory_tree(root, path_components_sequence):
    created = set()
    for path_components in path_components_sequence:
        thus_far = ''
        for path_component in path_components:
            relative_path = path.join(thus_far, path_component)
            if relative_path not in created:
                os.makedirs(path.join(root, relative_path))
                created.add(relative_path)
            thus_far = path.join(thus_far, path_component)


def _massage_proto_content(proto_content, test_name_bytes,
                           messages_proto_relative_file_name_bytes):
    package_substitution = (b'package grpc_protoc_plugin.invocation_testing.' +
                            test_name_bytes + b';')
    common_namespace_substituted = proto_content.replace(
        _COMMON_NAMESPACE, package_substitution)
    split_namespace_substituted = common_namespace_substituted.replace(
        _SPLIT_NAMESPACE, package_substitution)
    message_import_replaced = split_namespace_substituted.replace(
        _MESSAGES_IMPORT,
        b'import "' + messages_proto_relative_file_name_bytes + b'";')
    return message_import_replaced


def _packagify(directory):
    for subdirectory, _, _ in os.walk(directory):
        init_file_name = path.join(subdirectory, '__init__.py')
        with open(init_file_name, 'wb') as init_file:
            init_file.write(b'')


class _Servicer(object):

    def __init__(self, response_class):
        self._response_class = response_class

    def Call(self, request, context):
        return self._response_class()


def _protoc(proto_path, python_out, grpc_python_out_flag, grpc_python_out,
            absolute_proto_file_names):
    args = [
        '',
        '--proto_path={}'.format(proto_path),
    ]
    if python_out is not None:
        args.append('--python_out={}'.format(python_out))
    if grpc_python_out is not None:
        args.append('--grpc_python_out={}:{}'.format(grpc_python_out_flag,
                                                     grpc_python_out))
    args.extend(absolute_proto_file_names)
    return protoc.main(args)


class _Mid2016ProtocStyle(object):

    def name(self):
        return 'Mid2016ProtocStyle'

    def grpc_in_pb2_expected(self):
        return True

    def protoc(self, proto_path, python_out, absolute_proto_file_names):
        return (_protoc(proto_path, python_out, 'grpc_1_0', python_out,
                        absolute_proto_file_names),)


class _SingleProtocExecutionProtocStyle(object):

    def name(self):
        return 'SingleProtocExecutionProtocStyle'

    def grpc_in_pb2_expected(self):
        return False

    def protoc(self, proto_path, python_out, absolute_proto_file_names):
        return (_protoc(proto_path, python_out, 'grpc_2_0', python_out,
                        absolute_proto_file_names),)


class _ProtoBeforeGrpcProtocStyle(object):

    def name(self):
        return 'ProtoBeforeGrpcProtocStyle'

    def grpc_in_pb2_expected(self):
        return False

    def protoc(self, proto_path, python_out, absolute_proto_file_names):
        pb2_protoc_exit_code = _protoc(proto_path, python_out, None, None,
                                       absolute_proto_file_names)
        pb2_grpc_protoc_exit_code = _protoc(
            proto_path, None, 'grpc_2_0', python_out, absolute_proto_file_names)
        return pb2_protoc_exit_code, pb2_grpc_protoc_exit_code,


class _GrpcBeforeProtoProtocStyle(object):

    def name(self):
        return 'GrpcBeforeProtoProtocStyle'

    def grpc_in_pb2_expected(self):
        return False

    def protoc(self, proto_path, python_out, absolute_proto_file_names):
        pb2_grpc_protoc_exit_code = _protoc(
            proto_path, None, 'grpc_2_0', python_out, absolute_proto_file_names)
        pb2_protoc_exit_code = _protoc(proto_path, python_out, None, None,
                                       absolute_proto_file_names)
        return pb2_grpc_protoc_exit_code, pb2_protoc_exit_code,


_PROTOC_STYLES = (
    _Mid2016ProtocStyle(),
    _SingleProtocExecutionProtocStyle(),
    _ProtoBeforeGrpcProtocStyle(),
    _GrpcBeforeProtoProtocStyle(),
)


@unittest.skipIf(platform.python_implementation() == 'PyPy',
                 'Skip test if run with PyPy!')
class _Test(six.with_metaclass(abc.ABCMeta, unittest.TestCase)):

    def setUp(self):
        self._directory = tempfile.mkdtemp(suffix=self.NAME, dir='.')
        self._proto_path = path.join(self._directory, _RELATIVE_PROTO_PATH)
        self._python_out = path.join(self._directory, _RELATIVE_PYTHON_OUT)

        os.makedirs(self._proto_path)
        os.makedirs(self._python_out)

        proto_directories_and_names = {
            (
                self.MESSAGES_PROTO_RELATIVE_DIRECTORY_NAMES,
                self.MESSAGES_PROTO_FILE_NAME,
            ),
            (
                self.SERVICES_PROTO_RELATIVE_DIRECTORY_NAMES,
                self.SERVICES_PROTO_FILE_NAME,
            ),
        }
        messages_proto_relative_file_name_forward_slashes = '/'.join(
            self.MESSAGES_PROTO_RELATIVE_DIRECTORY_NAMES +
            (self.MESSAGES_PROTO_FILE_NAME,))
        _create_directory_tree(self._proto_path,
                               (relative_proto_directory_names
                                for relative_proto_directory_names, _ in
                                proto_directories_and_names))
        self._absolute_proto_file_names = set()
        for relative_directory_names, file_name in proto_directories_and_names:
            absolute_proto_file_name = path.join(
                self._proto_path, *relative_directory_names + (file_name,))
            raw_proto_content = pkgutil.get_data(
                'tests.protoc_plugin.protos.invocation_testing',
                path.join(*relative_directory_names + (file_name,)))
            massaged_proto_content = _massage_proto_content(
                raw_proto_content, self.NAME.encode(),
                messages_proto_relative_file_name_forward_slashes.encode())
            with open(absolute_proto_file_name, 'wb') as proto_file:
                proto_file.write(massaged_proto_content)
            self._absolute_proto_file_names.add(absolute_proto_file_name)

    def tearDown(self):
        shutil.rmtree(self._directory)

    def _protoc(self):
        protoc_exit_codes = self.PROTOC_STYLE.protoc(
            self._proto_path, self._python_out, self._absolute_proto_file_names)
        for protoc_exit_code in protoc_exit_codes:
            self.assertEqual(0, protoc_exit_code)

        _packagify(self._python_out)

        generated_modules = {}
        expected_generated_full_module_names = {
            self.EXPECTED_MESSAGES_PB2,
            self.EXPECTED_SERVICES_PB2,
            self.EXPECTED_SERVICES_PB2_GRPC,
        }
        with _system_path([self._python_out]):
            for full_module_name in expected_generated_full_module_names:
                module = importlib.import_module(full_module_name)
                generated_modules[full_module_name] = module

        self._messages_pb2 = generated_modules[self.EXPECTED_MESSAGES_PB2]
        self._services_pb2 = generated_modules[self.EXPECTED_SERVICES_PB2]
        self._services_pb2_grpc = generated_modules[
            self.EXPECTED_SERVICES_PB2_GRPC]

    def _services_modules(self):
        if self.PROTOC_STYLE.grpc_in_pb2_expected():
            return self._services_pb2, self._services_pb2_grpc,
        else:
            return self._services_pb2_grpc,

    def test_imported_attributes(self):
        self._protoc()

        self._messages_pb2.Request
        self._messages_pb2.Response
        self._services_pb2.DESCRIPTOR.services_by_name['TestService']
        for services_module in self._services_modules():
            services_module.TestServiceStub
            services_module.TestServiceServicer
            services_module.add_TestServiceServicer_to_server

    def test_call(self):
        self._protoc()

        for services_module in self._services_modules():
            server = test_common.test_server()
            services_module.add_TestServiceServicer_to_server(
                _Servicer(self._messages_pb2.Response), server)
            port = server.add_insecure_port('[::]:0')
            server.start()
            channel = grpc.insecure_channel('localhost:{}'.format(port))
            stub = services_module.TestServiceStub(channel)
            response = stub.Call(self._messages_pb2.Request())
            self.assertEqual(self._messages_pb2.Response(), response)
            server.stop(None)


def _create_test_case_class(split_proto, protoc_style):
    attributes = {}

    name = '{}{}'.format('SplitProto' if split_proto else 'SameProto',
                         protoc_style.name())
    attributes['NAME'] = name

    if split_proto:
        attributes['MESSAGES_PROTO_RELATIVE_DIRECTORY_NAMES'] = (
            'split_messages',
            'sub',
        )
        attributes['MESSAGES_PROTO_FILE_NAME'] = 'messages.proto'
        attributes['SERVICES_PROTO_RELATIVE_DIRECTORY_NAMES'] = (
            'split_services',)
        attributes['SERVICES_PROTO_FILE_NAME'] = 'services.proto'
        attributes['EXPECTED_MESSAGES_PB2'] = 'split_messages.sub.messages_pb2'
        attributes['EXPECTED_SERVICES_PB2'] = 'split_services.services_pb2'
        attributes['EXPECTED_SERVICES_PB2_GRPC'] = (
            'split_services.services_pb2_grpc')
    else:
        attributes['MESSAGES_PROTO_RELATIVE_DIRECTORY_NAMES'] = ()
        attributes['MESSAGES_PROTO_FILE_NAME'] = 'same.proto'
        attributes['SERVICES_PROTO_RELATIVE_DIRECTORY_NAMES'] = ()
        attributes['SERVICES_PROTO_FILE_NAME'] = 'same.proto'
        attributes['EXPECTED_MESSAGES_PB2'] = 'same_pb2'
        attributes['EXPECTED_SERVICES_PB2'] = 'same_pb2'
        attributes['EXPECTED_SERVICES_PB2_GRPC'] = 'same_pb2_grpc'

    attributes['PROTOC_STYLE'] = protoc_style

    attributes['__module__'] = _Test.__module__

    return type('{}Test'.format(name), (_Test,), attributes)


def _create_test_case_classes():
    for split_proto in (
            False,
            True,
    ):
        for protoc_style in _PROTOC_STYLES:
            yield _create_test_case_class(split_proto, protoc_style)


def load_tests(loader, tests, pattern):
    tests = tuple(
        loader.loadTestsFromTestCase(test_case_class)
        for test_case_class in _create_test_case_classes())
    return unittest.TestSuite(tests=tests)


if __name__ == '__main__':
    unittest.main(verbosity=2)

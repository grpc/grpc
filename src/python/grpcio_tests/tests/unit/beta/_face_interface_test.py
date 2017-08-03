# Copyright 2015 gRPC authors.
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
"""Tests Face interface compliance of the gRPC Python Beta API."""

import collections
import unittest

import six

from grpc.beta import implementations
from grpc.beta import interfaces
from tests.unit import resources
from tests.unit import test_common as grpc_test_common
from tests.unit.beta import test_utilities
from tests.unit.framework.common import test_constants
from tests.unit.framework.interfaces.face import test_cases
from tests.unit.framework.interfaces.face import test_interfaces

_SERVER_HOST_OVERRIDE = 'foo.test.google.fr'


class _SerializationBehaviors(
        collections.namedtuple('_SerializationBehaviors', (
            'request_serializers', 'request_deserializers',
            'response_serializers', 'response_deserializers',))):
    pass


def _serialization_behaviors_from_test_methods(test_methods):
    request_serializers = {}
    request_deserializers = {}
    response_serializers = {}
    response_deserializers = {}
    for (group, method), test_method in six.iteritems(test_methods):
        request_serializers[group, method] = test_method.serialize_request
        request_deserializers[group, method] = test_method.deserialize_request
        response_serializers[group, method] = test_method.serialize_response
        response_deserializers[group, method] = test_method.deserialize_response
    return _SerializationBehaviors(request_serializers, request_deserializers,
                                   response_serializers, response_deserializers)


class _Implementation(test_interfaces.Implementation):

    def instantiate(self, methods, method_implementations,
                    multi_method_implementation):
        serialization_behaviors = _serialization_behaviors_from_test_methods(
            methods)
        # TODO(nathaniel): Add a "groups" attribute to _digest.TestServiceDigest.
        service = next(iter(methods))[0]
        # TODO(nathaniel): Add a "cardinalities_by_group" attribute to
        # _digest.TestServiceDigest.
        cardinalities = {
            method: method_object.cardinality()
            for (group, method), method_object in six.iteritems(methods)
        }

        server_options = implementations.server_options(
            request_deserializers=serialization_behaviors.request_deserializers,
            response_serializers=serialization_behaviors.response_serializers,
            thread_pool_size=test_constants.POOL_SIZE)
        server = implementations.server(
            method_implementations, options=server_options)
        server_credentials = implementations.ssl_server_credentials([
            (resources.private_key(), resources.certificate_chain(),),
        ])
        port = server.add_secure_port('[::]:0', server_credentials)
        server.start()
        channel_credentials = implementations.ssl_channel_credentials(
            resources.test_root_certificates())
        channel = test_utilities.not_really_secure_channel(
            'localhost', port, channel_credentials, _SERVER_HOST_OVERRIDE)
        stub_options = implementations.stub_options(
            request_serializers=serialization_behaviors.request_serializers,
            response_deserializers=serialization_behaviors.
            response_deserializers,
            thread_pool_size=test_constants.POOL_SIZE)
        generic_stub = implementations.generic_stub(
            channel, options=stub_options)
        dynamic_stub = implementations.dynamic_stub(
            channel, service, cardinalities, options=stub_options)
        return generic_stub, {service: dynamic_stub}, server

    def destantiate(self, memo):
        memo.stop(test_constants.SHORT_TIMEOUT).wait()

    def invocation_metadata(self):
        return grpc_test_common.INVOCATION_INITIAL_METADATA

    def initial_metadata(self):
        return grpc_test_common.SERVICE_INITIAL_METADATA

    def terminal_metadata(self):
        return grpc_test_common.SERVICE_TERMINAL_METADATA

    def code(self):
        return interfaces.StatusCode.OK

    def details(self):
        return grpc_test_common.DETAILS

    def metadata_transmitted(self, original_metadata, transmitted_metadata):
        return original_metadata is None or grpc_test_common.metadata_transmitted(
            original_metadata, transmitted_metadata)


def load_tests(loader, tests, pattern):
    return unittest.TestSuite(tests=tuple(
        loader.loadTestsFromTestCase(test_case_class)
        for test_case_class in test_cases.test_cases(_Implementation())))


if __name__ == '__main__':
    unittest.main(verbosity=2)

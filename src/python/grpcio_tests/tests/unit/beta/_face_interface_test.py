# Copyright 2015, Google Inc.
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

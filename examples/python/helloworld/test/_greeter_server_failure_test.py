"""Test for helloworld greeter server with server failure negative condition."""

import grpc
import grpc_testing
import unittest

from python.helloworld import helloworld_pb2
from python.helloworld import Greeter


class TestGreeterFailure(unittest.TestCase):
    def setUp(self):
        servicers = {
            helloworld_pb2.DESCRIPTOR.services_by_name['Greeter']: Greeter()
        }

        self.test_server = grpc_testing.server_from_dictionary(
            servicers, grpc_testing.strict_real_time())

    def test_helloworld_error(self):
        """ expect to get Greeter response """
        name = "ERROR"
        request = helloworld_pb2.HelloRequest(name=name)

        sayhello_method = self.test_server.invoke_unary_unary(
            method_descriptor=(helloworld_pb2.DESCRIPTOR
                .services_by_name['Greeter']
                .methods_by_name['SayHello']),
            invocation_metadata={},
            request=request, timeout=1)

        response, metadata, code, details = sayhello_method.termination()

        # this assertion fails because invocation returns
        # grpc.StatusCode.UNKNOWN instead of what was raised by the method
        self.assertEqual(code, grpc.StatusCode.FAILED_PRECONDITION)


if __name__ == '__main__':
    unittest.main()

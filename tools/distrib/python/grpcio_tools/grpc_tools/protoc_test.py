"""Tests for protoc."""

from __future__ import absolute_import
from __future__ import division
from __future__ import print_function

import unittest
import grpc_tools



class ProtocTest(unittest.TestCase):

    # def test_import_protos(self):
    #     protos, services = grpc_tools.import_protos("grpc_tools/simple.proto", "tools/distrib/python/grpcio_tools/")
    #     print(dir(protos))
    #     print(dir(services))

    # # TODO: Ensure that we don't pollute STDOUT by invoking protoc.
    # def test_stdout_pollution(self):
    #     pass

    # def test_protoc_in_memory(self):
    #     from grpc_tools import protoc
    #     proto_path = "tools/distrib/python/grpcio_tools/"
    #     protos = protoc.get_protos("grpc_tools/simple.proto", proto_path)
    #     print(protos.SimpleMessageRequest)
    #     services = protoc.get_services("grpc_tools/simple.proto", proto_path)
    #     print(services.SimpleMessageServiceServicer)
    #     complicated_protos = protoc.get_protos("grpc_tools/complicated.proto", proto_path)
    #     print(complicated_protos.ComplicatedMessage)
    #     print(dir(complicated_protos.grpc__tools_dot_simplest__pb2))
    #     print(dir(protos.grpc__tools_dot_simpler__pb2.grpc__tools_dot_simplest__pb2))
    #     print("simplest is simplest: {}".format(complicated_protos.grpc__tools_dot_simplest__pb2.SimplestMessage is protos.grpc__tools_dot_simpler__pb2.grpc__tools_dot_simplest__pb2.SimplesMessage))

    # TODO: Test error messages.

    # TODO: These test cases have to run in different processes to be truly
    # independent of one another.

    def test_import_protos(self):
        from grpc_tools import protoc
        proto_path = "tools/distrib/python/grpcio_tools/"
        protos = protoc.get_protos("grpc_tools/simple.proto", proto_path)
        self.assertIsNotNone(protos.SimpleMessage)

    def test_import_services(self):
        from grpc_tools import protoc
        proto_path = "tools/distrib/python/grpcio_tools/"
        # TODO: Should we make this step optional if you only want to import
        # services?
        protos = protoc.get_protos("grpc_tools/simple.proto", proto_path)
        services = protoc.get_services("grpc_tools/simple.proto", proto_path)
        self.assertIsNotNone(services.SimpleMessageServiceStub)

    def test_proto_module_imported_once(self):
        from grpc_tools import protoc
        proto_path = "tools/distrib/python/grpcio_tools/"
        protos = protoc.get_protos("grpc_tools/simple.proto", proto_path)
        services = protoc.get_services("grpc_tools/simple.proto", proto_path)
        complicated_protos = protoc.get_protos("grpc_tools/complicated.proto", proto_path)
        self.assertIs(complicated_protos.grpc__tools_dot_simplest__pb2.SimplestMessage,
                      protos.grpc__tools_dot_simpler__pb2.grpc__tools_dot_simplest__pb2.SimplestMessage)


if __name__ == '__main__':
    unittest.main()

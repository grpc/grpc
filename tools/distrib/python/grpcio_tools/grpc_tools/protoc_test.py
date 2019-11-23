"""Tests for protoc."""

from __future__ import absolute_import
from __future__ import division
from __future__ import print_function

import importlib

import unittest
import grpc_tools

import os



class ProtocTest(unittest.TestCase):

    def test_protoc(self):
        # TODO: Get this thing to just give me the code via an FD.
        # TODO: Figure out what to do about STDOUT pollution.
        # TODO: How do we convert protoc failure into a Python error?
        protos, services = grpc_tools.import_protos("grpc_tools/simple.proto", "tools/distrib/python/grpcio_tools/")
        print(dir(protos))
        print(dir(services))


if __name__ == '__main__':
    unittest.main()

"""Tests for protoc."""

from __future__ import absolute_import
from __future__ import division
from __future__ import print_function

import unittest

import multiprocessing
import functools

def _wrap_in_subprocess(error_queue, fn):
    @functools.wraps(fn)
    def _wrapped():
        try:
            fn()
        except Exception as e:
            error_queue.put(e)
            raise
    return _wrapped

def _run_in_subprocess(test_case):
    error_queue = multiprocessing.Queue()
    wrapped_case = _wrap_in_subprocess(error_queue, test_case)
    proc = multiprocessing.Process(target=wrapped_case)
    proc.start()
    proc.join()
    if not error_queue.empty():
        raise error_queue.get()

def _test_import_protos():
    from grpc_tools import protoc
    proto_path = "tools/distrib/python/grpcio_tools/"
    protos = protoc.get_protos("grpc_tools/simple.proto", [proto_path])
    assert protos.SimpleMessage is not None


def _test_import_services():
    from grpc_tools import protoc
    proto_path = "tools/distrib/python/grpcio_tools/"
    protos = protoc.get_protos("grpc_tools/simple.proto", [proto_path])
    services = protoc.get_services("grpc_tools/simple.proto", [proto_path])
    assert services.SimpleMessageServiceStub is not None


# NOTE: In this case, we use sys.path to determine where to look for our protos.
def _test_import_implicit_include():
    from grpc_tools import protoc
    protos = protoc.get_protos("grpc_tools/simple.proto")
    services = protoc.get_services("grpc_tools/simple.proto")
    assert services.SimpleMessageServiceStub is not None


def _test_import_services_without_protos():
    from grpc_tools import protoc
    services = protoc.get_services("grpc_tools/simple.proto")
    assert services.SimpleMessageServiceStub is not None


def _test_proto_module_imported_once():
    from grpc_tools import protoc
    proto_path = "tools/distrib/python/grpcio_tools/"
    protos = protoc.get_protos("grpc_tools/simple.proto", [proto_path])
    services = protoc.get_services("grpc_tools/simple.proto", [proto_path])
    complicated_protos = protoc.get_protos("grpc_tools/complicated.proto", [proto_path])
    assert (complicated_protos.grpc__tools_dot_simplest__pb2.SimplestMessage is
            protos.grpc__tools_dot_simpler__pb2.grpc__tools_dot_simplest__pb2.SimplestMessage)


def _test_static_dynamic_combo():
    from grpc_tools import complicated_pb2
    from grpc_tools import protoc
    proto_path = "tools/distrib/python/grpcio_tools/"
    protos = protoc.get_protos("grpc_tools/simple.proto", [proto_path])
    assert (complicated_pb2.grpc__tools_dot_simplest__pb2.SimplestMessage is
            protos.grpc__tools_dot_simpler__pb2.grpc__tools_dot_simplest__pb2.SimplestMessage)


def _test_combined_import():
    from grpc_tools import protoc
    protos, services = protoc.get_protos_and_services("grpc_tools/simple.proto")
    assert protos.SimpleMessage is not None
    assert services.SimpleMessageServiceStub is not None


def _test_syntax_errors():
    from grpc_tools import protoc
    try:
        protos = protoc.get_protos("grpc_tools/flawed.proto")
    except Exception as e:
        error_str = str(e)
        assert "flawed.proto" in error_str
        assert "3:23" in error_str
        assert "7:23" in error_str
    else:
        assert False, "Compile error expected. None occurred."

# TODO: Test warnings.


class ProtocTest(unittest.TestCase):

    def test_import_protos(self):
        _run_in_subprocess(_test_import_protos)

    def test_import_services(self):
        _run_in_subprocess(_test_import_services)

    def test_import_implicit_include_path(self):
        _run_in_subprocess(_test_import_implicit_include)

    def test_import_services_without_protos(self):
        _run_in_subprocess(_test_import_services_without_protos)

    def test_proto_module_imported_once(self):
        _run_in_subprocess(_test_proto_module_imported_once)

    def test_static_dynamic_combo(self):
        _run_in_subprocess(_test_static_dynamic_combo)

    def test_combined_import(self):
        _run_in_subprocess(_test_combined_import)

    def test_syntax_errors(self):
        _run_in_subprocess(_test_syntax_errors)

    # TODO: Write test to ensure the right module loader is used.

if __name__ == '__main__':
    unittest.main()

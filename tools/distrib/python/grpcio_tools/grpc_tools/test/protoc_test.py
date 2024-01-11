# Copyright 2020 The gRPC authors.
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
"""Tests for protoc."""

from __future__ import absolute_import
from __future__ import division
from __future__ import print_function

import contextlib
import functools
import multiprocessing
import sys
import unittest


# TODO(https://github.com/grpc/grpc/issues/23847): Deduplicate this mechanism with
# the grpcio_tests module.
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
    assert proc.exitcode == 0, "Process exited with code {}".format(
        proc.exitcode
    )


@contextlib.contextmanager
def _augmented_syspath(new_paths):
    original_sys_path = sys.path
    if new_paths is not None:
        sys.path = list(new_paths) + sys.path
    try:
        yield
    finally:
        sys.path = original_sys_path


def _test_import_protos():
    from grpc_tools import protoc

    with _augmented_syspath(
        ("tools/distrib/python/grpcio_tools/grpc_tools/test/",)
    ):
        protos = protoc._protos("simple.proto")
        assert protos.SimpleMessage is not None


def _test_import_services():
    from grpc_tools import protoc

    with _augmented_syspath(
        ("tools/distrib/python/grpcio_tools/grpc_tools/test/",)
    ):
        protos = protoc._protos("simple.proto")
        services = protoc._services("simple.proto")
        assert services.SimpleMessageServiceStub is not None


def _test_import_services_without_protos():
    from grpc_tools import protoc

    with _augmented_syspath(
        ("tools/distrib/python/grpcio_tools/grpc_tools/test/",)
    ):
        services = protoc._services("simple.proto")
        assert services.SimpleMessageServiceStub is not None


def _test_proto_module_imported_once():
    from grpc_tools import protoc

    with _augmented_syspath(
        ("tools/distrib/python/grpcio_tools/grpc_tools/test/",)
    ):
        protos = protoc._protos("simple.proto")
        services = protoc._services("simple.proto")
        complicated_protos = protoc._protos("complicated.proto")
        simple_message = protos.SimpleMessage()
        complicated_message = complicated_protos.ComplicatedMessage()
        assert (
            simple_message.simpler_message.simplest_message.__class__
            is complicated_message.simplest_message.__class__
        )


def _test_static_dynamic_combo():
    with _augmented_syspath(
        ("tools/distrib/python/grpcio_tools/grpc_tools/test/",)
    ):
        from grpc_tools import protoc  # isort:skip
        import complicated_pb2

        protos = protoc._protos("simple.proto")
        static_message = complicated_pb2.ComplicatedMessage()
        dynamic_message = protos.SimpleMessage()
        assert (
            dynamic_message.simpler_message.simplest_message.__class__
            is static_message.simplest_message.__class__
        )


def _test_combined_import():
    from grpc_tools import protoc

    protos, services = protoc._protos_and_services("simple.proto")
    assert protos.SimpleMessage is not None
    assert services.SimpleMessageServiceStub is not None


def _test_syntax_errors():
    from grpc_tools import protoc

    try:
        protos = protoc._protos("flawed.proto")
    except Exception as e:
        error_str = str(e)
        assert "flawed.proto" in error_str
        assert "17:23" in error_str
        assert "21:23" in error_str
    else:
        assert False, "Compile error expected. None occurred."


class ProtocTest(unittest.TestCase):
    def test_import_protos(self):
        _run_in_subprocess(_test_import_protos)

    def test_import_services(self):
        _run_in_subprocess(_test_import_services)

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


if __name__ == "__main__":
    unittest.main()

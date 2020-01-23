# Copyright 2019 The gRPC authors.
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
"""Test of dynamic stub import API."""

import inspect
import unittest
import logging
import contextlib
import sys
import multiprocessing
import functools


@contextlib.contextmanager
def _protobuf_unimportable():
    original_sys_path = sys.path
    sys.path = [path for path in sys.path if "protobuf" not in path]
    try:
        yield
    finally:
        sys.path = original_sys_path


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
        proc.exitcode)


def _assert_unimplemented(msg_substr):
    import grpc
    try:
        protos, services = grpc.protos_and_services(
            "tests/unit/data/foo/bar.proto")
    except NotImplementedError as e:
        assert msg_substr in str(e), "{} was not in '{}'".format(
            msg_substr, str(e))
    else:
        assert False, "Did not raise NotImplementedError"


def _test_sunny_day():
    import grpc
    protos, services = grpc.protos_and_services("tests/unit/data/foo/bar.proto")
    assert protos.BarMessage is not None
    assert services.BarStub is not None

    class CustomBarServicer(services.BarServicer):

        def GetBar(self, request, context):
            return protos.BarMessage(a=request.a + request.a)

    from concurrent import futures
    server = grpc.server(futures.ThreadPoolExecutor())
    services.add_BarServicer_to_server(CustomBarServicer(), server)
    actual_port = server.add_secure_port('localhost:0',
                                         grpc.local_server_credentials())
    server.start()
    msg_str = "bar"
    with grpc.secure_channel('localhost:{}'.format(actual_port),
                             grpc.local_channel_credentials()) as channel:
        bar_stub = services.BarStub(channel)
        request = protos.BarMessage(a=msg_str)
        response = bar_stub.GetBar(request)
        assert msg_str * 2 == response.a
        try:
            bar_stub.CreateBar(request)
        except grpc.RpcError as e:
            assert e.code() == grpc.StatusCode.UNIMPLEMENTED
        else:
            assert False, "Unimplemented method did not raise!"


def _test_python2_import():
    _assert_unimplemented("Python 3")


def _test_public_symbols():
    import grpc
    services = grpc.services("tests/unit/data/foo/bar.proto")
    public_symbols = [
        symbol for symbol in dir(services) if not symbol.startswith("_")
    ]
    expected_symbols = set(("BarServicer", "BarStub",
                            "add_BarServicer_to_server"))
    assert expected_symbols == set(public_symbols)
    servicer_methods = [
        symbol for symbol in dir(services.BarServicer)
        if not symbol.startswith("_") and
        inspect.isfunction(getattr(services.BarServicer, symbol))
    ]
    expected_servicer_methods = set(("GetBar", "CreateBar"))
    assert expected_servicer_methods == set(servicer_methods)
    assert inspect.isfunction(services.add_BarServicer_to_server)


def _test_protobuf_unimportable():
    with _protobuf_unimportable():
        if sys.version_info[0] == 3:
            _assert_unimplemented("protobuf")


@unittest.skipIf(sys.version_info[0] < 3, "Not supported on Python 2.")
class DynamicStubTest(unittest.TestCase):

    def test_sunny_day(self):
        _run_in_subprocess(_test_sunny_day)

    def test_public_symbols(self):
        _run_in_subprocess(_test_public_symbols)

    def test_protobuf_unimportable(self):
        _run_in_subprocess(_test_protobuf_unimportable)


@unittest.skipIf(sys.version_info[0] != 2, "Only run on Python 2.")
class Python2DynamicStubTest(unittest.TestCase):

    def test_python2_import(self):
        _run_in_subprocess(_test_python2_import)


if __name__ == "__main__":
    logging.basicConfig()
    unittest.main(verbosity=2)

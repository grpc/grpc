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

import contextlib
import functools
import logging
import multiprocessing
import os
import sys
import unittest

from tests.unit import test_common

_DATA_DIR = os.path.join("tests", "unit", "data")


@contextlib.contextmanager
def _grpc_tools_unimportable():
    original_sys_path = sys.path
    sys.path = [path for path in sys.path if "grpcio_tools" not in str(path)]
    try:
        import grpc_tools
    except ImportError:
        pass
    else:
        del grpc_tools
        sys.path = original_sys_path
        raise unittest.SkipTest("Failed to make grpc_tools unimportable.")
    try:
        yield
    finally:
        sys.path = original_sys_path


def _collect_errors(fn):
    @functools.wraps(fn)
    def _wrapped(error_queue):
        try:
            fn()
        except Exception as e:
            error_queue.put(e)
            raise

    return _wrapped


def _python3_check(fn):
    @functools.wraps(fn)
    def _wrapped():
        if sys.version_info[0] == 3:
            fn()
        else:
            _assert_unimplemented("Python 3")

    return _wrapped


def _run_in_subprocess(test_case):
    sys.path.insert(
        0, os.path.join(os.path.realpath(os.path.dirname(__file__)), "..")
    )
    error_queue = multiprocessing.Queue()
    proc = multiprocessing.Process(target=test_case, args=(error_queue,))
    proc.start()
    proc.join()
    sys.path.pop(0)
    if not error_queue.empty():
        raise error_queue.get()
    assert proc.exitcode == 0, "Process exited with code {}".format(
        proc.exitcode
    )


def _assert_unimplemented(msg_substr):
    import grpc

    try:
        protos, services = grpc.protos_and_services(
            "tests/unit/data/foo/bar.proto"
        )
    except NotImplementedError as e:
        assert msg_substr in str(e), "{} was not in '{}'".format(
            msg_substr, str(e)
        )
    else:
        assert False, "Did not raise NotImplementedError"


@_collect_errors
@_python3_check
def _test_sunny_day():
    import grpc

    protos, services = grpc.protos_and_services(
        os.path.join(_DATA_DIR, "foo", "bar.proto")
    )
    assert protos.BarMessage is not None
    assert services.BarStub is not None


@_collect_errors
@_python3_check
def _test_well_known_types():
    import grpc

    protos, services = grpc.protos_and_services(
        os.path.join(_DATA_DIR, "foo", "bar_with_wkt.proto")
    )
    assert protos.BarMessage is not None
    assert services.BarStub is not None


@_collect_errors
@_python3_check
def _test_grpc_tools_unimportable():
    with _grpc_tools_unimportable():
        _assert_unimplemented("grpcio-tools")


# NOTE(rbellevi): multiprocessing.Process fails to pickle function objects
# when they do not come from the "__main__" module, so this test passes
# if run directly on Windows or MacOS, but not if started by the test runner.
@unittest.skipIf(
    os.name == "nt" or "darwin" in sys.platform,
    "Windows and MacOS multiprocessing unsupported",
)
class DynamicStubTest(unittest.TestCase):
    def test_sunny_day(self):
        _run_in_subprocess(_test_sunny_day)

    def test_well_known_types(self):
        _run_in_subprocess(_test_well_known_types)

    def test_grpc_tools_unimportable(self):
        _run_in_subprocess(_test_grpc_tools_unimportable)


if __name__ == "__main__":
    logging.basicConfig()
    unittest.main(verbosity=2)

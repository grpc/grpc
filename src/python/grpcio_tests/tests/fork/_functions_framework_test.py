# Copyright 2026 gRPC authors.
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

import os

import socket
import subprocess
import sys
import tempfile
import time
import unittest

from src.proto.grpc.testing import test_pb2_grpc
from tests.fork import debugger
from tests.interop import service as interop_service
from tests.unit import test_common


def _dump_streams(name, streams):
    assert len(streams) == 2
    for stream_name, stream in zip(("STDOUT", "STDERR"), streams):
        stream.seek(0)
        sys.stderr.write(
            "{} {}:\n{}\n".format(name, stream_name, stream.read().decode())
        )
        stream.close()
    sys.stderr.flush()



_SUBPROCESS_TIMEOUT_S = 80


@unittest.skipUnless(
    sys.platform.startswith("linux") or sys.platform.startswith("darwin"),
    f"not supported on {sys.platform}",
)
class FunctionsFrameworkForkTest(unittest.TestCase):

    def testFunctionsFrameworkForkOnInitialCallOn(self):
        self._verifyTestCase(fork_support=True, initial_call=True)

    def testFunctionsFrameworkForkOnInitialCallOff(self):
        self._verifyTestCase(fork_support=True, initial_call=False)

    def _verifyTestCase(self, fork_support, initial_call):
        server = test_common.test_server()
        test_pb2_grpc.add_TestServiceServicer_to_server(
            interop_service.TestService(), server)
        port = server.add_insecure_port("[::]:0")
        server.start()

        # No need to write temp file, the script is extracted to a separate file.
        script_path = os.path.join(os.path.dirname(__file__), "_functions_framework_app.py")


        streams = tuple(tempfile.TemporaryFile() for _ in range(2))

        env = os.environ.copy()
        if fork_support:
            env["GRPC_ENABLE_FORK_SUPPORT"] = "1"
        else:
            env["GRPC_ENABLE_FORK_SUPPORT"] = "0"

        if initial_call:
            env["TEST_INITIAL_CALL"] = "1"

        env["GRPC_SERVER_PORT"] = str(port)

        # Ensure PYTHONPATH is passed to the subprocess (macOS SIP strips it by default)
        env["PYTHONPATH"] = os.environ.get("PYTHONPATH", "")

        # Find a free port
        sock = socket.socket()
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind(("", 0))
        framework_port = sock.getsockname()[1]
        sock.close()

        # Start functions-framework
        process = subprocess.Popen(
            [sys.executable, "-m", "functions_framework", "--target", "hello", "--source", script_path, "--port", str(framework_port)],
            stdout=streams[0], stderr=streams[1], env=env
        )

        try:
            # send a request
            import urllib.request
            req = urllib.request.Request(f"http://localhost:{framework_port}/")
            try:
                with urllib.request.urlopen(req, timeout=10) as response: # noqa: S310
                    resp_text = response.read().decode()
            except Exception as e:
                # server might not be serving successfully, ignore
                resp_text = str(e)

            # terminate process
            process.terminate()

            try:
                process.wait(timeout=_SUBPROCESS_TIMEOUT_S)
            except subprocess.TimeoutExpired:
                sys.stderr.write("Child %d timed out\\n" % process.pid)
                debugger.print_backtraces(process.pid)
                process.kill()
                process.wait()
                raise AssertionError("Parent process timed out.")

            self.assertIn("OK", resp_text, f"Framework HTTP request failed: {resp_text}")
        finally:
            # No need cleanup for fixed file
            # Dump output as in the original fork interop test
            _dump_streams("Client", streams)

            server.stop(None).wait()

if __name__ == "__main__":
    unittest.main(verbosity=2)

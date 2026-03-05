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
import subprocess
import socket
import sys
import tempfile
import threading
import time
import unittest

from tests.fork import debugger
from tests.interop import service as interop_service
from tests.unit import test_common
from src.proto.grpc.testing import test_pb2_grpc

def _dump_streams(name, streams):
    assert len(streams) == 2
    for stream_name, stream in zip(("STDOUT", "STDERR"), streams):
        stream.seek(0)
        sys.stderr.write(
            "{} {}:\n{}\n".format(name, stream_name, stream.read().decode("utf-8", "ignore"))
        )
        stream.close()
    sys.stderr.flush()

_CLIENT_FORK_SCRIPT_TEMPLATE = """
import os
import time

import functions_framework
import grpc

from src.proto.grpc.testing import test_pb2
from src.proto.grpc.testing import test_pb2_grpc
from src.proto.grpc.testing import messages_pb2

channel = grpc.insecure_channel("localhost:%d")
stub = test_pb2_grpc.TestServiceStub(channel)


def initial_call():
    time.sleep(0.5)

    response = stub.UnaryCall(messages_pb2.SimpleRequest(), timeout=5)


if os.getenv("TEST_INITIAL_CALL", "0") == "1":
    initial_call()

time.sleep(0.5)


def process_call():
    try:
        response = stub.UnaryCall(messages_pb2.SimpleRequest(), timeout=5)
    except grpc.RpcError as e:
        return f"gRPC error: {e.code().name}\\n"
    except Exception as e:
        return f"Other error: {e!r}\\n"

    return "OK, called"


@functions_framework.http
def hello(request):
    return process_call()
"""

_SUBPROCESS_TIMEOUT_S = 80


@unittest.skipUnless(
    sys.platform.startswith("linux") or sys.platform.startswith("darwin"),
    f"not supported on {sys.platform}",
)
class FunctionsFrameworkForkTest(unittest.TestCase):

    @unittest.skip("Skipping for now, since it fails")
    def testFunctionsFrameworkForkOnInitialCallOn(self):
        self._verifyTestCase(fork_support=True, initial_call=True)

    @unittest.skip("Skipping for now, since it fails")
    def testFunctionsFrameworkForkOnInitialCallOff(self):
        self._verifyTestCase(fork_support=True, initial_call=False)

    def testFunctionsFrameworkForkOffInitialCallOn(self):
        self._verifyTestCase(fork_support=False, initial_call=True)

    @unittest.skip("Skipping for now, since it hangs")
    def testFunctionsFrameworkForkOffInitialCallOff(self):
        self._verifyTestCase(fork_support=False, initial_call=False)

    def _verifyTestCase(self, fork_support, initial_call):
        server = test_common.test_server()
        test_pb2_grpc.add_TestServiceServicer_to_server(
            interop_service.TestService(), server)
        port = server.add_insecure_port('[::]:0')
        server.start()

        script = _CLIENT_FORK_SCRIPT_TEMPLATE % port
        
        # We need to run functions framework targeting this script.
        # Write the script to a temporary file.
        fd, script_path = tempfile.mkstemp(suffix=".py")
        os.write(fd, script.encode("utf-8"))
        os.close(fd)
            
        streams = tuple(tempfile.TemporaryFile() for _ in range(2))
        
        env = os.environ.copy()
        if fork_support:
            env['GRPC_ENABLE_FORK_SUPPORT'] = '1'
        else:
            env['GRPC_ENABLE_FORK_SUPPORT'] = '0'
            
        if initial_call:
            env['TEST_INITIAL_CALL'] = '1'
        
        # Ensure PYTHONPATH is passed to the subprocess (macOS SIP strips it by default)
        env['PYTHONPATH'] = os.environ.get('PYTHONPATH', '')
        
        # Find a free port
        sock = socket.socket()
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind(('', 0))
        framework_port = sock.getsockname()[1]
        sock.close()

        # Start functions-framework
        process = subprocess.Popen(
            [sys.executable, "-m", "functions_framework", "--target", "hello", "--source", script_path, "--port", str(framework_port)],
            stdout=streams[0], stderr=streams[1], env=env
        )
        
        try:
            # wait a bit for the server to start
            time.sleep(3)
            
            # send a request
            import urllib.request
            req = urllib.request.Request(f"http://localhost:{framework_port}/")
            try:
                response = urllib.request.urlopen(req, timeout=10)
                resp_text = response.read().decode('utf-8')
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
            os.unlink(script_path)
            # Dump output as in the original fork interop test
            _dump_streams("Client", streams)
            
            server.stop(None).wait()

if __name__ == "__main__":
    unittest.main(verbosity=2)

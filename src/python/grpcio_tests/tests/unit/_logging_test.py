# Copyright 2018 gRPC authors.
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
"""Test of gRPC Python's interaction with the python logging module"""

import logging
import subprocess
import sys
import unittest

import grpc

INTERPRETER = sys.executable


class LoggingTest(unittest.TestCase):
    def test_logger_not_occupied(self):
        script = """if True:
            import logging

            import grpc

            if len(logging.getLogger().handlers) != 0:
                raise Exception('expected 0 logging handlers')

        """
        self._verifyScriptSucceeds(script)

    def test_handler_found(self):
        script = """if True:
            import logging

            import grpc
        """
        out, err = self._verifyScriptSucceeds(script)
        self.assertEqual(0, len(err), "unexpected output to stderr")

    def test_can_configure_logger(self):
        script = """if True:
            import logging

            import grpc
            import io

            intended_stream = io.StringIO()
            logging.basicConfig(stream=intended_stream)

            if len(logging.getLogger().handlers) != 1:
                raise Exception('expected 1 logging handler')

            if logging.getLogger().handlers[0].stream is not intended_stream:
                raise Exception('wrong handler stream')

        """
        self._verifyScriptSucceeds(script)

    def test_grpc_logger(self):
        script = """if True:
            import logging

            import grpc

            if "grpc" not in logging.Logger.manager.loggerDict:
                raise Exception('grpc logger not found')

            root_logger = logging.getLogger("grpc")
            if len(root_logger.handlers) != 1:
                raise Exception('expected 1 root logger handler')
            if not isinstance(root_logger.handlers[0], logging.NullHandler):
                raise Exception('expected logging.NullHandler')

        """
        self._verifyScriptSucceeds(script)

    def _verifyScriptSucceeds(self, script):
        process = subprocess.Popen(
            [INTERPRETER, "-c", script],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        out, err = process.communicate()
        self.assertEqual(
            0,
            process.returncode,
            "process failed with exit code %d (stdout: %s, stderr: %s)"
            % (process.returncode, out, err),
        )
        return out, err


if __name__ == "__main__":
    unittest.main(verbosity=2)

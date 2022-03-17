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
"""Tests for raise error."""
import os
import logging
import time
import unittest
import subprocess

_WAIT_PROCESS_ALIVE_MAX_TIME = 10
_WAIT_WHILE_COUNT = 20
_WAIT_INTERVAL = _WAIT_PROCESS_ALIVE_MAX_TIME / _WAIT_WHILE_COUNT
_TEST_EXAMPLE_PATH = (
    os.path.dirname(__file__),
    "raise_error_example",
    "main.py"
)

class SimpleStubsTest(unittest.TestCase):
    def test_main_thread_quit(self):
        test_file = os.path.join(*_TEST_EXAMPLE_PATH)
        p = subprocess.Popen(f"python {test_file}", shell=True)
        p_status = 0
        count = 0
        while count < _WAIT_WHILE_COUNT:
            p_status = p.poll()
            if p_status == 1:
                break
            time.sleep(_WAIT_INTERVAL)
            count += 1
        
        try:
            self.assertEqual(p.poll(), 1)
        finally:
            p.kill()


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO)
    unittest.main(verbosity=2)

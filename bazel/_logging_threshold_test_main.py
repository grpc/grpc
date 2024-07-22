# Copyright 2024 gRPC authors.
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


import tempfile
import sys
import subprocess

LOGGING_ERROR_THRESHOLD = 50
LOGGING_OUT_THRESHOLD = 20

if __name__ == "__main__":
 
    if len(sys.argv) != 2:
        print(f"USAGE: {sys.argv[0]} TARGET_MODULE", file=sys.stderr)

    target_module = sys.argv[1]
    command = [sys.executable, "./src/python/grpcio_tests/tests/unit/_single_module_tester.py", target_module]  
    
    with tempfile.TemporaryFile(mode="w+") as client_stdout:
        with tempfile.TemporaryFile(mode="w+") as client_stderr:
            result = subprocess.run(command, stdout=client_stdout, stderr=client_stderr, text=True) 

            client_stdout.seek(0)
            client_stderr.seek(0)

            stdout_count = len(client_stdout.readlines()) 
            stderr_count = len(client_stderr.readlines())

            if result.returncode != 0:
                sys.exit('Test failure')
            
            if stderr_count > LOGGING_ERROR_THRESHOLD or stdout_count > LOGGING_OUT_THRESHOLD:
                sys.exit('Test failure')
#!/bin/bash
# Copyright 2017 gRPC authors.
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

set -ex

# change to directory of project root
cd "$(dirname "$0")/../../.."

pip install pyjwt cryptography requests

coverage combine "./src/python/grpcio_tests"
coverage html --rcfile=.coveragerc -d ./reports/python
coverage report --rcfile=.coveragerc | \
    "./tools/run_tests/python_utils/check_on_pr.py" \
      --name "python coverage"

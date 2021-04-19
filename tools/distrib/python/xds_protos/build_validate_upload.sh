#! /bin/bash
# Copyright 2021 The gRPC Authors
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

WORK_DIR="$(dirname "$0")"
cd ${WORK_DIR}

# Build the source wheel
python3 setup.py sdist

# Run the tests to ensure all protos are importable
python3 -m pip install .
python3 generated_file_import_test.py

# Upload the package
python3 -m twine check dist/*
python3 -m twine upload dist/*

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

WORK_DIR=$(pwd)/"$(dirname "$0")"
cd ${WORK_DIR}

# Remove existing wheels
rm -rf ${WORK_DIR}/dist

# Generate the package content then build the source wheel
python3 build.py
python3 setup.py bdist_wheel

# Run the tests to ensure all protos are importable, also avoid confusing normal
# imports with relative imports
pushd $(mktemp -d '/tmp/test_xds_protos.XXXXXX')
python3 -m virtualenv env
env/bin/python -m pip install ${WORK_DIR}/dist/*.whl
cp ${WORK_DIR}/__init__.py generated_file_import_test.py
env/bin/python generated_file_import_test.py
popd

# Upload the package
python3 -m twine check dist/*
python3 -m twine upload dist/*

# Copyright 2020 The gRPC Authors
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

# To grpc repo root
cd $(dirname $0)/../../..
ROOT=$(pwd)

source tools/internal_ci/helper_scripts/prepare_build_linux_rc

python3 -m pip install -U nox virtualenv

tools/run_tests/task_runner.py -f artifact manylinux2010 x64 cp38-38 -j 1

ls -al ${ROOT}/artifacts
ls -al ${ROOT}/artifacts/python_manylinux2010_x64_cp38-cp38
ls -al ${ROOT}/artifacts/python_manylinux2010_x64_cp38-cp38/grpcio-1.29.0.dev0-cp38-cp38-manylinux2010_x86_64.whl

git clone https://github.com/googleapis/python-firestore
cd python-firestore
python3 -m nox -s unit-3.8

source .nox/unit-3-8/bin/activate
pip install -U ${ROOT}/artifacts/python_manylinux2010_x64_cp38-cp38/grpcio-1.29.0.dev0-cp38-cp38-manylinux2010_x86_64.whl
deactivate

python3 -m nox -r -s unit-3.8

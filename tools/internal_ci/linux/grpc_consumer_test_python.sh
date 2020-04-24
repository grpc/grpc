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

nighly_build_python_index=$(python3 tools/internal_ci/helper_scripts/fetch_python_nightly_build_index.py)

python3 -m pip install -U nox virtualenv

git clone https://github.com/googleapis/python-firestore
cd python-firestore

# Run the unit tests and
python3 -m nox --sessions unit

# Install nightly build gRPC Python
for dir in .nox/*; do
    ${dir}/bin/pip install --upgrade --force-reinstall --pre --extra-index-url=${nighly_build_python_index} grpcio
done

# Reuse the patched virtualenv to run the tests again
python3 -m nox --reuse-existing-virtualenvs --sessions unit

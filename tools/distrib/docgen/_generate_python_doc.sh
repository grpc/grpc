#! /bin/bash
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
#
# This script is meant to be ran in Docker instance of python:3.8.

set -ex

# Some Python package installation requires permission to change homedir. But
# due to the user-override in all_lang_docgen.sh, the user in the container
# doesn't have a home dir which leads to permission denied error.
HOME="$(mktemp -d)"
export HOME

pip install -r requirements.bazel.txt
tools/run_tests/run_tests.py -c opt -l python --compiler python3.8 --newline_on_success -j 8 --build_only
# shellcheck disable=SC1091
source py38/bin/activate
pip install --upgrade Sphinx
python setup.py doc

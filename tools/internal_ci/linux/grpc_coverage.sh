#!/usr/bin/env bash
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

# Enter the gRPC repo root
cd $(dirname $0)/../../..

source tools/internal_ci/helper_scripts/prepare_build_linux_rc

python tools/run_tests/run_tests.py \
   -l c c++ -x coverage_cpp/sponge_log.xml \
   --use_docker -t -c gcov -j 2 || FAILED="true"

python tools/run_tests/run_tests.py \
   -l python -x coverage_python/sponge_log.xml \
   --use_docker -t -c gcov -j 2 || FAILED="true"

python tools/run_tests/run_tests.py \
   -l ruby -x coverage_ruby/sponge_log.xml \
   --use_docker -t -c gcov -j 2 || FAILED="true"

python tools/run_tests/run_tests.py \
   -l php -x coverage_php/sponge_log.xml \
   --use_docker -t -c gcov -j 2 || FAILED="true"
  
# HTML reports can't be easily displayed in GCS, so create a zip archive
# and put it under reports directory to get it uploaded as an artifact.
zip -q -r coverage_report.zip reports || true
rm -rf reports || true
mkdir reports  || true
mv coverage_report.zip reports || true

if [ "$FAILED" != "" ]
then
  exit 1
fi

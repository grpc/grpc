#!/usr/bin/env bash
# Copyright 2015, gRPC authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# This script is invoked by Jenkins and triggers a test run based on
# env variable settings.
#
# Setting up rvm environment BEFORE we set -ex.
[[ -s /etc/profile.d/rvm.sh ]] && . /etc/profile.d/rvm.sh
# To prevent cygwin bash complaining about empty lines ending with \r
# we set the igncr option. The option doesn't exist on Linux, so we fallback
# to just 'set -ex' there.
# NOTE: No empty lines should appear in this file before igncr is set!
set -ex -o igncr || set -ex

if [ "$platform" == "linux" ]
then
  PLATFORM_SPECIFIC_ARGS="--use_docker --measure_cpu_costs"
elif [ "$platform" == "freebsd" ]
then
  export MAKE=gmake
fi

unset platform  # variable named 'platform' breaks the windows build

python tools/run_tests/run_tests.py \
  $PLATFORM_SPECIFIC_ARGS           \
  -t                                \
  -l $language                      \
  -c $config                        \
  -x report.xml                     \
  -j 2                              \
  $@ || TESTS_FAILED="true"

if [ ! -e reports/index.html ]
then
  mkdir -p reports
  echo 'No reports generated.' > reports/index.html
fi

if [ "$TESTS_FAILED" != "" ]
then
  exit 1
fi

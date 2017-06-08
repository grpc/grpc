#!/usr/bin/env bash
# Copyright 2015 gRPC authors.
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
# This script is invoked by Jenkins and runs portability tests based on
# env variable setting.
#
# Setting up rvm environment BEFORE we set -ex.
[[ -s /etc/profile.d/rvm.sh ]] && . /etc/profile.d/rvm.sh
# To prevent cygwin bash complaining about empty lines ending with \r
# we set the igncr option. The option doesn't exist on Linux, so we fallback
# to just 'set -ex' there.
# NOTE: No empty lines should appear in this file before igncr is set!
set -ex -o igncr || set -ex

echo "building $scenario"

# If scenario has _bo suffix, add --build_only flag.
# Short suffix name had to been chosen due to path length limit on Windows.
if [ "$scenario" != "${scenario%_bo}" ]
then
  scenario="${scenario%_bo}"
  BUILD_ONLY_MAYBE="--build_only"
fi

parts=($(echo $scenario | tr '_' ' '))  # split scenario into parts

curr_platform=${parts[0]}  # variable named 'platform' breaks the windows build
curr_arch=${parts[1]}
curr_compiler=${parts[2]}

config='dbg'

if [ "$curr_platform" == "linux" ]
then
  USE_DOCKER_MAYBE="--use_docker"
fi

python tools/run_tests/run_tests.py $USE_DOCKER_MAYBE $BUILD_ONLY_MAYBE -t -l $language -c $config --arch ${curr_arch} --compiler ${curr_compiler} -x report.xml -j 3 $@

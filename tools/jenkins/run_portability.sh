#!/usr/bin/env bash
# Copyright 2015, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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

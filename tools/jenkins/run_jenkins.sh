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
elif [ "$platform" == "macos" ]
then
  # Install python-protobuf which is needed by nanopb
  # the other platforms have it installed by default
  MACOS_NANOPB_VIRTUAL_ENV=$(mktemp -d /tmp/grpc-nanopb-XXXXXX)
  virtualenv ${MACOS_NANOPB_VIRTUAL_ENV}
  source ${MACOS_NANOPB_VIRTUAL_ENV}/bin/activate
  pip install protobuf==3.0.0b2
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

if [ "$platform" == "macos" ]
then
  deactivate
  rm -rf ${MACOS_NANOPB_VIRTUAL_ENV}
fi

if [ "$TESTS_FAILED" != "" ]
then
  exit 1
fi

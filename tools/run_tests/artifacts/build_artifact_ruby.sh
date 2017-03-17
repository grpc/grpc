#!/bin/bash
# Copyright 2016, Google Inc.
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
set -ex

SYSTEM=`uname | cut -f 1 -d_`

cd $(dirname $0)/../../..
set +ex
[[ -s /etc/profile.d/rvm.sh ]] && . /etc/profile.d/rvm.sh
[[ -s "$HOME/.rvm/scripts/rvm" ]] && source "$HOME/.rvm/scripts/rvm"
set -ex

if [ "$SYSTEM" == "MSYS" ] ; then
  SYSTEM=MINGW32
fi
if [ "$SYSTEM" == "MINGW64" ] ; then
  SYSTEM=MINGW32
fi

if [ "$SYSTEM" == "MINGW32" ] ; then
  echo "Need Linux to build the Windows ruby gem."
  exit 1
fi

set +ex
rvm use default
gem install bundler --update

tools/run_tests/helper_scripts/bundle_install_wrapper.sh

set -ex

rake gem:native

if [ "$SYSTEM" == "Darwin" ] ; then
  rm `ls pkg/*.gem | grep -v darwin`
fi

mkdir -p artifacts

cp pkg/*.gem artifacts

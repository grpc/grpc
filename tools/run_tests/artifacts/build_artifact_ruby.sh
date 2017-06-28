#!/bin/bash
# Copyright 2016 gRPC authors.
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

mkdir -p "${ARTIFACTS_OUT}"

cp pkg/*.gem "${ARTIFACTS_OUT}"/

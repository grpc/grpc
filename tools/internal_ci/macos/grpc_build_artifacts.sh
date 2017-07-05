#!/bin/bash
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

# change to grpc repo root
cd $(dirname $0)/../../..

source tools/internal_ci/helper_scripts/prepare_build_macos_rc

# python 3.5
wget -q https://www.python.org/ftp/python/3.5.2/python-3.5.2-macosx10.6.pkg
sudo installer -pkg python-3.5.2-macosx10.6.pkg -target /

# install cython for all python versions
python2.7 -m pip install cython setuptools wheel
python3.4 -m pip install cython setuptools wheel
python3.5 -m pip install cython setuptools wheel
python3.6 -m pip install cython setuptools wheel

# node-gyp (needed for node artifacts)
npm install -g node-gyp

# php dependencies: pecl
curl -O http://pear.php.net/go-pear.phar
sudo php -d detect_unicode=0 go-pear.phar

# needed to build ruby artifacts
gem install rake-compiler
wget https://raw.githubusercontent.com/grpc/grpc/master/tools/distrib/build_ruby_environment_macos.sh
bash build_ruby_environment_macos.sh

gem install rubygems-update
update_rubygems

tools/run_tests/task_runner.py -f artifact macos

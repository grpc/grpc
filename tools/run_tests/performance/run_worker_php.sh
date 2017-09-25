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

source ~/.rvm/scripts/rvm
set -ex

cd $(dirname $0)/../../..
repo=$(pwd)
# First set up all dependences needed for PHP QPS test
cd $repo
cd src/php/tests/qps
composer install
# Install protobuf C-extension for php
cd vendor/google/protobuf/php/ext/google/protobuf
phpize
./configure
make
# The proxy worker for PHP is implemented in Ruby
cd $repo
ruby src/ruby/qps/proxy-worker.rb $@


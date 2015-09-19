#!/bin/bash
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
# This script is invoked by run_jekins.sh. It contains the test logic
# that should run inside a docker container.
set -e

mkdir -p /var/local/git
git clone --recursive /var/local/jenkins/grpc /var/local/git/grpc

cd /var/local/git/grpc
nvm use 0.12
rvm use ruby-2.1

# TODO(jtattermusch): use cleaner way to install root certs
mkdir -p /usr/local/share/grpc
cp etc/roots.pem /usr/local/share/grpc/

# build C++ interop client & server
make interop_client interop_server

# build C# interop client & server
make install_grpc_csharp_ext
(cd src/csharp && mono /var/local/NuGet.exe restore Grpc.sln)
(cd src/csharp && xbuild Grpc.sln)

# build Node interop client & server
npm install -g node-gyp
make install_c -C /var/local/git/grpc
(cd src/node && npm install && node-gyp rebuild)

# build Ruby interop client and server
(cd src/ruby && gem update bundler && bundle && rake compile:grpc)

# TODO(jtattermusch): add python

# build PHP interop client
# TODO(jtattermusch): make php work
# TODO(jtattermusch): prerequisites should be installed sooner than here.
# Install composer
#curl -sS https://getcomposer.org/installer | php
#mv composer.phar /usr/local/bin/composer
# Download the patched PHP protobuf so that PHP gRPC clients can be generated
# from proto3 schemas.
#git clone https://github.com/stanley-cheung/Protobuf-PHP.git /var/local/git/protobuf-php
#(cd src/php/ext/grpc && phpize && ./configure && make)
#rvm all do gem install ronn rake
#(cd /var/local/git/protobuf-php \
#  && rvm all do rake pear:package version=1.0 \
#  && pear install Protobuf-1.0.tgz)
#(cd src/php && composer install)
#(cd src/php && protoc-gen-php -i tests/interop/ -o tests/interop/ tests/interop/test.proto)

# run the cloud-to-prod interop tests
tools/run_tests/run_interop_tests.py -l $language

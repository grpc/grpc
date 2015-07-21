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
# This script is invoked by Jenkins and triggers a test run of
# linuxbrew installation of a selected language
set -ex

if [ "$platform" == "linux" ]; then

  if [ "$dist_channel" == "homebrew" ]; then

    sha1=$(sha1sum tools/jenkins/grpc_linuxbrew/Dockerfile | cut -f1 -d\ )
    DOCKER_IMAGE_NAME=grpc_linuxbrew_$sha1

    docker build -t $DOCKER_IMAGE_NAME tools/jenkins/grpc_linuxbrew

    supported="python nodejs ruby php"

    if [ "$language" == "core" ]; then
      command="curl -fsSL https://goo.gl/getgrpc | bash -"
    elif [[ "$supported" =~ "$language" ]]; then
      command="curl -fsSL https://goo.gl/getgrpc | bash -s $language"
    else
      echo "unsupported language $language"
      exit 1
    fi

    docker run $DOCKER_IMAGE_NAME bash -l \
      -c "nvm use 0.12; \
          npm set unsafe-perm true; \
          rvm use ruby-2.1; \
          $command"

  else
    echo "Unsupported $platform dist_channel $dist_channel"
    exit 1
  fi

elif [ "$platform" == "macos" ]; then

  if [ "$dist_channel" == "homebrew" ]; then
    which brew # TODO: for debug, can be removed later
    brew list -l
    dir=/tmp/homebrew-test-$language
    rm -rf $dir
    mkdir -p $dir
    git clone https://github.com/Homebrew/homebrew.git $dir
    cd $dir
    # TODO: Uncomment these when the general structure of the script is verified
    # PATH=$dir/bin:$PATH brew tap homebrew/dupes
    # PATH=$dir/bin:$PATH brew install zlib
    # PATH=$dir/bin:$PATH brew install openssl
    # PATH=$dir/bin:$PATH brew tap grpc/grpc
    # PATH=$dir/bin:$PATH brew install --without-python google-protobuf
    # PATH=$dir/bin:$PATH brew install grpc
    PATH=$dir/bin:$PATH brew list -l
    brew list -l
    cd ~/ 
    rm -rf $dir
    echo $PATH # TODO: for debug, can be removed later
    brew list -l # TODO: for debug, can be removed later

  else
    echo "Unsupported $platform dist_channel $dist_channel"
    exit 1
  fi

else
  echo "unsupported platform $platform"
  exit 1
fi

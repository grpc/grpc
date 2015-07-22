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

    # build docker image, contains all pre-requisites
    docker build -t $DOCKER_IMAGE_NAME tools/jenkins/grpc_linuxbrew

    if [ "$language" == "core" ]; then
      command="curl -fsSL https://goo.gl/getgrpc | bash -"
    elif [[ "python nodejs ruby php" =~ "$language" ]]; then
      command="curl -fsSL https://goo.gl/getgrpc | bash -s $language"
    else
      echo "unsupported language $language"
      exit 1
    fi

    # run per-language homebrew installation script
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
    # system installed homebrew, don't interfere
    brew list -l

    # Set up temp directories for test installation of homebrew
    brew_root=/tmp/homebrew-test-$language
    rm -rf $brew_root
    mkdir -p $brew_root
    git clone https://github.com/Homebrew/homebrew.git $brew_root

    # Install grpc via homebrew
    #
    # The temp $PATH env variable makes sure we are operating at the right copy of
    # temp homebrew installation, and do not interfere with the system's main brew
    # installation.
    #
    # TODO: replace the next section with the actual homebrew installation script
    # i.e. curl -fsSL https://goo.gl/getgrpc | bash -s $language
    # need to resolve a bunch of environment and privilege issue on the jenkins
    # mac machine itself
    export OLD_PATH=$PATH
    export PATH=$brew_root/bin:$PATH
    cd $brew_root
    brew tap homebrew/dupes
    brew install zlib
    brew install openssl
    brew tap grpc/grpc
    brew install --without-python google-protobuf
    brew install grpc
    brew list -l

    # Install per-language modules/extensions on top of core grpc
    #
    # If a command below needs root access, the binary had been added to
    # /etc/sudoers. This step needs to be repeated if we add more mac instances
    # to our jenkins project.
    #
    # Examples (lines that needed to be added to /etc/sudoers):
    # + Defaults        env_keep += "CFLAGS CXXFLAGS LDFLAGS enable_grpc"
    # + jenkinsnode1 ALL=(ALL) NOPASSWD: /usr/bin/pecl, /usr/local/bin/pip,
    # +   /usr/local/bin/npm
    case $language in
      *core*) ;;
      *python*)
        sudo CFLAGS=-I$brew_root/include LDFLAGS=-L$brew_root/lib pip install grpcio
        pip list | grep grpcio
        echo 'y' | sudo pip uninstall grpcio
        ;;
      *nodejs*)
        sudo CXXFLAGS=-I$brew_root/include LDFLAGS=-L$brew_root/lib npm install grpc
        npm list | grep grpc
        sudo npm uninstall grpc
        ;;
      *ruby*)
        gem install grpc -- --with-grpc-dir=$brew_root
        gem list | grep grpc
        gem uninstall grpc
        ;;
      *php*)
        sudo enable_grpc=$brew_root CFLAGS="-Wno-parentheses-equality" pecl install grpc-alpha
        pecl list | grep grpc
        sudo pecl uninstall grpc
        ;;
      *)
        echo "Unsupported language $language"
        exit 1
        ;;
    esac

    # clean up
    cd ~/ 
    rm -rf $brew_root

    # Make sure the system brew installation is still unaffected
    export PATH=$OLD_PATH
    brew list -l

  else
    echo "Unsupported $platform dist_channel $dist_channel"
    exit 1
  fi

else
  echo "unsupported platform $platform"
  exit 1
fi

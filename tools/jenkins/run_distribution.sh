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

# Our homebrew installation script command, per language
# Can be used in both linux and macos
if [ "$language" == "core" ]; then
  command="curl -fsSL https://goo.gl/getgrpc | bash -"
elif [[ "python nodejs ruby php" =~ "$language" ]]; then
  command="curl -fsSL https://goo.gl/getgrpc | bash -s $language"
else
  echo "unsupported language $language"
  exit 1
fi

if [ "$platform" == "linux" ]; then

  if [ "$dist_channel" == "homebrew" ]; then

    sha1=$(sha1sum tools/dockerfile/grpc_linuxbrew/Dockerfile | cut -f1 -d\ )
    DOCKER_IMAGE_NAME=grpc_linuxbrew_$sha1

    # build docker image, contains all pre-requisites
    docker build -t $DOCKER_IMAGE_NAME tools/dockerfile/grpc_linuxbrew

    # run per-language homebrew installation script
    docker run --rm=true $DOCKER_IMAGE_NAME bash -l \
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

    echo "Formulas installed by system-wide homebrew (before)"
    brew list -l

    # Save the original PATH so that we can run the system `brew` command
    # again at the end of the script
    export ORIGINAL_PATH=$PATH

    # Set up temp directories for test installation of homebrew
    brew_root=/tmp/homebrew-test-$language
    rm -rf $brew_root
    mkdir -p $brew_root
    git clone https://github.com/Homebrew/homebrew.git $brew_root

    # Make sure we are operating at the right copy of temp homebrew
    # installation
    export PATH=$brew_root/bin:$PATH

    # Set up right environment for each language
    case $language in
      *python*)
        rm -rf jenkins_python_venv
        virtualenv jenkins_python_venv
        source jenkins_python_venv/bin/activate
        ;;
      *nodejs*)
        export PATH=$HOME/.nvm/versions/node/v0.12.7/bin:$PATH
        ;;
      *ruby*)
        export PATH=/usr/local/rvm/rubies/ruby-2.2.1/bin:$PATH
        ;;
      *php*)
        export CFLAGS="-Wno-parentheses-equality"
        ;;
    esac

    # Run our homebrew installation script
    bash -c "$command"

    # Uninstall / clean up per-language modules/extensions after the test
    case $language in
      *python*)
        deactivate
        rm -rf jenkins_python_venv
        ;;
      *nodejs*)
        npm list -g | grep grpc
        npm uninstall -g grpc
        ;;
      *ruby*)
        gem list | grep grpc
        gem uninstall grpc
        ;;
      *php*)
        rm grpc.so
        ;;
    esac

    # Clean up
    rm -rf $brew_root

    echo "Formulas installed by system-wide homebrew (after, should be unaffected)"
    export PATH=$ORIGINAL_PATH
    brew list -l

  else
    echo "Unsupported $platform dist_channel $dist_channel"
    exit 1
  fi

else
  echo "unsupported platform $platform"
  exit 1
fi

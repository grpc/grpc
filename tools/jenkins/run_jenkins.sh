#!/bin/sh
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
# Bootstrap into bash
[ -z $1 ] && exec bash $0 bootstrapped
# Setting up rvm environment BEFORE we set -ex.
[[ -s /etc/profile.d/rvm.sh ]] && . /etc/profile.d/rvm.sh
# To prevent cygwin bash complaining about empty lines ending with \r
# we set the igncr option. The option doesn't exist on Linux, so we fallback
# to just 'set -ex' there.
# NOTE: No empty lines should appear in this file before igncr is set!
set -ex -o igncr || set -ex

# Grabbing the machine's architecture
arch=`uname -m`

case $platform in
  i386)
    arch="i386"
    platform="linux"
    docker_suffix=_32bits
    ;;
esac

if [ "$platform" == "linux" ]
then
  echo "building $language on Linux"

  cd `dirname $0`/../..
  git_root=`pwd`
  cd -

  mkdir -p /tmp/ccache

  # Use image name based on Dockerfile checksum
  DOCKER_IMAGE_NAME=grpc_jenkins_slave$docker_suffix_`sha1sum tools/jenkins/grpc_jenkins_slave/Dockerfile | cut -f1 -d\ `

  # Make sure docker image has been built. Should be instantaneous if so.
  docker build -t $DOCKER_IMAGE_NAME tools/jenkins/grpc_jenkins_slave$docker_suffix

  # Create a local branch so the child Docker script won't complain
  git branch jenkins-docker

  # Make sure the CID file is gone.
  rm -f docker.cid

  # Zookeeper test server address
  export GRPC_ZOOKEEPER_SERVER_TEST="grpc-jenkins-master"

  # Run tests inside docker
  docker run \
    -e "config=$config" \
    -e "language=$language" \
    -e "arch=$arch" \
    -e CCACHE_DIR=/tmp/ccache \
    -i \
    -v "$git_root:/var/local/jenkins/grpc" \
    -v /tmp/ccache:/tmp/ccache \
    --cidfile=docker.cid \
    $DOCKER_IMAGE_NAME \
    bash -l /var/local/jenkins/grpc/tools/jenkins/docker_run_jenkins.sh || DOCKER_FAILED="true"

  DOCKER_CID=`cat docker.cid`
  docker kill $DOCKER_CID
  docker cp $DOCKER_CID:/var/local/git/grpc/report.xml $git_root
  sleep 4
  docker rm $DOCKER_CID || true
elif [ "$platform" == "interop" ]
then
  python tools/run_tests/run_interops.py --language=$language
elif [ "$platform" == "windows" ]
then
  echo "building $language on Windows"

  # Prevent msbuild from picking up "platform" env variable, which would break the build
  unset platform

  # TODO(jtattermusch): integrate nuget restore in a nicer way.
  /cygdrive/c/nuget/nuget.exe restore vsprojects/grpc.sln
  /cygdrive/c/nuget/nuget.exe restore src/csharp/Grpc.sln

  python tools/run_tests/run_tests.py -t -l $language -x report.xml || true

elif [ "$platform" == "macos" ]
then
  echo "building $language on MacOS"

  ./tools/run_tests/run_tests.py -t -l $language -c $config -x report.xml || true

elif [ "$platform" == "freebsd" ]
then
  echo "building $language on FreeBSD"

  MAKE=gmake ./tools/run_tests/run_tests.py -t -l $language -c $config -x report.xml || true
else
  echo "Unknown platform $platform"
  exit 1
fi
